#pragma once

#include "../ir/ir.hpp"

#include <cstdint>

namespace ValueFacts {

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
    if (!ctx)
        return false;

    for (auto *pred : ctx->pre_bbs_) {
        auto *term = dynamic_cast<BranchInst *>(pred->get_terminator());
        if (!term || term->num_ops_ != 3)
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

inline bool isKnownNonNegativeImpl(Value *v, BasicBlock *ctx, int depth) {
    if (!v || depth > 8)
        return false;

    if (auto *ci = dynamic_cast<ConstantInt *>(v))
        return ci->value_ >= 0;

    if (auto *arg = dynamic_cast<Argument *>(v)) {
        auto *func = arg->parent_;
        auto *module = func ? func->parent_ : nullptr;
        if (!func || !module)
            return nonNegativeBranchImpl(v, ctx);

        bool foundCaller = false;
        for (auto *caller : module->function_list_) {
            if (!caller || caller->is_declaration() || caller == func)
                continue;
            for (auto *bb : caller->basic_blocks_) {
                for (auto *inst : bb->instr_list_) {
                    auto *call = dynamic_cast<CallInst *>(inst);
                    if (!call)
                        continue;
                    auto *callee =
                        dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
                    if (callee != func || arg->arg_no_ >= call->num_ops_ - 1)
                        continue;
                    foundCaller = true;
                    if (!isKnownNonNegativeImpl(call->get_operand(arg->arg_no_),
                                                call->parent_, depth + 1))
                        return false;
                }
            }
        }
        if (foundCaller)
            return true;
        return nonNegativeBranchImpl(v, ctx);
    }

    auto *inst = dynamic_cast<Instruction *>(v);
    if (!inst)
        return nonNegativeBranchImpl(v, ctx);

    if (inst->is_load()) {
        auto *gv = dynamic_cast<GlobalVariable *>(inst->get_operand(0));
        if (gv && gv->init_val_) {
            bool initNonNegative = false;
            if (auto *ci = dynamic_cast<ConstantInt *>(gv->init_val_))
                initNonNegative = ci->value_ >= 0;
            else if (dynamic_cast<ConstantZero *>(gv->init_val_))
                initNonNegative = true;

            if (!initNonNegative)
                return nonNegativeBranchImpl(v, ctx);

            auto *module = gv->type_ && inst->parent_ && inst->parent_->parent_
                               ? inst->parent_->parent_->parent_
                               : nullptr;
            if (!module)
                return nonNegativeBranchImpl(v, ctx);

            bool sawStore = false;
            for (auto *func : module->function_list_) {
                if (!func || func->is_declaration())
                    continue;
                for (auto *bb : func->basic_blocks_) {
                    for (auto *other : bb->instr_list_) {
                        if (!other->is_store() || other->get_operand(1) != gv)
                            continue;
                        sawStore = true;
                        if (!isKnownNonNegativeImpl(other->get_operand(0), bb, depth + 1))
                            return false;
                    }
                }
            }
            if (sawStore || initNonNegative)
                return true;
        }
    }

    if (inst->op_id_ == Instruction::LShr || inst->op_id_ == Instruction::ZExt ||
        inst->op_id_ == Instruction::Clz)
        return true;

    if (inst->op_id_ == Instruction::AShr)
        return isKnownNonNegativeImpl(inst->get_operand(0), ctx, depth + 1);

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

    if (inst->op_id_ == Instruction::Select)
        return isKnownNonNegativeImpl(inst->get_operand(1), ctx, depth + 1) &&
               isKnownNonNegativeImpl(inst->get_operand(2), ctx, depth + 1);

    if (inst->op_id_ == Instruction::Add) {
        auto *rhsConst = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        auto *lhsConst = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        if (rhsConst && rhsConst->value_ >= 0)
            return isKnownNonNegativeImpl(inst->get_operand(0), ctx, depth + 1);
        if (lhsConst && lhsConst->value_ >= 0)
            return isKnownNonNegativeImpl(inst->get_operand(1), ctx, depth + 1);
    }

    if (inst->op_id_ == Instruction::Sub) {
        auto *lhsConst = dynamic_cast<ConstantInt *>(inst->get_operand(0));
        auto *rhsConst = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (lhsConst && rhsConst)
            return lhsConst->value_ >= rhsConst->value_;

        if (lhsConst && lhsConst->value_ >= 0) {
            auto *rhsInst = dynamic_cast<Instruction *>(inst->get_operand(1));
            if (rhsInst &&
                (rhsInst->op_id_ == Instruction::AShr ||
                 rhsInst->op_id_ == Instruction::LShr)) {
                auto *shiftBase = dynamic_cast<ConstantInt *>(rhsInst->get_operand(0));
                if (shiftBase && shiftBase->value_ == lhsConst->value_)
                    return true;
            }
        }
    }

    if (inst->op_id_ == Instruction::SDiv) {
        auto *divisor = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (divisor && divisor->value_ > 0)
            return isKnownNonNegativeImpl(inst->get_operand(0), ctx, depth + 1);
    }

    if (inst->is_phi() && inst->parent_) {
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->num_ops_ == 4) {
            Value *initValue = nullptr;
            Value *backValue = nullptr;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                Value *incomingValue = phi->get_operand(i);
                auto *update = dynamic_cast<BinaryInst *>(incomingValue);
                int step = 0;
                bool isPositiveStep = false;
                if (update && update->is_add()) {
                    auto *rhsConst = dynamic_cast<ConstantInt *>(update->get_operand(1));
                    auto *lhsConst = dynamic_cast<ConstantInt *>(update->get_operand(0));
                    if (update->get_operand(0) == phi && rhsConst) {
                        step = rhsConst->value_;
                        isPositiveStep = true;
                    } else if (update->get_operand(1) == phi && lhsConst) {
                        step = lhsConst->value_;
                        isPositiveStep = true;
                    }
                }
                if (isPositiveStep && step > 0) {
                    backValue = incomingValue;
                } else {
                    initValue = incomingValue;
                }
            }
            if (!initValue || !backValue ||
                !isKnownNonNegativeImpl(initValue, ctx, depth + 1))
                return nonNegativeBranchImpl(v, ctx);

            auto *update = dynamic_cast<BinaryInst *>(backValue);
            if (update && update->is_add()) {
                auto *term = dynamic_cast<BranchInst *>(inst->parent_->get_terminator());
                auto *cmp = term && term->num_ops_ == 3
                                ? dynamic_cast<ICmpInst *>(term->get_operand(0))
                                : nullptr;
                if (cmp) {
                    ICmpInst::ICmpOp predOp = cmp->icmp_op_;
                    Value *bound = nullptr;
                    if (cmp->get_operand(0) == phi) {
                        bound = cmp->get_operand(1);
                    } else if (cmp->get_operand(1) == phi) {
                        predOp = swapCmp(predOp);
                        bound = cmp->get_operand(0);
                    }
                    if ((predOp == ICmpInst::ICMP_SLT || predOp == ICmpInst::ICMP_SLE) &&
                        isKnownNonNegativeImpl(bound, inst->parent_, depth + 1))
                        return true;
                }
            }
        }
    }

    return nonNegativeBranchImpl(v, ctx);
}

inline bool isKnownNonNegative(Value *v, BasicBlock *ctx = nullptr) {
    return isKnownNonNegativeImpl(v, ctx, 0);
}

} // namespace ValueFacts
