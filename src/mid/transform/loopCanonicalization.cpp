#include "../../include/mid/transform/loopCanonicalization.hpp"

#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <vector>

namespace {

bool insertPreheader(Loop *loop, Function *func);
bool insertBackedgeBlock(Loop *loop, Function *func);
bool insertDedicatedExits(Loop *loop, Function *func);

} // namespace

bool canonicalizeLoopForm(Function *func) {
    if (func->basic_blocks_.empty()) return false;

    bool changed = false;
    bool progress = true;
    while (progress) {
        progress = false;

        LoopInfo LI;
        LI.analyze(func);

        // Process innermost loops first so that outer-loop canonicalization
        // sees loop blocks created for children after the next analysis round.
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
static bool isExistingPreheader(BasicBlock *bb, BasicBlock *header) {
    if (bb->succ_bbs_.size() != 1 || bb->succ_bbs_[0] != header)
        return false;
    auto *term = bb->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 1)
        return false;
    if (term->get_operand(0) != header)
        return false;
    return true;
}

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

static void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldTarget,
                                BasicBlock *newTarget) {
    auto *term = pred->get_terminator();
    if (!term || !term->is_br())
        return;

    for (unsigned i = 0; i < term->num_ops_; i++) {
        if (term->get_operand(i) == oldTarget)
            term->set_operand(i, newTarget);
    }
}

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

    // ── 2. Check if a preheader already exists ─────────────────────────
    // A valid preheader is already a dedicated single-successor predecessor.
    if (outsidePreds.size() == 1 && isExistingPreheader(outsidePreds[0], header))
        return false;

    // ── 3. Check if we need a preheader at all ─────────────────────────
    // A unique outside predecessor is not necessarily a preheader: it may
    // still have another successor. isExistingPreheader above is the
    // authoritative test, so every remaining outside edge must be split.

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

        // Redirect the terminator operand that points to header
        replaceBranchTarget(pred, header, preheader);

        // Add new CFG edge pred→preheader
        pred->add_succ_basic_block(preheader);
        preheader->add_pre_basic_block(pred);
    }

    // ── 6. Fix phi nodes in the header ─────────────────────────────────
    // For each phi, replace incoming edges from outside predecessors with
    // a single edge from the new preheader.
    for (auto *instr : header->instr_list_) {
        if (!instr->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instr);

        // Collect values coming from outside predecessors.
        // We process in reverse to safely remove operand pairs.
        std::vector<Value *>    outsideVals;
        std::vector<BasicBlock *> outsideBBs; // for dedup check

        for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
            // Check if this BB operand is one of our outside predecessors
            auto *predBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!predBB) continue;

            bool isOutside = false;
            for (auto *op : outsidePreds) {
                if (predBB == op) { isOutside = true; break; }
            }
            if (!isOutside) continue;

            // Record the value and remove this pair
            Value *val = phi->get_operand(i);
            outsideVals.push_back(val);
            outsideBBs.push_back(predBB);
            phi->remove_operands(i, i + 1);
        }

        if (outsideVals.empty()) continue;

        // Determine if all outside predecessors contributed the same value.
        // If they differ, the new preheader must merge them with its own phi;
        // the header phi may only have one incoming edge from preheader.
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
        for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
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
            for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
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
