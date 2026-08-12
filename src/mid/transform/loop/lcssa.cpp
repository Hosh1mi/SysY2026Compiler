// 对每个循环（由内向外）：
//   1. 扫描循环内每条指令的 use_list_，按"使用点"判定是否在循环外：
//      非 phi 用户看其所在块；phi 用户看对应 incoming 块（phi 的 use
//      语义上发生在入边块末尾——exit phi 引用循环值因此是合法的）。
//   2. 有循环外使用的值 v：在每个被 def 支配的 exit 块顶部插
//      %v.lcssa = phi（每个 exit 前驱一条入边，值都是 v；dedicated
//      exits 保证前驱全在循环内，且 def 支配 exit ⇒ def 支配各前驱）。
//   3. 循环外的 use 改写到支配它的那个 exit phi。

#include "../../../include/mid/opt/lcssa.hpp"
#include "../../../include/mid/analysis/loopUtils.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <map>
#include <vector>

void LCSSA::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LCSSA::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

bool LCSSA::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty())
        return false;

    LoopInfo &LI = AM.getLoopInfo(func);
    DominatorTreeAnalysis &DT = AM.getDominatorTree(func);
    if (LI.allLoops().empty())
        return false;

    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    bool changed = false;
    for (auto *loop : loops)
        changed |= runOnLoop(loop, DT);

    if (changed)
        func->set_instr_name();
    return changed;
}

bool LCSSA::runOnLoop(Loop *loop, const DominatorTreeAnalysis &DT) {
    bool changed = false;

    std::vector<Instruction *> insts;
    for (auto *bb : loop->blocksOrdered)
        for (auto *inst : bb->instr_list_)
            insts.push_back(inst);

    for (auto *inst : insts) {
        // 1. 收集循环外的 use
        std::vector<std::pair<Instruction *, unsigned>> outsideUses;
        for (const auto &use : inst->use_list_) {
            auto *user = use.user_;
            if (!user || !user->parent_)
                continue;
            BasicBlock *useBlock = getSemanticUseBlock(user, use.operand_index_);
            if (!loop->isInLoop(useBlock))
                outsideUses.emplace_back(user, use.operand_index_);
        }
        if (outsideUses.empty())
            continue;

        std::vector<std::pair<BasicBlock *, PhiInst *>> exitPhi;
        for (auto *exit : loop->exits) {
            if (!DT.dominates(inst->parent_, exit))
                continue;

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

        for (auto &[user, idx] : outsideUses) {
            BasicBlock *useBlock = getSemanticUseBlock(user, idx);
            for (auto &[exit, phi] : exitPhi) {
                if (user == phi)
                    continue;           
                if (DT.dominates(exit, useBlock)) {
                    user->set_operand(idx, phi);
                    changed = true;
                    break;
                }
            }
        }

        changed = true;
    }

    return changed;
}
