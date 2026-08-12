#include "../../include/mid/opt/tailCallOpt.hpp"
#include "../../include/mid/opt/cfgUtils.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"

#include <vector>

static bool isFinalNonTerminator(CallInst *call, BasicBlock *block) {
    if (!call || !block)
        return false;
    Instruction *terminator = block->get_terminator();
    bool seenCall = false;
    for (Instruction *instruction : block->instr_list_) {
        if (instruction == call) {
            seenCall = true;
            continue;
        }
        if (seenCall && instruction != terminator)
            return false;
    }
    return seenCall;
}

// Pattern 2: call is final non-terminator, unconditional br to a return
// block; every use of the call is a phi in that block; non-void ret uses
// one of those phis.
static bool isPattern2TailCall(CallInst *call, BasicBlock *target,
                               BasicBlock *term_bb) {
    if (!isFinalNonTerminator(call, term_bb))
        return false;

    auto *targetTerm = target->get_terminator();
    if (!targetTerm || !targetTerm->is_ret())
        return false;

    for (auto &use : call->use_list_) {
        auto *phi = dynamic_cast<PhiInst *>(use.user_);
        if (!phi || phi->parent_ != target)
            return false;
    }

    if (targetTerm->num_ops() > 0) {
        Value *retVal = targetTerm->get_operand(0);
        bool usedByRet = false;
        for (auto &use : call->use_list_) {
            if (use.user_ == retVal) {
                usedByRet = true;
                break;
            }
        }
        if (!usedByRet)
            return false;
    }
    return true;
}

static CallInst *findTrailingCall(BasicBlock *bb) {
    Instruction *term = bb->get_terminator();
    if (!term)
        return nullptr;
    for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend();
         ++rit) {
        if (*rit == term)
            continue;
        return dynamic_cast<CallInst *>(*rit);
    }
    return nullptr;
}

// Rewrite Pattern 2 into Pattern 1 in the call block, then mark tail.
static void canonicalizePattern2(CallInst *call, BasicBlock *bb,
                                 BasicBlock *retBB) {
    Instruction *br = bb->get_terminator();

    // Drop this predecessor from every phi in the return block.
    for (auto it = retBB->instr_list_.begin(); it != retBB->instr_list_.end();) {
        auto *phi = dynamic_cast<PhiInst *>(*it);
        if (!phi)
            break;
        bool removed = false;
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            if (phi->get_operand(i + 1) == bb) {
                phi->remove_operands(static_cast<int>(i),
                                     static_cast<int>(i + 1));
                removed = true;
                break;
            }
        }
        // If the call fed this phi and no other preds remain with a value,
        // leave a possibly-empty phi for later cleanup; CFG is still valid
        // as long as remaining edges match remaining phi pairs.
        (void)removed;
        ++it;
    }

    bb->remove_succ_basic_block(retBB);
    retBB->remove_pre_basic_block(bb);
    bb->delete_instr(br);

    if (call->is_void())
        new ReturnInst(bb);
    else
        new ReturnInst(call, bb);
}

void TailCallOpt::execute(Module *module) {
    AnalysisManager unused;
    execute(module, unused);
}

PreservedAnalyses TailCallOpt::execute(Module *module, AnalysisManager &) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool TailCallOpt::runOnFunction(Function *func) {
    // Collect sites first so Pattern-2 rewrites do not invalidate iteration.
    struct Site {
        CallInst *call;
        BasicBlock *bb;
        BasicBlock *retBB; // non-null => Pattern 2
    };
    std::vector<Site> sites;

    for (auto *bb : func->basic_blocks_) {
        Instruction *term = bb->get_terminator();
        if (!term)
            continue;

        if (term->is_ret()) {
            CallInst *call = nullptr;
            if (term->num_ops() > 0) {
                call = dynamic_cast<CallInst *>(term->get_operand(0));
                if (!call || !isFinalNonTerminator(call, bb))
                    continue;
            } else {
                // void ret: trailing void call
                call = findTrailingCall(bb);
                if (!call || !call->is_void() ||
                    !isFinalNonTerminator(call, bb))
                    continue;
            }
            sites.push_back({call, bb, nullptr});
            continue;
        }

        if (term->is_br() && term->num_ops() == 1) {
            auto *target = static_cast<BasicBlock *>(term->get_operand(0));
            CallInst *call = findTrailingCall(bb);
            if (!call || !isPattern2TailCall(call, target, bb))
                continue;
            sites.push_back({call, bb, target});
        }
    }

    if (sites.empty())
        return false;

    for (auto &site : sites) {
        if (site.retBB)
            canonicalizePattern2(site.call, site.bb, site.retBB);
        site.call->set_tail(true);
    }
    removeUnreachableBlocks(func);
    return true;
}
