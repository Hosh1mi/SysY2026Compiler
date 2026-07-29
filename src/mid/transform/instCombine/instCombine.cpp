#include "../../../include/mid/opt/instCombine.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "instCombineInternal.hpp"

#include <iostream>
#include <unordered_set>
#include <vector>

thread_local RangeAnalysis *gInstCombineRangeAnalysis = nullptr;

// ── trySinkInstruction ──────────────────────────────────────────────
// If inst has exactly one user and that user is in a different block,
// sink (move) inst to just before the user.  This is a just-in-time
// companion to the worklist loop: when a replacement removes one use
// of an operand, we check whether the operand can now be sunk.
//
// Safe when: user's block has inst's block as its sole predecessor.
// (Simpler than full dominator analysis and correct for the common
//  "computed before branch, used only in one successor" case.)

static bool isSinkableOp(Instruction::OpID op) {
    switch (op) {
        case Instruction::Add: case Instruction::Sub: case Instruction::Mul:
        case Instruction::SDiv: case Instruction::SRem:
        case Instruction::Shl: case Instruction::LShr: case Instruction::AShr:
        case Instruction::And: case Instruction::Or: case Instruction::Xor:
        case Instruction::FAdd: case Instruction::FSub:
        case Instruction::FMul: case Instruction::FDiv:
        case Instruction::FNeg:
        case Instruction::ZExt: case Instruction::FPtoSI:
        case Instruction::SItoFP: case Instruction::BitCast:
            return true;
        default:
            return false;  // loads, stores, calls — never sink
    }
}

static void trySinkInstruction(Instruction *inst) {
    if (inst->use_list_.size() != 1) return;

    auto *user = dynamic_cast<Instruction*>((*inst->use_list_.begin()).val_);
    if (!user) return;

    BasicBlock *user_bb = user->parent_;
    BasicBlock *inst_bb = inst->parent_;

    // Already in the same block — nothing to do
    if (user_bb == inst_bb) return;

    // Don't sink into PHI or terminator
    if (user->isTerminator()) return;
    if (user->op_id_ == Instruction::PHI) return;

    // Don't sink terminators or non-sinkable instructions
    if (inst->isTerminator() || !isSinkableOp(inst->op_id_)) return;

    // Safety: user's block must have inst's block as its only predecessor.
    // This guarantees inst dominates user — every path to user must have
    // already executed inst.
    if (user_bb->pre_bbs_.size() != 1 || user_bb->pre_bbs_[0] != inst_bb)
        return;

    // Perform the sink
    inst_bb->remove_instr(inst);
    user_bb->add_instruction_before_inst(inst, user);
}

void InstCombine::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func, nullptr);
    }
}

PreservedAnalyses InstCombine::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, &AM);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool InstCombine::runOnFunction(Function *func, AnalysisManager *AM) {
    auto countInstructions = [&]() -> size_t {
        size_t total = 0;
        for (auto *bb : func->basic_blocks_)
            total += bb->instr_list_.size();
        return total;
    };

    auto enqueueIfAlive = [](Instruction *inst,
                             std::vector<Instruction *> &worklist,
                             std::unordered_set<Instruction *> &inWorklist) {
        if (!inst || !inst->parent_) return;
        if (!inWorklist.insert(inst).second) return;
        worklist.push_back(inst);
    };

    const size_t initialInstrCount = countInstructions();
    // RangeAnalysis construction computes whole-function post-dominance and
    // control dependence.  InstCombine invalidates that analysis after every
    // replacement because its predicate facts may reference the replaced
    // instruction.  Rebuilding it for every local fold is prohibitively
    // expensive on large generated functions.  Keep the range-assisted
    // combines for normal functions, while large functions use the always-safe
    // local combines.
    constexpr size_t kRangeAnalysisInstructionLimit = 1024;
    const bool useRangeAnalysis =
        AM && initialInstrCount <= kRangeAnalysisInstructionLimit;
    const size_t processBudget =
        std::max<size_t>(20000, initialInstrCount * 32);
    const size_t rewriteBudget =
        std::max<size_t>(4000, initialInstrCount * 16);
    size_t processedCount = 0;
    size_t rewrittenCount = 0;
    bool budgetHit = false;
    bool changed = false;

    RangeAnalysis *savedRangeAnalysis = gInstCombineRangeAnalysis;
    gInstCombineRangeAnalysis =
        useRangeAnalysis ? &AM->getRangeAnalysis(func) : nullptr;

    std::vector<Instruction*> worklist;
    std::unordered_set<Instruction *> inWorklist;
    for (auto *bb : func->basic_blocks_)
        for (auto *inst : bb->instr_list_)
            enqueueIfAlive(inst, worklist, inWorklist);

    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();
        inWorklist.erase(inst);

        if (!inst->parent_) continue;
        if (inst->isTerminator()) continue;
        if (rewrittenCount >= rewriteBudget) {
            budgetHit = true;
            break;
        }
        if (++processedCount > processBudget) {
            budgetHit = true;
            break;
        }

        Value *replacement = nullptr;

        switch (inst->op_id_) {
        // AddSub
        case Instruction::Add:
            replacement = visitAdd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Sub:
            replacement = visitSub(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FAdd:
            replacement = visitFAdd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FSub:
            replacement = visitFSub(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FNeg:
            replacement = visitFNeg(static_cast<UnaryInst*>(inst));
            break;
        // MulDivRem
        case Instruction::Mul:
            replacement = visitMul(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::SDiv:
            replacement = visitSDiv(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::SRem:
            replacement = visitSRem(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FMul:
            replacement = visitFMul(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FDiv:
            replacement = visitFDiv(static_cast<BinaryInst*>(inst));
            break;
        // Shifts
        case Instruction::Shl:
            replacement = visitShl(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::LShr:
            replacement = visitLShr(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::AShr:
            replacement = visitAShr(static_cast<BinaryInst*>(inst));
            break;
        // Bitwise
        case Instruction::And:
            replacement = visitAnd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Or:
            replacement = visitOr(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Xor:
            replacement = visitXor(static_cast<BinaryInst*>(inst));
            break;
        // CmpSelect
        case Instruction::ICmp:
            replacement = visitICmp(static_cast<ICmpInst*>(inst));
            break;
        case Instruction::FCmp:
            replacement = visitFCmp(static_cast<FCmpInst*>(inst));
            break;
        case Instruction::Select:
            replacement = visitSelect(static_cast<SelectInst*>(inst));
            break;
        // Cast / Phi  (constant folding migrated from ConstantFold)
        case Instruction::ZExt:
        case Instruction::SItoFP:
        case Instruction::FPtoSI:
        case Instruction::BitCast:
        case Instruction::Clz:
            replacement = visitCast(inst);
            break;
        case Instruction::PHI:
            replacement = visitPhi(static_cast<PhiInst*>(inst));
            break;
        default:
            break;
        }

        auto *replacementInst = dynamic_cast<Instruction *>(replacement);
        if (replacementInst && replacementInst->parent_ &&
            sameInstructionShape(inst, replacementInst)) {
            replacementInst->parent_->delete_instr(replacementInst);
            replacement = nullptr;
            replacementInst = nullptr;
        }

        if (replacement) {
            // Bound every successful rewrite, including replacement by an
            // existing value or a constant.  Counting only newly-created
            // instructions leaves constant-rewrite cycles constrained solely
            // by the much larger worklist budget.
            ++rewrittenCount;
            changed = true;
            std::vector<Instruction*> users;
            for (auto &use : inst->use_list_) {
                if (auto *user_inst = dynamic_cast<Instruction*>(use.val_))
                    users.push_back(user_inst);
            }

            std::vector<Instruction*> operands;
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                if (auto *op = dynamic_cast<Instruction*>(inst->get_operand(i)))
                    operands.push_back(op);
            }

            inst->replace_all_use_with(replacement);
            inst->parent_->delete_instr(inst);
            if (useRangeAnalysis && gInstCombineRangeAnalysis) {
                // The first rewrite can invalidate predicate operands and
                // interprocedural summaries.  Drop all cached range analyses
                // once, then finish this worklist with local combines.  A
                // later InstCombine invocation may build one fresh analysis;
                // rebuilding whole-function post-dominance after every local
                // replacement is both unnecessary and a compile-time hazard.
                AM->clearRangeAnalyses();
                gInstCombineRangeAnalysis = nullptr;
            }

            for (auto *user : users)
                enqueueIfAlive(user, worklist, inWorklist);

            if (replacementInst && replacementInst->parent_) {
                enqueueIfAlive(replacementInst, worklist, inWorklist);
            }

            for (auto *op : operands) {
                trySinkInstruction(op);
                enqueueIfAlive(op, worklist, inWorklist);
            }
        }
    }

    gInstCombineRangeAnalysis = savedRangeAnalysis;

    if (budgetHit) {
        std::cerr << "[InstCombine] budget hit in @" << func->name_
                  << " processed=" << processedCount
                  << " rewritten=" << rewrittenCount
                  << " initial_insts=" << initialInstrCount << "\n";
    }
    return changed;
}
