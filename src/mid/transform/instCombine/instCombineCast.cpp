#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitCast — ZExt / SItoFP / FPtoSI / Clz / BitCast
//
// Capabilities:
//   - Identity: zext/bitcast of same type → operand
//   - Constant fold: zext i1/iN, sitofp, fptosi, clz (32 for zero)
// ═══════════════════════════════════════════════════════════════════════

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
            return make_const_int(ty, ci->value_);
        case Instruction::SItoFP:
            return make_const_float(ty, static_cast<float>(ci->value_));
        case Instruction::Clz: {
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

// ═══════════════════════════════════════════════════════════════════════
// visitPhi — PHI collapse
//
// Capabilities:
//   - If every non-self incoming value is the same Value, replace phi with it
//     (constants and identical SSA values; self-edges ignored)
// ═══════════════════════════════════════════════════════════════════════

Value* visitPhi(PhiInst *inst) {
    Value *common = nullptr;
    for (unsigned i = 0; i < inst->num_ops_; i += 2) {
        Value *v = inst->get_operand(i);
        if (v == inst) continue;
        if (!common) common = v;
        else if (common != v) return nullptr;
    }
    return common;
}
