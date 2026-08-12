#pragma once
#include "dominanceAnalysis.hpp"

#include "../ir/ir.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace ValueFacts {

struct KnownBits {
    uint32_t zero = 0;
    uint32_t one = 0;
};

inline uint32_t widthMask(unsigned bits) {
    return bits >= 32 ? UINT32_MAX : ((1u << bits) - 1u);
}

inline unsigned integerBitWidth(Value *v) {
    if (!v || !v->type_ || v->type_->tid_ != Type::IntegerTyID)
        return 0;
    return static_cast<IntegerType *>(v->type_)->num_bits_;
}

inline unsigned countKnownTrailingZeros(uint32_t knownZero, unsigned bits) {
    unsigned count = 0;
    while (count < bits && (knownZero & (1u << count)))
        ++count;
    return count;
}

inline KnownBits computeKnownBitsImpl(Value *v, int depth) {
    const unsigned bits = integerBitWidth(v);
    if (bits == 0 || bits > 32 || depth > 8)
        return {};
    const uint32_t mask = widthMask(bits);

    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        const uint32_t value = static_cast<uint32_t>(ci->value_) & mask;
        return {static_cast<uint32_t>(~value) & mask, value};
    }

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)
        return {};

    if (inst->op_id_ == Instruction::Select) {
        KnownBits t = computeKnownBitsImpl(inst->get_operand(1), depth + 1);
        KnownBits f = computeKnownBitsImpl(inst->get_operand(2), depth + 1);
        return {t.zero & f.zero, t.one & f.one};
    }

    if (inst->op_id_ == Instruction::ZExt) {
        Value *src = inst->get_operand(0);
        const unsigned srcBits = integerBitWidth(src);
        if (srcBits == 0 || srcBits > bits)
            return {};
        KnownBits known = computeKnownBitsImpl(src, depth + 1);
        known.zero |= mask & ~widthMask(srcBits);
        return known;
    }

    if (inst->num_ops() < 2)
        return {};
    KnownBits lhs = computeKnownBitsImpl(inst->get_operand(0), depth + 1);
    KnownBits rhs = computeKnownBitsImpl(inst->get_operand(1), depth + 1);

    switch (inst->op_id_) {
    case Instruction::And:
        return {(lhs.zero | rhs.zero) & mask, (lhs.one & rhs.one) & mask};
    case Instruction::Or:
        return {(lhs.zero & rhs.zero) & mask, (lhs.one | rhs.one) & mask};
    case Instruction::Xor:
        return {((lhs.zero & rhs.zero) | (lhs.one & rhs.one)) & mask,
                ((lhs.zero & rhs.one) | (lhs.one & rhs.zero)) & mask};
    case Instruction::Add:
    case Instruction::Sub: {
        unsigned lowZeros = std::min(countKnownTrailingZeros(lhs.zero, bits),
                                     countKnownTrailingZeros(rhs.zero, bits));
        return {lowZeros == 32 ? UINT32_MAX : ((1u << lowZeros) - 1u), 0};
    }
    case Instruction::Mul: {
        unsigned lowZeros = std::min(
            bits, countKnownTrailingZeros(lhs.zero, bits) +
                      countKnownTrailingZeros(rhs.zero, bits));
        return {lowZeros == 32 ? UINT32_MAX : ((1u << lowZeros) - 1u), 0};
    }
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr: {
        auto *amount = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (!amount || amount->value_ < 0 ||
            static_cast<unsigned>(amount->value_) >= bits)
            return {};
        unsigned shift = static_cast<unsigned>(amount->value_);
        if (inst->op_id_ == Instruction::Shl) {
            uint32_t low = shift == 0 ? 0 : ((1u << shift) - 1u);
            return {((lhs.zero << shift) | low) & mask,
                    (lhs.one << shift) & mask};
        }
        uint32_t high = shift == 0 ? 0 : mask & ~widthMask(bits - shift);
        KnownBits result{(lhs.zero >> shift) & mask, (lhs.one >> shift) & mask};
        if (inst->op_id_ == Instruction::LShr) {
            result.zero |= high;
        } else {
            uint32_t sign = 1u << (bits - 1);
            if (lhs.zero & sign) result.zero |= high;
            if (lhs.one & sign) result.one |= high;
        }
        return result;
    }
    default:
        return {};
    }
}

inline KnownBits computeKnownBits(Value *v) {
    return computeKnownBitsImpl(v, 0);
}

// Derive the tightest unsigned interval implied by the currently known bits.
// Unknown bits may independently be either zero or one, so `one` is a safe
// lower bound and every bit not known zero contributes to the upper bound.
inline bool knownUnsignedBounds(Value *v, uint32_t &lower, uint32_t &upper) {
    const unsigned bits = integerBitWidth(v);
    if (bits == 0 || bits > 32)
        return false;
    const uint32_t mask = widthMask(bits);
    KnownBits known = computeKnownBits(v);
    lower = known.one & mask;
    upper = static_cast<uint32_t>(~known.zero) & mask;
    return true;
}

// Signed bounds are directly available when the sign bit is known zero.
// This deliberately returns "unknown" for mixed/negative ranges rather than
// trying to linearize a wrapped unsigned interval.
inline bool knownNonNegativeBounds(Value *v, int64_t &lower, int64_t &upper) {
    const unsigned bits = integerBitWidth(v);
    if (bits == 0 || bits > 32)
        return false;
    uint32_t unsignedLower = 0, unsignedUpper = 0;
    if (!knownUnsignedBounds(v, unsignedLower, unsignedUpper))
        return false;
    const uint32_t signBit = 1u << (bits - 1);
    if (unsignedUpper & signBit)
        return false;
    lower = unsignedLower;
    upper = unsignedUpper;
    return true;
}

inline bool hasKnownZeroBits(Value *v, uint32_t mask) {
    return (computeKnownBits(v).zero & mask) == mask;
}

inline ICmpInst::ICmpOp swapCmp(ICmpInst::ICmpOp op) {
    switch (op) {
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGE;
    default:
        return op;
    }
}

inline bool nonNegativeBranchImpl(Value *v, BasicBlock *ctx) {
    if (!ctx || ctx->pre_bbs_.size() != 1)
        return false;

    for (auto *pred : ctx->pre_bbs_) {
        auto *term = dynamic_cast<BranchInst *>(pred->get_terminator());
        if (!term || term->num_ops() != 3)
            continue;

        auto *cond = dynamic_cast<ICmpInst *>(term->get_operand(0));
        auto *trueSucc = dynamic_cast<BasicBlock *>(term->get_operand(1));
        auto *falseSucc = dynamic_cast<BasicBlock *>(term->get_operand(2));
        if (!cond || !trueSucc || !falseSucc)
            continue;

        bool onTrue = trueSucc == ctx;
        bool onFalse = falseSucc == ctx;
        if (!onTrue && !onFalse)
            continue;

        ICmpInst::ICmpOp predOp = cond->icmp_op_;
        if (onFalse) {
            switch (predOp) {
            case ICmpInst::ICMP_SLT:
                predOp = ICmpInst::ICMP_SGE;
                break;
            case ICmpInst::ICMP_SLE:
                predOp = ICmpInst::ICMP_SGT;
                break;
            case ICmpInst::ICMP_SGT:
                predOp = ICmpInst::ICMP_SLE;
                break;
            case ICmpInst::ICMP_SGE:
                predOp = ICmpInst::ICMP_SLT;
                break;
            default:
                continue;
            }
        }

        Value *lhs = cond->get_operand(0);
        Value *rhs = cond->get_operand(1);
        auto *rhsConst = dynamic_cast<ConstantInt *>(rhs);
        auto *lhsConst = dynamic_cast<ConstantInt *>(lhs);
        if (!rhsConst && lhsConst) {
            std::swap(lhs, rhs);
            rhsConst = lhsConst;
            predOp = swapCmp(predOp);
        }
        if (lhs != v || !rhsConst)
            continue;

        if ((predOp == ICmpInst::ICMP_SGE && rhsConst->value_ == 0) ||
            (predOp == ICmpInst::ICMP_SGT && rhsConst->value_ == -1))
            return true;
    }

    return false;
}

// Prove loop-carried non-negativity as a closed induction over an SSA value
// cycle.  Encountering a value already on the recursion stack is the
// induction hypothesis; every non-cyclic incoming edge must still have a
// non-negative base, and every traversed operation must preserve the fact.
inline bool isNonNegativeRecurrenceImpl(
    Value *v, std::unordered_set<Value *> &visiting, int depth) {
    if (!v || depth > 32)
        return false;
    if (auto *ci = dynamic_cast<ConstantInt *>(v))
        return ci->value_ >= 0;

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)
        return false;
    if (inst->hasSemFlag(SemFlag::KnownNonNegative))
        return true;

    const unsigned bits = integerBitWidth(v);
    if (bits > 0 && bits <= 32 &&
        hasKnownZeroBits(v, 1u << (bits - 1)))
        return true;

    if (!visiting.insert(v).second)
        return true;
    auto finish = [&](bool result) {
        visiting.erase(v);
        return result;
    };
    auto recurse = [&](Value *operand) {
        return isNonNegativeRecurrenceImpl(operand, visiting, depth + 1);
    };

    if (inst->op_id_ == Instruction::LShr ||
        inst->op_id_ == Instruction::ZExt ||
        inst->op_id_ == Instruction::Clz)
        return finish(true);

    if (inst->op_id_ == Instruction::AShr)
        return finish(recurse(inst->get_operand(0)));

    if (inst->op_id_ == Instruction::SDiv) {
        auto *divisor =
            dynamic_cast<ConstantInt *>(inst->get_operand(1));
        return finish(divisor && divisor->value_ > 0 &&
                      recurse(inst->get_operand(0)));
    }

    if (inst->op_id_ == Instruction::And) {
        auto *mask = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (!mask)
            mask = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        if (mask && mask->value_ >= 0)
            return finish(true);
    }

    if (inst->op_id_ == Instruction::Select) {
        auto *cmp = dynamic_cast<ICmpInst *>(inst->get_operand(0));
        Value *trueValue = inst->get_operand(1);
        Value *falseValue = inst->get_operand(2);
        auto *trueConst = dynamic_cast<ConstantInt *>(trueValue);
        auto *zero = cmp
                         ? dynamic_cast<ConstantInt *>(cmp->get_operand(1))
                         : nullptr;
        if (cmp && cmp->icmp_op_ == ICmpInst::ICMP_SLT &&
            trueConst && trueConst->value_ == 0 &&
            cmp->get_operand(0) == falseValue &&
            zero && zero->value_ == 0)
            return finish(true);
        return finish(recurse(trueValue) && recurse(falseValue));
    }

    if (inst->op_id_ == Instruction::Or)
        return finish(recurse(inst->get_operand(0)) &&
                      recurse(inst->get_operand(1)));

    if (inst->op_id_ == Instruction::Add &&
        inst->hasSemFlag(SemFlag::NoSignedWrap))
        return finish(recurse(inst->get_operand(0)) &&
                      recurse(inst->get_operand(1)));

    if (inst->is_phi()) {
        for (unsigned i = 0; i + 1 < inst->num_ops(); i += 2) {
            if (!recurse(inst->get_operand(i)))
                return finish(false);
        }
        return finish(true);
    }

    return finish(false);
}

inline bool isKnownNonNegativeImpl(Value *v, BasicBlock *ctx, int depth,
                                   const DominatorTreeAnalysis *DT = nullptr) {
    if (!v || depth > 8)
        return false;

    if (auto *ci = dynamic_cast<ConstantInt *>(v))
        return ci->value_ >= 0;

    if (auto *arg = dynamic_cast<Argument *>(v)) {
        auto *func = arg->parent_;
        if (!func || !func->parent_)
            return nonNegativeBranchImpl(v, ctx);

        bool foundCaller = false;
        // A Function is the final operand of every CallInst and therefore
        // already owns an exact call-site index in its use list.  Scanning
        // the whole module for each formal argument makes a function with N
        // arguments and O(N) IR cost O(N^2), even when it has one call site.
        for (const Use &use : func->use_list_) {
            auto *call = dynamic_cast<CallInst *>(use.user_);
            if (!call || call->num_ops() == 0 ||
                call->get_operand(call->num_ops() - 1) != func ||
                arg->arg_no_ >= call->num_ops() - 1)
                continue;
            auto *caller =
                call->parent_ ? call->parent_->parent_ : nullptr;
            if (!caller || caller == func)
                continue;
            foundCaller = true;
            if (!isKnownNonNegativeImpl(call->get_operand(arg->arg_no_),
                                        call->parent_, depth + 1, DT))
                return false;
        }
        if (foundCaller)
            return true;
        return nonNegativeBranchImpl(v, ctx);
    }

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)
        return nonNegativeBranchImpl(v, ctx);

    if (inst->hasSemFlag(SemFlag::KnownNonNegative))
        return true;

    const unsigned bits = integerBitWidth(v);
    if (bits > 0 && bits <= 32 &&
        hasKnownZeroBits(v, 1u << (bits - 1)))
        return true;

    if (inst->op_id_ == Instruction::LShr || inst->op_id_ == Instruction::ZExt ||
        inst->op_id_ == Instruction::Clz)
        return true;

    if (inst->op_id_ == Instruction::AShr)
        return isKnownNonNegativeImpl(inst->get_operand(0), ctx, depth + 1, DT);

    if (inst->op_id_ == Instruction::And) {
        auto *mask = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (!mask)
            mask = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        if (mask && inst->type_->tid_ == Type::IntegerTyID) {
            auto bits = static_cast<IntegerType *>(inst->type_)->num_bits_;
            if (bits > 0) {
                uint64_t signBit = 1ull << (bits - 1);
                if (mask->value_ >= 0 &&
                    (static_cast<uint64_t>(mask->value_) & signBit) == 0)
                    return true;
            }
        }
    }

    if (inst->op_id_ == Instruction::Select) {
        // Canonical signed clamp: select (x < 0), 0, x == max(x, 0).
        // Recognizing the value relation (rather than requiring both arms to
        // be independently non-negative) lets later division and remainder
        // transforms safely use the fact.
        auto *cmp = dynamic_cast<ICmpInst *>(inst->get_operand(0));
        Value *trueValue = inst->get_operand(1);
        Value *falseValue = inst->get_operand(2);
        auto *trueConst = dynamic_cast<ConstantInt *>(trueValue);
        if (cmp && cmp->icmp_op_ == ICmpInst::ICMP_SLT &&
            trueConst && trueConst->value_ == 0 &&
            cmp->get_operand(0) == falseValue) {
            auto *zero = dynamic_cast<ConstantInt *>(cmp->get_operand(1));
            if (zero && zero->value_ == 0)
                return true;
        }
        return isKnownNonNegativeImpl(inst->get_operand(1), ctx, depth + 1, DT) &&
               isKnownNonNegativeImpl(inst->get_operand(2), ctx, depth + 1, DT);
    }

    if (inst->op_id_ == Instruction::Or)
        return isKnownNonNegativeImpl(inst->get_operand(0), ctx, depth + 1, DT) &&
               isKnownNonNegativeImpl(inst->get_operand(1), ctx, depth + 1, DT);

    if (inst->op_id_ == Instruction::Add &&
        inst->hasSemFlag(SemFlag::NoSignedWrap))
        return isKnownNonNegativeImpl(inst->get_operand(0), ctx, depth + 1, DT) &&
               isKnownNonNegativeImpl(inst->get_operand(1), ctx, depth + 1, DT);

    if (inst->is_phi()) {
        std::unordered_set<Value *> visiting;
        if (isNonNegativeRecurrenceImpl(inst, visiting, 0))
            return true;
    }

    // A bounded increasing induction variable stays non-negative when the
    // loop's taken edge dominates its backedge and the constant upper bound
    // leaves enough room for the update.  The explicit bound check is what
    // makes this independent of signed-overflow assumptions.
    if (inst->is_phi() && inst->parent_ && inst->num_ops() == 4) {
        auto *phi = static_cast<PhiInst *>(inst);
        Value *init = nullptr;
        BinaryInst *update = nullptr;
        BasicBlock *backedge = nullptr;
        int64_t step = 0;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *candidate = dynamic_cast<BinaryInst *>(phi->get_operand(i));
            auto *incomingBB = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            ConstantInt *stepConst = nullptr;
            if (candidate && candidate->op_id_ == Instruction::Add) {
                if (candidate->get_operand(0) == phi)
                    stepConst = dynamic_cast<ConstantInt *>(candidate->get_operand(1));
                else if (candidate->get_operand(1) == phi)
                    stepConst = dynamic_cast<ConstantInt *>(candidate->get_operand(0));
            }
            if (stepConst && stepConst->value_ > 0 && incomingBB &&
                candidate->parent_ == incomingBB) {
                if (update) return nonNegativeBranchImpl(v, ctx);
                update = candidate;
                backedge = incomingBB;
                step = stepConst->value_;
            } else {
                init = phi->get_operand(i);
            }
        }

        auto *term = dynamic_cast<BranchInst *>(inst->parent_->get_terminator());
        auto *cmp = term && term->num_ops() == 3
                        ? dynamic_cast<ICmpInst *>(term->get_operand(0))
                        : nullptr;
        if (init && update && cmp &&
            isKnownNonNegativeImpl(init, ctx, depth + 1, DT)) {
            ICmpInst::ICmpOp pred = cmp->icmp_op_;
            Value *boundValue = nullptr;
            if (cmp->get_operand(0) == phi) {
                boundValue = cmp->get_operand(1);
            } else if (cmp->get_operand(1) == phi) {
                pred = swapCmp(pred);
                boundValue = cmp->get_operand(0);
            }
            auto *bound = dynamic_cast<ConstantInt *>(boundValue);
            auto *taken = dynamic_cast<BasicBlock *>(term->get_operand(1));
            if (taken && backedge && DT && DT->dominates(taken, backedge)) {
                bool noOverflow = false;
                bool nonNegativeBound = false;
                if (bound) {
                    int64_t maxPhi = pred == ICmpInst::ICMP_SLT
                        ? static_cast<int64_t>(bound->value_) - 1
                        : static_cast<int64_t>(bound->value_);
                    nonNegativeBound = maxPhi >= 0;
                    noOverflow = maxPhi + step <=
                                 std::numeric_limits<int32_t>::max();
                } else if (pred == ICmpInst::ICMP_SLT && step == 1) {
                    // For any signed i32 bound, phi < bound proves
                    // phi <= INT_MAX-1, so incrementing once cannot overflow.
                    nonNegativeBound =
                        isKnownNonNegativeImpl(boundValue, inst->parent_, depth + 1, DT);
                    noOverflow = true;
                }
                bool upperBound = pred == ICmpInst::ICMP_SLT ||
                                  pred == ICmpInst::ICMP_SLE;
                if (upperBound && nonNegativeBound && noOverflow)
                    return true;
            }
        }
    }

    return nonNegativeBranchImpl(v, ctx);
}

inline bool isKnownNonNegative(Value *v, BasicBlock *ctx = nullptr,
                               const DominatorTreeAnalysis *DT = nullptr) {
    return isKnownNonNegativeImpl(v, ctx, 0, DT);
}

inline bool isKnownMultipleOfFromBranch(Value *v, int k, BasicBlock *ctx) {
    if (!ctx || ctx->pre_bbs_.size() != 1 || k <= 0 || k >= 32)
        return false;
    auto *br = ctx->pre_bbs_.front()->get_terminator();
    if (!br || br->op_id_ != Instruction::Br || br->num_ops() != 3 ||
        br->get_operand(1) != ctx)
        return false;
    auto *cond = dynamic_cast<ICmpInst *>(br->get_operand(0));
    if (!cond || cond->icmp_op_ != ICmpInst::ICMP_EQ)
        return false;
    Value *fact = cond->get_operand(0);
    auto *zero = dynamic_cast<ConstantInt *>(cond->get_operand(1));
    if (!zero || zero->value_ != 0) {
        zero = dynamic_cast<ConstantInt *>(cond->get_operand(0));
        fact = cond->get_operand(1);
    }
    if (!zero || zero->value_ != 0)
        return false;

    auto *factInst = dynamic_cast<Instruction *>(fact);
    if (!factInst)
        return false;
    uint32_t lowMask = (1u << k) - 1u;
    if (factInst->op_id_ == Instruction::SRem && factInst->get_operand(0) == v) {
        auto *divisor = dynamic_cast<ConstantInt *>(factInst->get_operand(1));
        return divisor &&
               (static_cast<uint32_t>(divisor->value_) & lowMask) == 0;
    }
    if (factInst->op_id_ == Instruction::And) {
        Value *other = factInst->get_operand(0);
        auto *andMask = dynamic_cast<ConstantInt *>(factInst->get_operand(1));
        if (!andMask) {
            andMask = dynamic_cast<ConstantInt *>(factInst->get_operand(0));
            other = factInst->get_operand(1);
        }
        return other == v && andMask &&
               (static_cast<uint32_t>(andMask->value_) & lowMask) == lowMask;
    }
    return false;
}

inline bool isKnownMultipleOf(Value *v, int k, BasicBlock *ctx = nullptr) {
    if (k <= 0)
        return true;
    const unsigned bits = integerBitWidth(v);
    if (bits == 0 || k > static_cast<int>(bits) || k >= 32)
        return false;
    uint32_t lowMask = (1u << k) - 1u;
    return hasKnownZeroBits(v, lowMask) ||
           isKnownMultipleOfFromBranch(v, k, ctx);
}

// 计算 |v| 的一个上界：若能证明 |v| <= bound 则设置 bound 并返回 true。
// 主要驱动"被除数幅值已小于除数"的除/余化简（x%C→x、x/C→0），其中最关键的
// 来源是 srem(_, C)：其结果幅值恒 < |C|，因此 (x%C)%C 这类内联后产生的冗余取余
// 可被折叠回单次取余。
inline bool knownAbsBound(Value *v, uint32_t &bound) {
    if (!v)
        return false;
    const unsigned bits = integerBitWidth(v);
    if (bits == 0 || bits > 32)
        return false;

    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        int64_t a = ci->value_ < 0 ? -static_cast<int64_t>(ci->value_)
                                   : static_cast<int64_t>(ci->value_);
        bound = static_cast<uint32_t>(a);
        return true;
    }

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)
        return false;

    // srem(_, C)：|结果| <= |C| - 1（与被除数、C 的符号都无关）
    if (inst->op_id_ == Instruction::SRem) {
        if (auto *c = dynamic_cast<ConstantInt *>(inst->get_operand(1))) {
            if (c->value_ != 0) {
                int64_t mag = c->value_ < 0 ? -static_cast<int64_t>(c->value_)
                                            : static_cast<int64_t>(c->value_);
                bound = static_cast<uint32_t>(mag - 1);
                return true;
            }
        }
    }

    // and x, mask（mask 为非负常量）：结果落在 [0, mask]
    if (inst->op_id_ == Instruction::And) {
        auto *m = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (!m)
            m = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        if (m && m->value_ >= 0) {
            bound = static_cast<uint32_t>(m->value_);
            return true;
        }
    }

    // 由 known bits 兜底：符号位已知为 0 ⇒ 非负，上界 = 未证零的低位全 1。
    // 自动覆盖 zext / lshr / and 掩码等来源。
    KnownBits kb = computeKnownBits(v);
    uint32_t mask = widthMask(bits);
    uint32_t signBit = 1u << (bits - 1);
    if (kb.zero & signBit) {
        bound = (~kb.zero) & mask;
        return true;
    }

    return false;
}

} // namespace ValueFacts
