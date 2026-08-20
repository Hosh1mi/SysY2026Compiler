/**
 * @file loopCanonicalization.cpp
 * @brief 实现循环 CFG 规范化：创建专用预头、单一回边块和专用退出块，并维护 PHI 与前驱/后继关系。
 * @details 实现按“预头、回边、退出边”的顺序迭代规范化；每次结构变化后重新分析循环，避免继续使用失效的 Loop 指针。
 */

#include "../../include/mid/transform/loopCanonicalization.hpp"

#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <vector>

namespace {

/**
 * @brief 原地执行 insertPreheader 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool insertPreheader(Loop *loop, Function *func);
bool insertBackedgeBlock(Loop *loop, Function *func);
bool insertDedicatedExits(Loop *loop, Function *func);

} // namespace

/**
 * @brief 反复规范化函数中的自然循环，直到 preheader、回边和出口形态稳定。
 * @param func 待规范化循环 CFG 的函数。
 * @return 至少修改过一次 CFG 时返回 true，否则返回 false。
 * @details 每次结构改写后重新构建 LoopInfo，避免继续使用已经失效的 Loop 对象。
 */
bool canonicalizeLoopForm(Function *func) {
    // 每次只提交一种结构修复，随后重新构建 LoopInfo。
    // 这是因为插入预头、回边块或退出块都会使现有 Loop* 和 CFG 分析立即失效。
    if (func->basic_blocks_.empty()) return false;

    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;

        LoopInfo LI;
        LI.analyze(func);

        // 由内向外处理。子循环插入的新块会在下一轮分析中计入父循环，
        // 避免父循环依据旧 blocks 集合做错误的结构判断。
        std::vector<Loop *> sorted;
        for (auto &l : LI.allLoops())
            sorted.push_back(l.get());
        std::sort(sorted.begin(), sorted.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });

        for (auto *loop : sorted) {
            if (::insertPreheader(loop, func) ||
                ::insertBackedgeBlock(loop, func) ||
                ::insertDedicatedExits(loop, func)) {
                changed = true;
                progress = true;
                func->set_instr_name();
                break;
            }
        }
    }

    return changed;
}

namespace {

// Return true if bb is already a valid preheader for header:
//   - it has exactly one successor, the loop header;
//   - it terminates with an unconditional branch to the header.
// A preheader may contain setup instructions, and the function entry block may
// be a preheader when it has no other successor.
/**
 * @brief 判断 isExistingPreheader 所描述的结构、合法性或安全条件是否成立。
 * @param bb 目标或待修改的基本块。
 * @param header 循环头基本块。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isExistingPreheader(BasicBlock *bb, BasicBlock *header) {
    if (bb->succ_bbs_.size() != 1 || bb->succ_bbs_[0] != header)
        return false;
    auto *term = bb->get_terminator();
    if (!term || !term->is_br() || term->num_ops() != 1)
        return false;
    if (term->get_operand(0) != header)
        return false;
    return true;
}

/**
 * @brief 实现 placeBlockBefore 对应的局部分析或变换辅助逻辑。
 * @param func 待分析或改写的函数。
 * @param block 目标或待检查的基本块。
 * @param before 参数 `before`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void placeBlockBefore(Function *func, BasicBlock *block, BasicBlock *before) {
    auto &bbs = func->basic_blocks_;
    auto blockIt = std::find(bbs.begin(), bbs.end(), block);
    if (blockIt != bbs.end())
        bbs.erase(blockIt);

    auto beforeIt = std::find(bbs.begin(), bbs.end(), before);
    if (beforeIt != bbs.end())
        bbs.insert(beforeIt, block);
    else
        bbs.push_back(block);
}

/**
 * @brief 实现 placeBlockAfter 对应的局部分析或变换辅助逻辑。
 * @param func 待分析或改写的函数。
 * @param block 目标或待检查的基本块。
 * @param after 参数 `after`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void placeBlockAfter(Function *func, BasicBlock *block, BasicBlock *after) {
    auto &bbs = func->basic_blocks_;
    auto blockIt = std::find(bbs.begin(), bbs.end(), block);
    if (blockIt != bbs.end())
        bbs.erase(blockIt);

    auto afterIt = std::find(bbs.begin(), bbs.end(), after);
    if (afterIt != bbs.end())
        bbs.insert(afterIt + 1, block);
    else
        bbs.push_back(block);
}

/**
 * @brief 原地执行 replaceBranchTarget 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param pred 前驱基本块。
 * @param oldTarget 需要替换的原分支目标。
 * @param newTarget 替换后的新分支目标。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldTarget,
                                BasicBlock *newTarget) {
    auto *term = pred->get_terminator();
    if (!term || !term->is_br())
        return;

    for (unsigned i = 0; i < term->num_ops(); i++) {
        if (term->get_operand(i) == oldTarget)
            term->set_operand(i, newTarget);
    }
}

/**
 * @brief 原地执行 insertPreheader 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool insertPreheader(Loop *loop, Function *func) {
    BasicBlock *header = loop->header;
    if (!header) return false;

    // ── 1. Collect outside predecessors ────────────────────────────────
    std::vector<BasicBlock *> outsidePreds;
    for (auto *pred : header->pre_bbs_) {
        if (!loop->isInLoop(pred))
            outsidePreds.push_back(pred);
    }

    // Degenerate: no outside predecessors → function entry is the header
    // or the loop is unreachable. Skip.
    if (outsidePreds.empty())
        return false;

    // ── 2. 检查是否已经存在专用 preheader ──────────────────────────────
    // 合法 preheader 必须是 header 的唯一出口前驱，并且只能跳向 header。
    if (outsidePreds.size() == 1 && isExistingPreheader(outsidePreds[0], header))
        return false;

    // ── 3. Check if we need a preheader at all ─────────────────────────
    // 唯一环外前驱不一定是合法预头：它可能同时跳向 header 和其他块。
    // 上面的 isExistingPreheader 是唯一判据；未通过时仍要分裂入口边。

    // ── 4. Create the preheader block ──────────────────────────────────
    std::string preheaderName = header->name_ + ".preheader";
    auto *preheader = new BasicBlock(func->parent_, preheaderName, func);
    placeBlockBefore(func, preheader, header);

    // Unconditional branch from preheader to header
    new BranchInst(header, preheader);

    // ── 5. Redirect all outside predecessors to the preheader ──────────
    for (auto *pred : outsidePreds) {
        auto *term = pred->get_terminator();
        if (!term) continue;

        // Update CFG: remove old edge pred→header
        pred->remove_succ_basic_block(header);
        header->remove_pre_basic_block(pred);

        // 先把所有循环外前驱改为跳向新 preheader，再统一维护前驱/后继关系。
        replaceBranchTarget(pred, header, preheader);

        // Add new CFG edge pred→preheader
        pred->add_succ_basic_block(preheader);
        preheader->add_pre_basic_block(pred);
    }

    // ── 6. Fix phi nodes in the header ─────────────────────────────────
    // 将 header PHI 的多条环外入边收拢成一条 preheader 入边。
    for (auto *instr : header->instr_list_) {
        if (!instr->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instr);

        // 收集来自循环外前驱的入值。逆序删除 (value, pred) 对，避免下标移动
        // 导致尚未访问的 PHI 操作数被跳过。
        std::vector<Value *>    outsideVals;
        std::vector<BasicBlock *> outsideBBs; // for dedup check

        for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
            // 只折叠循环外入边；回边仍须直接保留在 header PHI 中。
            auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!predBB) continue;

            bool isOutside = false;
            for (auto *op : outsidePreds) {
                if (predBB == op) { isOutside = true; break; }
            }
            if (!isOutside) continue;

            // 暂存原入值后删除旧入边，稍后在 preheader 中构造汇合值。
            Value *val = phi->get_operand(i);
            outsideVals.push_back(val);
            outsideBBs.push_back(predBB);
            phi->remove_operands(i, i + 1);
        }

        if (outsideVals.empty()) continue;

        // 若各环外前驱提供不同值，先在 preheader 新建 PHI 合并；header
        // 只能保留一条来自新 preheader 的 incoming，不能任取某个原值。
        Value *preheaderVal = outsideVals[0];
        for (size_t j = 1; j < outsideVals.size(); j++) {
            if (outsideVals[j] == outsideVals[0])
                continue;

            auto *preheaderPhi = PhiInst::create_phi(phi->type_, preheader);
            for (size_t k = 0; k < outsideVals.size(); k++)
                preheaderPhi->addIncoming(outsideVals[k], outsideBBs[k]);
            preheader->add_instruction_before_terminator(preheaderPhi);
            preheaderVal = preheaderPhi;
            break;
        }

        phi->addIncoming(preheaderVal, preheader);
    }

    return true;
}

/**
 * @brief 原地执行 insertBackedgeBlock 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool insertBackedgeBlock(Loop *loop, Function *func) {
    BasicBlock *header = loop->header;
    if (!header) return false;
    if (loop->latches.size() <= 1) return false;

    std::vector<BasicBlock *> latches = loop->latches;
    std::string backedgeName = header->name_ + ".backedge";
    auto *backedge = new BasicBlock(func->parent_, backedgeName, func);
    placeBlockAfter(func, backedge, header);
    new BranchInst(header, backedge);

    for (auto *latch : latches) {
        replaceBranchTarget(latch, header, backedge);
        latch->remove_succ_basic_block(header);
        latch->add_succ_basic_block(backedge);
        header->remove_pre_basic_block(latch);
        backedge->add_pre_basic_block(latch);
    }

    for (auto *instr : header->instr_list_) {
        if (!instr->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instr);

        std::vector<Value *> latchVals;
        std::vector<BasicBlock *> latchBBs;
        for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
            auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!predBB) continue;
            if (std::find(latches.begin(), latches.end(), predBB) == latches.end())
                continue;

            latchVals.push_back(phi->get_operand(i));
            latchBBs.push_back(predBB);
            phi->remove_operands(i, i + 1);
        }

        if (latchVals.empty()) continue;

        auto *backedgePhi = PhiInst::create_phi(phi->type_, backedge);
        for (size_t i = 0; i < latchVals.size(); i++)
            backedgePhi->addIncoming(latchVals[i], latchBBs[i]);
        backedge->add_instruction_before_terminator(backedgePhi);
        phi->addIncoming(backedgePhi, backedge);
    }

    return true;
}

// Dedicated exits（LLVM LoopSimplify 第三项保证）：每个 exit 块的前驱
// 必须全在循环内。exit 同时被循环外路径汇入时，把循环内的出口边拆到
// 专用块 <exit>.loopexit，循环外前驱保持指向原 exit。
// 这是 LCSSA 的前置：exit phi 必须能唯一归属于一个循环。
/**
 * @brief 原地执行 insertDedicatedExits 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool insertDedicatedExits(Loop *loop, Function *func) {
    bool changed = false;

    for (auto *exit : loop->exits) {
        std::vector<BasicBlock *> inPreds, outPreds;
        for (auto *pred : exit->pre_bbs_) {
            if (loop->isInLoop(pred))
                inPreds.push_back(pred);
            else
                outPreds.push_back(pred);
        }
        // 已经 dedicated（无外部前驱），或 exits 缓存过期（无内部前驱）
        if (outPreds.empty() || inPreds.empty())
            continue;

        auto *dedicated = new BasicBlock(func->parent_,
                                         exit->name_ + ".loopexit", func);
        placeBlockBefore(func, dedicated, exit);
        new BranchInst(exit, dedicated);

        for (auto *pred : inPreds) {
            replaceBranchTarget(pred, exit, dedicated);
            pred->remove_succ_basic_block(exit);
            pred->add_succ_basic_block(dedicated);
            exit->remove_pre_basic_block(pred);
            dedicated->add_pre_basic_block(pred);
        }

        // exit 的 phi：把来自循环内前驱的入边对收拢为一条来自 dedicated
        // 的入边；多值时在 dedicated 内建 phi 汇合。
        for (auto *instr : exit->instr_list_) {
            if (!instr->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(instr);

            std::vector<Value *> vals;
            std::vector<BasicBlock *> bbs;
            for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
                auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
                if (!predBB) continue;
                if (std::find(inPreds.begin(), inPreds.end(), predBB) ==
                    inPreds.end())
                    continue;
                vals.push_back(phi->get_operand(i));
                bbs.push_back(predBB);
                phi->remove_operands(i, i + 1);
            }
            if (vals.empty()) continue;

            Value *merged = vals[0];
            for (size_t j = 1; j < vals.size(); j++) {
                if (vals[j] == vals[0]) continue;
                auto *dPhi = PhiInst::create_phi(phi->type_, dedicated);
                for (size_t k = 0; k < vals.size(); k++)
                    dPhi->addIncoming(vals[k], bbs[k]);
                dedicated->add_instruction_before_terminator(dPhi);
                merged = dPhi;
                break;
            }
            phi->addIncoming(merged, dedicated);
        }

        changed = true;
    }

    return changed;
}

} // namespace
