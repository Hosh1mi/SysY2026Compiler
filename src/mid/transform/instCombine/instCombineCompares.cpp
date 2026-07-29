#include "instCombineInternal.hpp"

#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════
// foldICmpAddSub — fold add/sub with constant into icmp
//
// Capabilities:
//   - Swap so add/sub±C sits on LHS
//   - Constant RHS: icmp pred (x±C1), C2 → icmp pred x, C2∓C1
//     (only when folded constant fits in i32; overflow would flip signed cmp)
//   - ±1 forms: sle(x+1,y)→slt(x,y), sgt(x+1,y)→sge(x,y), and duals for sub;
//     same for unsigned predicates
// ═══════════════════════════════════════════════════════════════════════

Value* foldICmpAddSub(ICmpInst *inst) {
    Value *lhs = inst->get_operand(0);
    Value *rhs = inst->get_operand(1);
    ICmpInst::ICmpOp pred = inst->icmp_op_;
    BasicBlock *bb = inst->parent_;

    auto *rhs_inst = dynamic_cast<Instruction*>(rhs);
    if (rhs_inst && (rhs_inst->is_add() || rhs_inst->is_sub()) &&
        as_const_int(rhs_inst->get_operand(1))) {
        pred = getSwappedPredicate(pred);
        std::swap(lhs, rhs);
    }

    auto *lhs_inst = dynamic_cast<Instruction*>(lhs);
    if (!lhs_inst || (!lhs_inst->is_add() && !lhs_inst->is_sub()))
        return nullptr;

    auto *c = as_const_int(lhs_inst->get_operand(1));
    if (!c) return nullptr;

    Value *x = lhs_inst->get_operand(0);

    auto *c_rhs = as_const_int(rhs);
    if (c_rhs) {
        long long new_c = lhs_inst->is_add()
            ? (long long)c_rhs->value_ - (long long)c->value_
            : (long long)c_rhs->value_ + (long long)c->value_;
        if (new_c >= INT32_MIN && new_c <= INT32_MAX) {
            auto *new_icmp = new ICmpInst(pred, x,
                make_const_int(x->type_, (int)new_c), bb, true);
            bb->add_instruction_before_inst(new_icmp, inst);
            return new_icmp;
        }
    }

    bool is_effectively_add1 = (lhs_inst->is_add() && c->value_ == 1)
                            || (lhs_inst->is_sub() && c->value_ == -1);
    bool is_effectively_sub1 = (lhs_inst->is_sub() && c->value_ == 1)
                            || (lhs_inst->is_add() && c->value_ == -1);

    ICmpInst::ICmpOp new_pred;
    bool valid = false;

    if (is_effectively_add1) {
        switch (pred) {
            case ICmpInst::ICMP_SLE: new_pred = ICmpInst::ICMP_SLT; valid = true; break;
            case ICmpInst::ICMP_SGT: new_pred = ICmpInst::ICMP_SGE; valid = true; break;
            case ICmpInst::ICMP_ULE: new_pred = ICmpInst::ICMP_ULT; valid = true; break;
            case ICmpInst::ICMP_UGT: new_pred = ICmpInst::ICMP_UGE; valid = true; break;
            default: break;
        }
    } else if (is_effectively_sub1) {
        switch (pred) {
            case ICmpInst::ICMP_SLT: new_pred = ICmpInst::ICMP_SLE; valid = true; break;
            case ICmpInst::ICMP_SGE: new_pred = ICmpInst::ICMP_SGT; valid = true; break;
            case ICmpInst::ICMP_ULT: new_pred = ICmpInst::ICMP_ULE; valid = true; break;
            case ICmpInst::ICMP_UGE: new_pred = ICmpInst::ICMP_UGT; valid = true; break;
            default: break;
        }
    }

    if (valid) {
        auto *new_icmp = new ICmpInst(new_pred, x, rhs, bb, true);
        bb->add_instruction_before_inst(new_icmp, inst);
        return new_icmp;
    }

    return nullptr;
}
