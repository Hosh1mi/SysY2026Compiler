// LCSSA 实现（plan 阶段 2）。
//
// 对每个循环（由内向外）：
//   1. 扫描循环内每条指令的 use_list_，按"使用点"判定是否在循环外：
//      非 phi 用户看其所在块；phi 用户看对应 incoming 块（phi 的 use
//      语义上发生在入边块末尾——exit phi 引用循环值因此是合法的）。
//   2. 有循环外使用的值 v：在每个被 def 支配的 exit 块顶部插
//      %v.lcssa = phi（每个 exit 前驱一条入边，值都是 v；dedicated
//      exits 保证前驱全在循环内，且 def 支配 exit ⇒ def 支配各前驱）。
//   3. 循环外的 use 改写到支配它的那个 exit phi。
//
// CFG 不变，LoopInfo/支配信息全程有效。

#include "../../include/mid/opt/lcssa.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <map>
#include <vector>

void LCSSA::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

bool LCSSA::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty())
        return false;

    LoopInfo LI;
    LI.analyze(func);
    if (LI.allLoops().empty())
        return false;

    // 由内向外：内层插入的 lcssa phi 是外层循环内的定义，外层按需续接
    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    bool changed = false;
    for (auto *loop : loops)
        changed |= runOnLoop(loop, LI);

    if (changed)
        func->set_instr_name();
    return changed;
}

bool LCSSA::runOnLoop(Loop *loop, LoopInfo &LI) {
    bool changed = false;

    // 快照指令清单：改写过程会新增 exit phi（exit 在循环外，不影响），
    // 但稳妥起见不在遍历容器时修改它。用确定序 blocksOrdered——
    // 快照顺序决定 exit phi 的插入顺序，指针序会跨进程漂移。
    std::vector<Instruction *> insts;
    for (auto *bb : loop->blocksOrdered)
        for (auto *inst : bb->instr_list_)
            insts.push_back(inst);

    for (auto *inst : insts) {
        // 1. 收集循环外的 use
        std::vector<std::pair<Instruction *, unsigned>> outsideUses;
        for (const auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_)
                continue;
            BasicBlock *useBlock = user->parent_;
            if (user->is_phi()) {
                // phi 的 use 位置 = 对应 incoming 块末尾
                auto *incoming = dynamic_cast<BasicBlock *>(
                    user->get_operand(use.arg_no_ + 1));
                if (incoming)
                    useBlock = incoming;
            }
            if (!loop->isInLoop(useBlock))
                outsideUses.emplace_back(user, use.arg_no_);
        }
        if (outsideUses.empty())
            continue;

        // 2. 在被 def 支配的 exit 块插入 lcssa phi（每值缓存复用）。
        //    有序 vector 而非 map：步骤 3 按此顺序为 use 挑支配 phi，
        //    多个候选同时支配时选择必须确定（exits 已是 RPO 序）。
        std::vector<std::pair<BasicBlock *, PhiInst *>> exitPhi;
        for (auto *exit : loop->exits) {
            if (!LI.dominates(inst->parent_, exit))
                continue;
            // 防御：非 dedicated exit（存在循环外前驱）时，"每前驱入边都是
            // 该循环值"不成立，插 phi 即错译。LoopSimplify 保证 dedicated，
            // 但这里不依赖调度顺序，违例直接跳过该 exit。
            bool dedicated = true;
            for (auto *pred : exit->pre_bbs_) {
                if (!loop->isInLoop(pred)) { dedicated = false; break; }
            }
            if (!dedicated)
                continue;
            auto *phi = PhiInst::create_phi(inst->type_, exit);
            for (auto *pred : exit->pre_bbs_)
                phi->addIncoming(inst, pred);
            exit->add_instruction_front(phi);
            exitPhi.emplace_back(exit, phi);
        }
        if (exitPhi.empty())
            continue;

        // 3. 改写每个循环外 use 到支配它的 exit phi
        for (auto &[user, idx] : outsideUses) {
            BasicBlock *useBlock = user->parent_;
            if (user->is_phi()) {
                auto *incoming = dynamic_cast<BasicBlock *>(
                    user->get_operand(idx + 1));
                if (incoming)
                    useBlock = incoming;
            }
            for (auto &[exit, phi] : exitPhi) {
                if (user == phi)
                    continue;           // 不要改写刚插的 lcssa phi 自身
                if (LI.dominates(exit, useBlock)) {
                    user->set_operand(idx, phi);
                    changed = true;
                    break;
                }
                // 无支配的 exit phi：保持原样，L3 校验会告警
            }
        }

        changed = true;
    }

    return changed;
}
