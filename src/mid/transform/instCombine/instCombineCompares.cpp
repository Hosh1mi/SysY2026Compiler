#include "instCombineInternal.hpp"

#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════
// foldICmpAddSub  —  fold add/sub with constant into icmp
//
// Handles patterns like:
//   icmp sle (add x, 1),  y  →  icmp slt x, y   (eliminate add)
//   icmp eq  (add x, 5), 10  →  icmp eq  x, 5   (fold constant)
// ═══════════════════════════════════════════════════════════════════════

Value* foldICmpAddSub(ICmpInst *inst) {
    Value *lhs = inst->get_operand(0);
    Value *rhs = inst->get_operand(1);
    ICmpInst::ICmpOp pred = inst->icmp_op_;
    BasicBlock *bb = inst->parent_;

    // ── If add/sub is on RHS, swap to put it on LHS ──────────────────
    auto *rhs_inst = dynamic_cast<Instruction*>(rhs);
    if (rhs_inst && (rhs_inst->is_add() || rhs_inst->is_sub()) &&
        as_const_int(rhs_inst->get_operand(1))) {
        pred = getSwappedPredicate(pred);
        std::swap(lhs, rhs);
    }

    // ── Now look for add/sub on LHS ──────────────────────────────────
    auto *lhs_inst = dynamic_cast<Instruction*>(lhs);
    if (!lhs_inst || (!lhs_inst->is_add() && !lhs_inst->is_sub()))
        return nullptr;

    auto *c = as_const_int(lhs_inst->get_operand(1));
    if (!c) return nullptr;

    Value *x = lhs_inst->get_operand(0);

    // ── Category B: constant on RHS → fold into comparison ───────────
    //    icmp pred (add x, C1), C2  →  icmp pred x, C2 - C1
    //    icmp pred (sub x, C1), C2  →  icmp pred x, C2 + C1
    auto *c_rhs = as_const_int(rhs);
    if (c_rhs) {
        // Compute in 64-bit: the rewrite (x±C1) pred C2 → x pred (C2∓C1) is
        // only sound when the folded constant fits in i32. If C2∓C1 overflows
        // (e.g. C1=C2=2^30 → 2^31 wraps to INT_MIN), the signed comparison
        // would silently flip, so bail out and leave the icmp untouched.
        long long new_c = lhs_inst->is_add()
            ? (long long)c_rhs->value_ - (long long)c->value_  // add: → C2-C1
            : (long long)c_rhs->value_ + (long long)c->value_; // sub: → C2+C1
        if (new_c >= INT32_MIN && new_c <= INT32_MAX) {
            auto *new_icmp = new ICmpInst(pred, x,
                make_const_int(x->type_, (int)new_c), bb, true);
            bb->add_instruction_before_inst(new_icmp, inst);
            return new_icmp;
        }
    }

    // ── Category A: add/sub by ±1 → reduce predicate, eliminate inst ─
    //    add x, 1  patterns:  sle→slt,  sgt→sge
    //    sub x, 1  patterns:  slt→sle,  sge→sgt
    //    (Also handles add x, -1 ≡ sub x, 1  and  sub x, -1 ≡ add x, 1)

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
