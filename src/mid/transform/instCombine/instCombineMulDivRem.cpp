#include "instCombineInternal.hpp"
#include "../../../include/mid/analysis/moduloRecurrenceAnalysis.hpp"

#include <limits>
#include <set>

namespace {

// Infer a signed i32 interval from operations whose bounds are independent of
// their inputs' exact values.  In particular, a remainder by a positive
// constant is always in (-C, C), which lets modular loop recurrences retain a
// useful bound through their header phi.
bool inferSignedBounds(Value *value, long long &lower, long long &upper,
                       std::set<Value *> &visiting, unsigned depth = 0) {
    if (!value || depth > 12 || !visiting.insert(value).second)
        return false;

    auto finish = [&](bool result) {
        visiting.erase(value);
        return result;
    };

    if (auto *constant = dynamic_cast<ConstantInt *>(value)) {
        lower = upper = constant->value_;
        return finish(true);
    }

    auto *inst = dynamic_cast<Instruction *>(value);
    auto *type = value->type_
                     ? dynamic_cast<IntegerType *>(value->type_)
                     : nullptr;
    if (!inst || !type || type->num_bits_ != 32)
        return finish(false);

    if (inst->op_id_ == Instruction::SRem) {
        auto *divisor =
            dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (!divisor || divisor->value_ <= 0)
            return finish(false);
        lower = -static_cast<long long>(divisor->value_) + 1;
        upper = static_cast<long long>(divisor->value_) - 1;
        return finish(true);
    }

    if (inst->op_id_ == Instruction::Add ||
        inst->op_id_ == Instruction::Sub) {
        long long lhsLower = 0, lhsUpper = 0;
        long long rhsLower = 0, rhsUpper = 0;
        if (!inferSignedBounds(inst->get_operand(0), lhsLower, lhsUpper,
                               visiting, depth + 1) ||
            !inferSignedBounds(inst->get_operand(1), rhsLower, rhsUpper,
                               visiting, depth + 1))
            return finish(false);

        if (inst->op_id_ == Instruction::Add) {
            lower = lhsLower + rhsLower;
            upper = lhsUpper + rhsUpper;
        } else {
            lower = lhsLower - rhsUpper;
            upper = lhsUpper - rhsLower;
        }
        if (lower < std::numeric_limits<int>::min() ||
            upper > std::numeric_limits<int>::max())
            return finish(false);
        return finish(true);
    }

    if (auto *select = dynamic_cast<SelectInst *>(inst)) {
        long long trueLower = 0, trueUpper = 0;
        long long falseLower = 0, falseUpper = 0;
        if (!inferSignedBounds(select->get_operand(1), trueLower, trueUpper,
                               visiting, depth + 1) ||
            !inferSignedBounds(select->get_operand(2), falseLower, falseUpper,
                               visiting, depth + 1))
            return finish(false);
        lower = std::min(trueLower, falseLower);
        upper = std::max(trueUpper, falseUpper);
        return finish(true);
    }

    if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
        bool haveIncoming = false;
        long long joinedLower = 0, joinedUpper = 0;
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            long long incomingLower = 0, incomingUpper = 0;
            if (!inferSignedBounds(phi->get_operand(i), incomingLower,
                                   incomingUpper, visiting, depth + 1))
                return finish(false);
            if (!haveIncoming) {
                joinedLower = incomingLower;
                joinedUpper = incomingUpper;
                haveIncoming = true;
            } else {
                joinedLower = std::min(joinedLower, incomingLower);
                joinedUpper = std::max(joinedUpper, incomingUpper);
            }
        }
        if (!haveIncoming)
            return finish(false);
        lower = joinedLower;
        upper = joinedUpper;
        return finish(true);
    }

    return finish(false);
}

bool inferSignedBounds(Value *value, long long &lower, long long &upper) {
    std::set<Value *> visiting;
    return inferSignedBounds(value, lower, upper, visiting);
}

// Keep a loop-carried remainder intact until loop transforms have had a
// chance to combine several unrolled recurrence steps.  A later InstCombine
// run can still lower the remaining remainder to bounded corrections.
bool isLoopCarriedRemainder(BinaryInst *remainder) {
    for (const auto &use : remainder->use_list_) {
        auto *phi = dynamic_cast<PhiInst *>(use.val_);
        if (!phi || use.arg_no_ % 2 != 0 ||
            use.arg_no_ + 1 >= phi->num_ops_ ||
            phi->get_operand(use.arg_no_ + 1) != remainder->parent_)
            continue;
        std::set<BasicBlock *> updateBlocks{remainder->parent_};
        ModuloRecurrenceAnalysis::Recurrence recurrence;
        if (!ModuloRecurrenceAnalysis::analyze(
                phi, remainder, updateBlocks, recurrence) ||
            !ModuloRecurrenceAnalysis::hasPrivateUpdateChain(
                recurrence, updateBlocks, true))
            continue;

        Value *init = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) != remainder->parent_) {
                init = phi->get_operand(i);
                break;
            }
        }
        long long initLower = 0, initUpper = 0;
        if (!init || !ModuloRecurrenceAnalysis::inferBounds(
                         init, initLower, initUpper))
            continue;

        const long long mod = recurrence.modulus->value_;
        auto advanceByTerms = [&](long long &lower, long long &upper) {
            for (const auto &term : recurrence.contributionTerms) {
                long long termLower = 0, termUpper = 0;
                if (!ModuloRecurrenceAnalysis::inferBounds(
                        term.value, termLower, termUpper))
                    return false;
                __int128 nextLower =
                    term.sign > 0
                        ? static_cast<__int128>(lower) + termLower
                        : static_cast<__int128>(lower) - termUpper;
                __int128 nextUpper =
                    term.sign > 0
                        ? static_cast<__int128>(upper) + termUpper
                        : static_cast<__int128>(upper) - termLower;
                if (nextLower < std::numeric_limits<int>::min() ||
                    nextUpper > std::numeric_limits<int>::max())
                    return false;
                lower = static_cast<long long>(nextLower);
                upper = static_cast<long long>(nextUpper);
            }
            return true;
        };

        long long prefixLower = std::min(initLower, -mod + 1);
        long long prefixUpper = std::max(initUpper, mod - 1);
        bool safe = true;
        // Cover every factor currently selected by LoopUnroll (4 or 8).
        for (int iteration = 0; iteration < 7; ++iteration)
            if (safe)
                safe = advanceByTerms(prefixLower, prefixUpper);
        long long finalLower = -mod + 1;
        long long finalUpper = mod - 1;
        if (safe)
            safe = advanceByTerms(finalLower, finalUpper);
        if (safe && ModuloRecurrenceAnalysis::needsAtMostOneCorrection(
                        prefixLower, prefixUpper, mod) &&
            ModuloRecurrenceAnalysis::needsAtMostOneCorrection(
                finalLower, finalUpper, mod))
            return true;
    }
    return false;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// visitMul — integer Mul
//
// Capabilities:
//   - Constant fold; canonicalize constant to RHS
//   - Identities: x*1 → x, x*0 → 0, x*(-1) → 0-x
//   - Reassoc: (x*C1)*C2 → x*(C1*C2)
//   - A53 fuse before strength-reduce:
//       mul x,2^k with sole user add/sub of same x → mul x, 2^k±1
//   - Strength reduce: mul x, 2^k → shl x, k
// ═══════════════════════════════════════════════════════════════════════

Value* visitMul(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    if (cx && !cy) {
        if (inst->get_operand(1) == x)
            return nullptr;
        auto *new_inst = new BinaryInst(ty, Instruction::Mul, y, x, bb, true);
        copySemFlags(inst, new_inst);
        stampIntegerFacts(new_inst);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy && cy->value_ == 1)
        return x;

    if (cy && cy->value_ == 0)
        return make_const_int(ty, 0);

    if (cy && cy->value_ == -1) {
        auto *neg = new BinaryInst(ty, Instruction::Sub,
            make_const_int(ty, 0), x, bb, true);
        stampIntegerFacts(neg);
        bb->add_instruction_before_inst(neg, inst);
        return neg;
    }

    if (cy) {
        auto *x_inst = dynamic_cast<Instruction*>(x);
        if (x_inst && x_inst->is_mul()) {
            auto *c1 = as_const_int(x_inst->get_operand(1));
            if (c1) {
                int combined;
                if (!ConstantEvaluator::foldIntegerBinary(
                        Instruction::Mul, c1->value_, cy->value_, combined))
                    return nullptr;
                auto *new_inst = new BinaryInst(ty, Instruction::Mul,
                    x_inst->get_operand(0),
                    make_const_int(ty, combined), bb, true);
                stampIntegerFacts(new_inst);
                bb->add_instruction_before_inst(new_inst, inst);
                return new_inst;
            }
        }
    }

    if (!cy) return nullptr;

    if (isPowerOfTwo(cy->value_) && cy->value_ > 1 &&
        inst->use_list_.size() == 1) {
        auto *user = dynamic_cast<Instruction*>((*inst->use_list_.begin()).val_);
        if (user) {
            if (user->op_id_ == Instruction::Add) {
                Value *op0 = user->get_operand(0);
                Value *op1 = user->get_operand(1);
                if ((op0 == x && op1 == inst) || (op0 == inst && op1 == x)) {
                    auto *new_mul = new BinaryInst(ty, Instruction::Mul,
                        x, make_const_int(ty, cy->value_ + 1), bb, true);
                    stampIntegerFacts(new_mul);
                    bb->add_instruction_before_inst(new_mul, user);
                    user->replace_all_use_with(new_mul);
                    user->parent_->delete_instr(user);
                    bb->delete_instr(inst);
                    return nullptr;
                }
            } else if (user->op_id_ == Instruction::Sub) {
                if (user->get_operand(0) == inst && user->get_operand(1) == x) {
                    auto *new_mul = new BinaryInst(ty, Instruction::Mul,
                        x, make_const_int(ty, cy->value_ - 1), bb, true);
                    stampIntegerFacts(new_mul);
                    bb->add_instruction_before_inst(new_mul, user);
                    user->replace_all_use_with(new_mul);
                    user->parent_->delete_instr(user);
                    bb->delete_instr(inst);
                    return nullptr;
                }
            }
        }
    }

    if (isPowerOfTwo(cy->value_) && cy->value_ > 1) {
        int shift = log2Int(cy->value_);
        auto *shl = new BinaryInst(ty, Instruction::Shl, x,
            make_const_int(ty, shift), bb, true);
        stampIntegerFacts(shl);
        bb->add_instruction_before_inst(shl, inst);
        return shl;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSDiv — integer SDiv
//
// Capabilities:
//   - Constant fold (div0 / INT_MIN/-1 rejected by evaluator)
//   - Identities: x/1 → x; x/(-1) → 0-x
//   - Range: |x| < |C| → 0
//   - Power-of-two: sdiv x, 2^k → ashr when x≥0 or exact multiple
//   - Variable pow2 divisor via isKnownPowerOfTwo, same ashr rule
// ═══════════════════════════════════════════════════════════════════════

Value* visitSDiv(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    if (cy && cy->value_ == 1)
        return x;

    if (cy && cy->value_ == -1) {
        auto *neg = new BinaryInst(ty, Instruction::Sub,
            make_const_int(ty, 0), x, bb, true);
        stampIntegerFacts(neg);
        bb->add_instruction_before_inst(neg, inst);
        return neg;
    }

    if (cy && cy->value_ != 0) {
        uint32_t b;
        int64_t cmag = cy->value_ < 0 ? -static_cast<int64_t>(cy->value_)
                                      : static_cast<int64_t>(cy->value_);
        if (knownAbsBound(x, b) && static_cast<int64_t>(b) < cmag)
            return make_const_int(ty, 0);
    }

    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        int k = log2Int(cy->value_);
        bool exact = isKnownMultipleOf(x, k, bb);
        bool nonNegative = isKnownNonNegative(x, bb);
        if (!nonNegative && gInstCombineRangeAnalysis)
            nonNegative = gInstCombineRangeAnalysis->isKnownNonNegative(x, bb);
        if (nonNegative || exact) {
            auto *ashr = new BinaryInst(ty, Instruction::AShr, x,
                            make_const_int(ty, k), bb, true);
            if (exact) ashr->setSemFlag(SemFlag::Exact);
            stampIntegerFacts(ashr);
            bb->add_instruction_before_inst(ashr, inst);
            return ashr;
        }
    }

    if (!cy) {
        int k;
        if (isKnownPowerOfTwo(y, k) && k > 0) {
            bool exact = isKnownMultipleOf(x, k, bb);
            bool nonNegative = isKnownNonNegative(x, bb);
            if (!nonNegative && gInstCombineRangeAnalysis)
                nonNegative =
                    gInstCombineRangeAnalysis->isKnownNonNegative(x, bb);
            if (nonNegative || exact) {
                auto *ashr = new BinaryInst(ty, Instruction::AShr, x,
                                make_const_int(ty, k), bb, true);
                if (exact) ashr->setSemFlag(SemFlag::Exact);
                stampIntegerFacts(ashr);
                bb->add_instruction_before_inst(ashr, inst);
                return ashr;
            }
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitSRem — integer SRem
//
// Capabilities:
//   - Constant fold; x%1 → 0; |x|<|C| → x
//   - RangeAnalysis: identity / x-C / select(sge, x-C, x) when range tight
//   - Power-of-two → and mask when:
//       sole user is icmp eq/ne …,0  (sign-safe zero test), or
//       x proven non-negative / exact multiple of 2^k
// ═══════════════════════════════════════════════════════════════════════

Value* visitSRem(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::IntegerTyID) return nullptr;
    stampIntegerFacts(inst);

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantInt *cx = as_const_int(x);
    ConstantInt *cy = as_const_int(y);

    if (cx && cy) {
        int result;
        if (ConstantEvaluator::foldIntegerBinary(inst->op_id_, cx->value_,
                                                 cy->value_, result))
            return make_const_int(ty, result);
        return nullptr;
    }

    if (cy && cy->value_ == 1)
        return make_const_int(ty, 0);

    // x % (-1) → 0  (same as %1 for two's complement remainder toward zero)
    if (cy && cy->value_ == -1)
        return make_const_int(ty, 0);

    if (cy && cy->value_ != 0) {
        uint32_t b;
        int64_t cmag = cy->value_ < 0 ? -static_cast<int64_t>(cy->value_)
                                      : static_cast<int64_t>(cy->value_);
        if (knownAbsBound(x, b) && static_cast<int64_t>(b) < cmag)
            return x;
    }

    if (cy && cy->value_ > 0 && isLoopCarriedRemainder(inst))
        return nullptr;

    // A value already known to lie in (-2M, 2M) needs at most one signed
    // correction in either direction.  This is especially valuable for
    // modular loop recurrences: the backedge remainder bounds the state phi,
    // and a smaller bounded increment keeps the next dividend in this range.
    // The replacement preserves C/IR signed-remainder behavior for negative
    // dividends and avoids a multiply-high division sequence on AArch64.
    if (cy && cy->value_ > 0) {
        long long lower = 0, upper = 0;
        long long modulus = cy->value_;
        if (inferSignedBounds(x, lower, upper) &&
            lower > -2 * modulus && upper < 2 * modulus) {
            auto *positiveMod = make_const_int(ty, cy->value_);
            Value *adjusted = x;

            if (upper >= modulus) {
                auto *highCmp = new ICmpInst(ICmpInst::ICMP_SGE, adjusted,
                                              positiveMod, bb, true);
                bb->add_instruction_before_inst(highCmp, inst);
                auto *highSub = new BinaryInst(ty, Instruction::Sub, adjusted,
                                                positiveMod, bb, true);
                stampIntegerFacts(highSub);
                bb->add_instruction_before_inst(highSub, inst);
                auto *highAdjusted =
                    new SelectInst(highCmp, highSub, adjusted, ty);
                stampIntegerFacts(highAdjusted);
                bb->add_instruction_before_inst(highAdjusted, inst);
                adjusted = highAdjusted;
            }

            if (lower <= -modulus) {
                auto *negativeMod = make_const_int(ty, -cy->value_);
                auto *lowCmp = new ICmpInst(ICmpInst::ICMP_SLE, adjusted,
                                             negativeMod, bb, true);
                bb->add_instruction_before_inst(lowCmp, inst);
                auto *lowAdd =
                    new BinaryInst(ty, Instruction::Add, adjusted,
                                   positiveMod, bb, true);
                stampIntegerFacts(lowAdd);
                bb->add_instruction_before_inst(lowAdd, inst);
                auto *lowAdjusted =
                    new SelectInst(lowCmp, lowAdd, adjusted, ty);
                stampIntegerFacts(lowAdjusted);
                bb->add_instruction_before_inst(lowAdjusted, inst);
                adjusted = lowAdjusted;
            }
            return adjusted;
        }
    }

    if (cy && cy->value_ > 0 && gInstCombineRangeAnalysis &&
        x->type_ && x->type_->tid_ == Type::IntegerTyID) {
        auto range = gInstCombineRangeAnalysis->getRange(x, bb);
        if (range.valid && !range.isTop && !range.isBottom) {
            long long mod = cy->value_;
            if (range.lower > -mod && range.upper < mod)
                return x;

            if (range.lower >= 0 && range.upper < mod)
                return x;

            long long doubleMod = 0;
            if (mod <= std::numeric_limits<long long>::max() / 2) {
                doubleMod = mod * 2;
                if (range.lower >= mod && range.upper < doubleMod) {
                    auto *subInst = new BinaryInst(ty, Instruction::Sub, x,
                                                   make_const_int(ty, cy->value_),
                                                   bb, true);
                    stampIntegerFacts(subInst);
                    bb->add_instruction_before_inst(subInst, inst);
                    return subInst;
                }

                if (range.lower >= 0 && range.upper < doubleMod) {
                    auto *cmpInst = new ICmpInst(ICmpInst::ICMP_SGE, x,
                                                 make_const_int(ty, mod), bb, true);
                    bb->add_instruction_before_inst(cmpInst, inst);

                    auto *subInst = new BinaryInst(ty, Instruction::Sub, x,
                                                   make_const_int(ty, mod), bb, true);
                    stampIntegerFacts(subInst);
                    bb->add_instruction_before_inst(subInst, inst);

                    auto *selInst = new SelectInst(cmpInst, subInst, x, ty);
                    stampIntegerFacts(selInst);
                    bb->add_instruction_before_inst(selInst, inst);
                    return selInst;
                }
            }
        }
    }

    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        if (inst->use_list_.size() == 1) {
            auto *cmp = dynamic_cast<ICmpInst*>((*inst->use_list_.begin()).val_);
            if (cmp && cmp->op_id_ == Instruction::ICmp &&
                (cmp->icmp_op_ == ICmpInst::ICMP_EQ ||
                 cmp->icmp_op_ == ICmpInst::ICMP_NE)) {
                auto *c0 = dynamic_cast<ConstantInt*>(cmp->get_operand(0));
                auto *c1 = dynamic_cast<ConstantInt*>(cmp->get_operand(1));
                bool cmp_with_zero = (c0 && c0->value_ == 0) || (c1 && c1->value_ == 0);
                if (cmp_with_zero) {
                    auto *andInst = new BinaryInst(ty, Instruction::And,
                        x, make_const_int(ty, cy->value_ - 1), bb, true);
                    stampIntegerFacts(andInst);
                    bb->add_instruction_before_inst(andInst, inst);
                    return andInst;
                }
            }
        }
    }
    if (cy && cy->value_ > 1 && isPowerOfTwo(cy->value_)) {
        int k = log2Int(cy->value_);
        bool nonNegative = isKnownNonNegative(x);
        if (!nonNegative && gInstCombineRangeAnalysis)
            nonNegative = gInstCombineRangeAnalysis->isKnownNonNegative(x, bb);
        if (nonNegative || isKnownMultipleOf(x, k, bb)) {
            auto *andInst = new BinaryInst(ty, Instruction::And,
                x, make_const_int(ty, cy->value_ - 1), bb, true);
            stampIntegerFacts(andInst);
            bb->add_instruction_before_inst(andInst, inst);
            return andInst;
        }
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFMul — floating-point FMul
//
// Capabilities:
//   - Constant fold; canonicalize constant to RHS
//   - Identity: x*1.0 → x; x*(-1.0) → fneg x
// ═══════════════════════════════════════════════════════════════════════

Value* visitFMul(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    if (cx && cy) {
        float result;
        if (ConstantEvaluator::foldFloatBinary(inst->op_id_, cx->value_,
                                               cy->value_, result))
            return make_const_float(ty, result);
        return nullptr;
    }

    if (cx && !cy) {
        auto *new_inst = new BinaryInst(ty, Instruction::FMul, y, x, bb, true);
        bb->add_instruction_before_inst(new_inst, inst);
        return new_inst;
    }

    if (cy && cy->value_ == 1.0f)
        return x;

    if (cy && cy->value_ == -1.0f) {
        auto *fneg = new UnaryInst(ty, Instruction::FNeg, x, bb, true);
        bb->add_instruction_before_inst(fneg, inst);
        return fneg;
    }

    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// visitFDiv — floating-point FDiv
//
// Capabilities:
//   - Constant fold (div0 guarded by evaluator)
//   - Identity: x/1.0 → x; x/(-1.0) → fneg x
// ═══════════════════════════════════════════════════════════════════════

Value* visitFDiv(BinaryInst *inst) {
    if (inst->type_->tid_ != Type::FloatTyID) return nullptr;

    Value *x = inst->get_operand(0);
    Value *y = inst->get_operand(1);
    Type *ty = inst->type_;
    BasicBlock *bb = inst->parent_;

    ConstantFloat *cx = as_const_float(x);
    ConstantFloat *cy = as_const_float(y);

    if (cx && cy) {
        float result;
        if (ConstantEvaluator::foldFloatBinary(inst->op_id_, cx->value_,
                                               cy->value_, result))
            return make_const_float(ty, result);
        return nullptr;
    }

    if (cy && cy->value_ == 1.0f)
        return x;

    if (cy && cy->value_ == -1.0f) {
        auto *fneg = new UnaryInst(ty, Instruction::FNeg, x, bb, true);
        bb->add_instruction_before_inst(fneg, inst);
        return fneg;
    }

    return nullptr;
}
