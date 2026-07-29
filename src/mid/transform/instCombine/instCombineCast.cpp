#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// Constant-folding for type-conversion / built-in unary instructions and
// phi nodes.  These cases were migrated here when the standalone
// ConstantFold pass was folded into InstCombine: binary/icmp/fcmp folding
// already lived in the per-opcode visitors, leaving only the unary casts,
// the Clz built-in, and constant phi collapse to bring over.
// ═══════════════════════════════════════════════════════════════════════

// visitCast  —  fold ZExt / SItoFP / FPtoSI / Clz on a constant operand.
// Takes Instruction* because the cast nodes (ZextInst / FpToSiInst /
// SiToFpInst) derive directly from Instruction, not UnaryInst.
Value* visitCast(Instruction *inst) {
    Value *op = inst->get_operand(0);
    Type *ty = inst->type_;

    if ((inst->op_id_ == Instruction::ZExt || inst->op_id_ == Instruction::BitCast) &&
        op->type_ == ty) {
        return op;
    }

    if (auto *ci = as_const_int(op)) {
        switch (inst->op_id_) {
        case Instruction::ZExt:
            // i1 -> i32 (and any widening of a constant) preserves the value.
            return make_const_int(ty, ci->value_);
        case Instruction::SItoFP:
            return make_const_float(ty, static_cast<float>(ci->value_));
        case Instruction::Clz: {
            // Count leading zero bits; 32 for a zero input.  Computed on an
            // unsigned copy so the shift never touches the sign bit.
            uint32_t v = static_cast<uint32_t>(ci->value_);
            int r = 0;
            while (r < 32 && (v & (1u << 31)) == 0) { v <<= 1; ++r; }
            return make_const_int(ty, r);
        }
        default:
            break;
        }
    }

    if (auto *cf = as_const_float(op)) {
        if (inst->op_id_ == Instruction::FPtoSI)
            return make_const_int(ty, static_cast<int>(cf->value_));
    }

    return nullptr;
}

// visitPhi  —  collapse a phi whose every (non-self) incoming value is the
// same constant.  Self-referential edges are ignored: they cannot change a
// value that is otherwise constant on all entry paths.
Value* visitPhi(PhiInst *inst) {
    Value *common = nullptr;
    for (unsigned i = 0; i < inst->num_ops_; i += 2) {
        Value *v = inst->get_operand(i);
        if (v == inst) continue;          // skip self-reference
        if (!common) common = v;
        else if (common != v) return nullptr;
    }
    if (dynamic_cast<Constant *>(common))
        return common;
    return nullptr;
}
