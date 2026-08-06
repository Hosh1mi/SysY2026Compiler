// LoopPeel — peel the first iteration of a canonical 2-BB loop while keeping
// both the header exit and the latch side-exit.  Trivial-phi / icmp folding /
// block deletion are left to the subsequent DCE / InstCombine / CFGSimplify
// cleanup in the driver pipeline.

#include "../../../include/mid/opt/loopPeel.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/transform/loopCloneUtils.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using loop_clone::addRemappedIncomingForClonedEdge;
using loop_clone::cloneInstruction;
using loop_clone::incomingFrom;
using loop_clone::remapValueOrInvariant;
using loop_clone::removeIncomingFrom;

void replaceTerminatorTarget(Instruction *term, unsigned opIdx,
                             BasicBlock *oldTarget, BasicBlock *newTarget) {
    if (term->get_operand(opIdx) != oldTarget)
        return;
    term->get_operand(opIdx)->remove_use(term->use_pos_[opIdx]);
    term->operands_[opIdx] = newTarget;
    term->use_pos_[opIdx] = newTarget->add_use(term, opIdx);
}

bool isSupportedCloneInst(Instruction *inst) {
    if (inst->is_phi() || inst->isTerminator() || inst->is_alloca())
        return false;
    return dynamic_cast<BinaryInst *>(inst) ||
           dynamic_cast<UnaryInst *>(inst) ||
           dynamic_cast<ICmpInst *>(inst) ||
           dynamic_cast<FCmpInst *>(inst) ||
           dynamic_cast<SelectInst *>(inst) ||
           dynamic_cast<GetElementPtrInst *>(inst) ||
           dynamic_cast<LoadInst *>(inst) ||
           dynamic_cast<StoreInst *>(inst) ||
           dynamic_cast<ZextInst *>(inst) ||
           dynamic_cast<FpToSiInst *>(inst) ||
           dynamic_cast<SiToFpInst *>(inst) ||
           dynamic_cast<Bitcast *>(inst) ||
           dynamic_cast<CallInst *>(inst);
}

bool hasOnlyLCSSAOutsideUses(Loop &loop) {
    auto isExitBlock = [&](BasicBlock *bb) {
        for (auto *exit : loop.exits) {
            if (exit == bb)
                return true;
        }
        return false;
    };

    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            for (auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (!user || !user->parent_)
                    continue;
                if (loop.isInLoop(user->parent_))
                    continue;
                if (!user->is_phi() || !isExitBlock(user->parent_))
                    return false;
            }
        }
    }
    return true;
}

bool willCreateTrivialHeaderPhi(
    Loop &loop, BasicBlock *preheader, BasicBlock *latch,
    const std::vector<PhiInst *> &headerPhis,
    const std::unordered_set<BasicBlock *> &loopBlocks) {
    std::unordered_map<Value *, Value *> seed;
    for (auto *phi : headerPhis) {
        Value *init = incomingFrom(phi, preheader);
        if (!init)
            return false;
        seed[phi] = init;
    }

    for (auto *phi : headerPhis) {
        Value *next = incomingFrom(phi, latch);
        if (!next)
            continue;
        // Profit preview: latch values defined inside the loop become new
        // cloned SSA and cannot match `next`.  Loop-invariant latch values
        // remap to themselves and make the remaining header phi trivial.
        Value *peeled = remapValueOrInvariant(next, seed, loopBlocks);
        if (peeled && peeled == next)
            return true;
    }
    return false;
}

bool canRemap(Value *value,
              const std::unordered_map<Value *, Value *> &valueMap,
              const std::unordered_set<BasicBlock *> &loopBlocks) {
    return remapValueOrInvariant(value, valueMap, loopBlocks) != nullptr;
}

bool preflightClone(Loop &loop, BasicBlock *header, BasicBlock *latch,
                    BasicBlock *headerExit, BasicBlock *latchExit,
                    const std::vector<PhiInst *> &headerPhis,
                    const std::unordered_set<BasicBlock *> &loopBlocks) {
    std::unordered_map<Value *, Value *> valueMap;
    for (auto *phi : headerPhis)
        valueMap[phi] = incomingFrom(phi, loop.preheader);

    for (auto *bb : {header, latch}) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            for (unsigned i = 0; i < inst->num_ops_; ++i)
                if (!canRemap(inst->get_operand(i), valueMap, loopBlocks))
                    return false;
            // Model the value produced by each instruction so later operands
            // can be checked against the same SSA order as the clone.
            valueMap.emplace(inst, inst);
        }
    }

    if (!canRemap(header->get_terminator()->get_operand(0), valueMap,
                  loopBlocks) ||
        !canRemap(latch->get_terminator()->get_operand(0), valueMap,
                  loopBlocks))
        return false;
    for (auto *exit : {headerExit, latchExit}) {
        for (auto *inst : exit->instr_list_) {
            if (!inst->is_phi())
                break;
            auto *phi = static_cast<PhiInst *>(inst);
            BasicBlock *pred = exit == headerExit ? header : latch;
            Value *incoming = incomingFrom(phi, pred);
            if (incoming && !canRemap(incoming, valueMap, loopBlocks))
                return false;
        }
    }
    return true;
}

bool tryPeelLoop(Loop &loop, Function *func, Module *module) {
    if (loop.blocks.size() != 2)
        return false;
    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *preheader = loop.preheader;
    if (!header || !latch || !preheader || latch == header)
        return false;

    auto *headerBr = dynamic_cast<BranchInst *>(header->get_terminator());
    auto *latchBr = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!headerBr || headerBr->num_ops_ != 3)
        return false;
    if (!latchBr || latchBr->num_ops_ != 3)
        return false;

    auto *headerTrue = dynamic_cast<BasicBlock *>(headerBr->get_operand(1));
    auto *headerFalse = dynamic_cast<BasicBlock *>(headerBr->get_operand(2));
    if (!headerTrue || !headerFalse)
        return false;

    BasicBlock *headerExit = nullptr;
    if (headerTrue == latch && !loop.isInLoop(headerFalse))
        headerExit = headerFalse;
    else if (headerFalse == latch && !loop.isInLoop(headerTrue))
        headerExit = headerTrue;
    else
        return false;

    auto *latchTrue = dynamic_cast<BasicBlock *>(latchBr->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchBr->get_operand(2));
    if (!latchTrue || !latchFalse)
        return false;

    BasicBlock *latchExit = nullptr;
    if (latchTrue == header && !loop.isInLoop(latchFalse))
        latchExit = latchFalse;
    else if (latchFalse == header && !loop.isInLoop(latchTrue))
        latchExit = latchTrue;
    else
        return false;

    std::vector<PhiInst *> headerPhis;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (!incomingFrom(phi, preheader) || !incomingFrom(phi, latch))
            return false;
        // Exactly those two incomings.
        if (phi->num_ops_ != 4)
            return false;
        headerPhis.push_back(phi);
    }
    if (headerPhis.empty())
        return false;

    std::unordered_set<BasicBlock *> loopBlocks(loop.blocks.begin(),
                                                loop.blocks.end());

    for (auto *bb : {header, latch}) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator())
                continue;
            if (!isSupportedCloneInst(inst))
                return false;
        }
    }

    if (!hasOnlyLCSSAOutsideUses(loop))
        return false;

    if (!willCreateTrivialHeaderPhi(loop, preheader, latch, headerPhis,
                                    loopBlocks))
        return false;

    if (!preflightClone(loop, header, latch, headerExit, latchExit,
                        headerPhis, loopBlocks))
        return false;

    // ── Transform ────────────────────────────────────────────────────
    auto *peeledHeader = new BasicBlock(module, "peeled.header", func);
    auto *peeledLatch = new BasicBlock(module, "peeled.latch", func);

    std::unordered_map<Value *, Value *> valueMap;
    for (auto *phi : headerPhis)
        valueMap[phi] = incomingFrom(phi, preheader);

    // Clone header body (non-phi, non-term).
    for (auto *inst : header->instr_list_) {
        if (inst->is_phi() || inst->isTerminator())
            continue;
        if (!cloneInstruction(inst, peeledHeader, valueMap, loopBlocks))
            return false;
    }

    // Clone header terminator into peeled.header with remapped successors.
    {
        Value *cond = remapValueOrInvariant(headerBr->get_operand(0), valueMap,
                                            loopBlocks);
        if (!cond)
            return false;
        auto mapHeaderSucc = [&](BasicBlock *succ) -> BasicBlock * {
            if (succ == latch)
                return peeledLatch;
            if (succ == headerExit)
                return headerExit;
            return nullptr;
        };
        BasicBlock *tSucc = mapHeaderSucc(headerTrue);
        BasicBlock *fSucc = mapHeaderSucc(headerFalse);
        if (!tSucc || !fSucc)
            return false;
        new BranchInst(cond, tSucc, fSucc, peeledHeader);
    }

    // Clone latch body.
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator())
            continue;
        if (inst->is_phi())
            return false;
        if (!cloneInstruction(inst, peeledLatch, valueMap, loopBlocks))
            return false;
    }

    // Clone latch terminator: backedge still targets original header;
    // side exit still targets original latchExit.
    {
        Value *cond = remapValueOrInvariant(latchBr->get_operand(0), valueMap,
                                            loopBlocks);
        if (!cond)
            return false;
        BasicBlock *tSucc = latchTrue == header ? header : latchExit;
        BasicBlock *fSucc = latchFalse == header ? header : latchExit;
        new BranchInst(cond, tSucc, fSucc, peeledLatch);
    }

    // Patch exit phis for cloned edges before rewriting header phis /
    // preheader, while original edges still exist for lookup.
    for (auto *inst : headerExit->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (!incomingFrom(phi, header))
            continue;
        if (!addRemappedIncomingForClonedEdge(phi, header, peeledHeader,
                                              valueMap, loopBlocks))
            return false;
    }
    for (auto *inst : latchExit->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (!incomingFrom(phi, latch))
            continue;
        if (!addRemappedIncomingForClonedEdge(phi, latch, peeledLatch,
                                              valueMap, loopBlocks))
            return false;
    }

    // Rewrite header phis: replace preheader incoming with peeled.latch.
    for (auto *phi : headerPhis) {
        Value *latchIncoming = incomingFrom(phi, latch);
        Value *peeledIncoming =
            remapValueOrInvariant(latchIncoming, valueMap, loopBlocks);
        if (!peeledIncoming)
            return false;
        removeIncomingFrom(phi, preheader);
        phi->addIncoming(peeledIncoming, peeledLatch);
    }

    // Redirect preheader → peeled.header.
    auto *preBr = preheader->get_terminator();
    if (!preBr || !preBr->is_br())
        return false;
    for (unsigned i = 0; i < preBr->num_ops_; ++i) {
        if (preBr->get_operand(i) == header) {
            replaceTerminatorTarget(preBr, i, header, peeledHeader);
            preheader->remove_succ_basic_block(header);
            header->remove_pre_basic_block(preheader);
            preheader->add_succ_basic_block(peeledHeader);
            peeledHeader->add_pre_basic_block(preheader);
            break;
        }
    }

    if (std::getenv("DEBUG_LOOP_PEEL"))
        std::cerr << "[LoopPeel] func=" << func->name_
                  << " header=" << header->name_
                  << " peeled\n";

    func->set_instr_name();
    return true;
}

bool runOnFunction(Function *func, Module *module) {
    bool changed = false;
    LoopInfo LI;
    LI.analyze(func);

    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    // Innermost first; each loop is considered once to keep this pass
    // bounded even when peeling leaves another structurally similar loop.
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    for (auto *loop : loops) {
        if (tryPeelLoop(*loop, func, module))
            changed = true;
    }
    return changed;
}

} // namespace

void LoopPeel::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, module);
    }
}

PreservedAnalyses LoopPeel::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, module);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
