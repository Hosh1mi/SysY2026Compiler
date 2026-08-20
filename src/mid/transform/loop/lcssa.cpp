/**
 * @file lcssa.cpp
 * @brief 循环闭包 SSA 形式：把循环外对循环内定义的使用改写为经退出块 PHI 传递的 LCSSA 形式。
 * @details 外部使用按实际发生边判断；出口 PHI 只放在定义可支配的专用退出块，并重写其支配范围内的使用。
 */

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

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LCSSA::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LCSSA::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
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

/**
 * @brief 在单个循环上运行本变换。
 * @param loop 待检查或变换的循环。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LCSSA::runOnLoop(Loop *loop, const DominatorTreeAnalysis &DT) {
    // 使用点是否在循环外要按边语义判断：普通指令看用户块，PHI 看对应 incoming 块。
    // 对每个逃逸定义只在其支配的专用退出块建立 PHI，再改写该出口支配范围内的使用。
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

        // 2. 只在定义能够支配、且所有前驱均来自循环内的专用出口建立 LCSSA PHI。
        //    这样 PHI 的每条入边都可以合法直接引用当前循环内定义。
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

        // 3. 将外部 use 改写为支配该使用点的出口 PHI；PHI 用户仍按 incoming 边
        //    所在块判断支配关系，而不是按 PHI 自身所在块判断。
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
