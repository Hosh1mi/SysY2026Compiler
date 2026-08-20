/**
 * @file instCombineCast.cpp
 * @brief 实现整数/浮点转换、位转换与前导零计数等转换指令的局部化简。
 * @details 仅在源/目标类型与常量语义明确时折叠转换，避免把有损浮点或位宽变化误当作恒等变换。
 */

#include "instCombineInternal.hpp"

// ═══════════════════════════════════════════════════════════════════════
// visitCast — ZExt / SItoFP / FPtoSI / Clz / BitCast
//
// Capabilities:
//   - Identity: zext/bitcast of same type → operand
//   - Constant fold: zext i1/iN, sitofp, fptosi, clz (32 for zero)
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief 对 Cast 类指令执行局部规范化、常量折叠和代数化简。
 * @param inst 待分析、化简或克隆的指令。
 * @return 成功时返回对应对象指针；无法匹配或构造时可能返回 nullptr。
 */
Value* visitCast(Instruction *inst) {
    // 相同类型转换可直接旁路；跨类型转换只有常量求值器语义明确时才折叠。
    // 非常量的窄化、浮点舍入和可能越界转换均保留给后续专门分析。
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

/**
 * @brief 对 Phi 类指令执行局部规范化、常量折叠和代数化简。
 * @param inst 待分析、化简或克隆的指令。
 * @return 成功时返回对应对象指针；无法匹配或构造时可能返回 nullptr。
 */
Value* visitPhi(PhiInst *inst) {
    Value *common = nullptr;
    for (unsigned i = 0; i < inst->num_ops(); i += 2) {
        Value *v = inst->get_operand(i);
        if (v == inst) continue;
        if (!common) common = v;
        else if (common != v) return nullptr;
    }
    return common;
}
