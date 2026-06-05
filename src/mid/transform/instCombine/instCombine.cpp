#include "../../../include/mid/opt/instCombine.hpp"
#include "instCombineInternal.hpp"
#include <vector>

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
        runOnFunction(func);
    }
}

void InstCombine::runOnFunction(Function *func) {
    // Collect all instructions into the worklist
    std::vector<Instruction*> worklist;
    for (auto bb : func->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            worklist.push_back(inst);
        }
    }

    // Process until fixed point
    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();

        // Skip instructions that were deleted by an earlier iteration
        if (!inst->parent_) continue;

        // Skip terminators — they are never simplified here
        if (inst->isTerminator()) continue;

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
        case Instruction::Select:
            replacement = visitSelect(static_cast<SelectInst*>(inst));
            break;
        default:
            break;
        }

        if (replacement) {
            // Collect users *before* replace_all_use_with clears the use list
            std::vector<Instruction*> users;
            for (auto &use : inst->use_list_) {
                if (auto *user_inst = dynamic_cast<Instruction*>(use.val_)) {
                    users.push_back(user_inst);
                }
            }

            // Save operands *before* delete_instr drops use counts.
            // After the old instruction is gone, operands may become
            // single-use — we try to sink them to their remaining user.
            std::vector<Instruction*> operands;
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                if (auto *op = dynamic_cast<Instruction*>(inst->get_operand(i))) {
                    operands.push_back(op);
                }
            }

            inst->replace_all_use_with(replacement);
            inst->parent_->delete_instr(inst);

            // Revisit users — they may now be simplifiable with the new operand
            for (auto *user : users) {
                worklist.push_back(user);
            }

            // If the replacement is itself an instruction, visit it too
            if (auto *new_inst = dynamic_cast<Instruction*>(replacement)) {
                worklist.push_back(new_inst);
            }

            // Try to sink operands that just lost a use
            for (auto *op : operands) {
                trySinkInstruction(op);
                if (op->parent_) worklist.push_back(op);
            }
        }
    }
}
