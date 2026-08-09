// Vector IR canonicalization and constant folding shared by source vectors
// and vectors synthesized by loop/SLP transforms.

#include "instCombineInternal.hpp"

#include <cstdint>
#include <vector>

namespace {

Constant *zeroLane(Type *type) {
    if (type->tid_ == Type::FloatTyID)
        return new ConstantFloat(type, 0.0f);
    return new ConstantInt(type, 0);
}

bool lanesOf(Value *value, VectorType *type, std::vector<Constant *> &lanes) {
    if (auto *vector = dynamic_cast<ConstantVector *>(value)) {
        if (vector->type_ != type ||
            vector->elements_.size() != type->num_elements_)
            return false;
        lanes = vector->elements_;
        return true;
    }
    if (!dynamic_cast<ConstantZero *>(value))
        return false;
    lanes.clear();
    lanes.reserve(type->num_elements_);
    for (unsigned lane = 0; lane < type->num_elements_; ++lane)
        lanes.push_back(zeroLane(type->contained_));
    return true;
}

bool isSplat(Value *value, VectorType *type, int integerValue,
             float floatValue) {
    std::vector<Constant *> lanes;
    if (!lanesOf(value, type, lanes))
        return false;
    for (Constant *lane : lanes) {
        if (type->contained_->tid_ == Type::FloatTyID) {
            auto *constant = dynamic_cast<ConstantFloat *>(lane);
            if (!constant || constant->value_ != floatValue)
                return false;
        } else {
            auto *constant = dynamic_cast<ConstantInt *>(lane);
            if (!constant || constant->value_ != integerValue)
                return false;
        }
    }
    return true;
}

Constant *foldIntegerLane(Instruction::OpID op, Constant *lhs,
                          Constant *rhs, Type *type) {
    auto *left = dynamic_cast<ConstantInt *>(lhs);
    auto *right = dynamic_cast<ConstantInt *>(rhs);
    if (!left || !right)
        return nullptr;
    int result = 0;
    if (ConstantEvaluator::foldIntegerBinary(
            op, left->value_, right->value_, result))
        return new ConstantInt(type, result);

    const int shift = right->value_;
    switch (op) {
    case Instruction::And:
        result = left->value_ & right->value_;
        break;
    case Instruction::Or:
        result = left->value_ | right->value_;
        break;
    case Instruction::Xor:
        result = left->value_ ^ right->value_;
        break;
    case Instruction::Shl:
        result = shift >= 0 && shift < 32
                     ? static_cast<int32_t>(
                           static_cast<uint32_t>(left->value_) << shift)
                     : 0;
        break;
    case Instruction::LShr:
        result = shift >= 0 && shift < 32
                     ? static_cast<int32_t>(
                           static_cast<uint32_t>(left->value_) >> shift)
                     : 0;
        break;
    case Instruction::AShr:
        if (shift < 0)
            return nullptr;
        result = left->value_ >> (shift < 32 ? shift : 31);
        break;
    default:
        return nullptr;
    }
    return new ConstantInt(type, result);
}

Constant *foldFloatLane(Instruction::OpID op, Constant *lhs,
                        Constant *rhs, Type *type) {
    auto *left = dynamic_cast<ConstantFloat *>(lhs);
    auto *right = dynamic_cast<ConstantFloat *>(rhs);
    if (!left || !right)
        return nullptr;
    float result = 0.0f;
    if (!ConstantEvaluator::foldFloatBinary(
            op, left->value_, right->value_, result))
        return nullptr;
    return new ConstantFloat(type, result);
}

ConstantVector *zeroVector(VectorType *type) {
    std::vector<Constant *> lanes(type->num_elements_);
    for (Constant *&lane : lanes)
        lane = zeroLane(type->contained_);
    return new ConstantVector(type, lanes);
}

} // namespace

Value *visitVectorBinary(BinaryInst *inst) {
    auto *type = dynamic_cast<VectorType *>(inst->type_);
    if (!type || inst->get_operand(0)->type_ != type ||
        inst->get_operand(1)->type_ != type)
        return nullptr;

    std::vector<Constant *> lhs;
    std::vector<Constant *> rhs;
    if (lanesOf(inst->get_operand(0), type, lhs) &&
        lanesOf(inst->get_operand(1), type, rhs)) {
        std::vector<Constant *> folded;
        folded.reserve(type->num_elements_);
        for (unsigned lane = 0; lane < type->num_elements_; ++lane) {
            Constant *value = type->contained_->tid_ == Type::FloatTyID
                                  ? foldFloatLane(inst->op_id_, lhs[lane],
                                                  rhs[lane], type->contained_)
                                  : foldIntegerLane(inst->op_id_, lhs[lane],
                                                    rhs[lane], type->contained_);
            if (!value)
                return nullptr;
            folded.push_back(value);
        }
        return new ConstantVector(type, folded);
    }

    Value *left = inst->get_operand(0);
    Value *right = inst->get_operand(1);
    const bool integer = type->contained_->tid_ == Type::IntegerTyID;
    if (integer) {
        if ((inst->op_id_ == Instruction::Add ||
             inst->op_id_ == Instruction::Sub ||
             inst->op_id_ == Instruction::Or ||
             inst->op_id_ == Instruction::Xor ||
             inst->op_id_ == Instruction::Shl ||
             inst->op_id_ == Instruction::LShr ||
             inst->op_id_ == Instruction::AShr) &&
            isSplat(right, type, 0, 0.0f))
            return left;
        if ((inst->op_id_ == Instruction::Add ||
             inst->op_id_ == Instruction::Or ||
             inst->op_id_ == Instruction::Xor) &&
            isSplat(left, type, 0, 0.0f))
            return right;
        if (inst->op_id_ == Instruction::Mul &&
            isSplat(right, type, 1, 1.0f))
            return left;
        if (inst->op_id_ == Instruction::Mul &&
            isSplat(left, type, 1, 1.0f))
            return right;
        if ((inst->op_id_ == Instruction::Mul ||
             inst->op_id_ == Instruction::And) &&
            (isSplat(left, type, 0, 0.0f) ||
             isSplat(right, type, 0, 0.0f)))
            return zeroVector(type);
        if ((inst->op_id_ == Instruction::Sub ||
             inst->op_id_ == Instruction::Xor) &&
            left == right)
            return zeroVector(type);
    }
    return nullptr;
}

Value *visitInsertElement(InsertElementInst *inst) {
    auto *type = dynamic_cast<VectorType *>(inst->type_);
    auto *index = dynamic_cast<ConstantInt *>(inst->get_operand(2));
    if (!type || !index || index->value_ < 0 ||
        static_cast<unsigned>(index->value_) >= type->num_elements_)
        return nullptr;

    std::vector<Constant *> lanes;
    auto *inserted = dynamic_cast<Constant *>(inst->get_operand(1));
    if (inserted && lanesOf(inst->get_operand(0), type, lanes)) {
        lanes[index->value_] = inserted;
        return new ConstantVector(type, lanes);
    }

    auto *previous = dynamic_cast<InsertElementInst *>(inst->get_operand(0));
    auto *previousIndex = previous
                              ? dynamic_cast<ConstantInt *>(
                                    previous->get_operand(2))
                              : nullptr;
    if (previousIndex && previousIndex->value_ == index->value_) {
        auto *replacement = new InsertElementInst(
            previous->get_operand(0), inst->get_operand(1),
            inst->get_operand(2), inst->parent_, true);
        inst->parent_->add_instruction_before_inst(replacement, inst);
        return replacement;
    }
    return nullptr;
}

Value *visitExtractElement(ExtractElementInst *inst) {
    auto *type = dynamic_cast<VectorType *>(inst->get_operand(0)->type_);
    auto *index = dynamic_cast<ConstantInt *>(inst->get_operand(1));
    if (!type || !index || index->value_ < 0 ||
        static_cast<unsigned>(index->value_) >= type->num_elements_)
        return nullptr;

    std::vector<Constant *> lanes;
    if (lanesOf(inst->get_operand(0), type, lanes))
        return lanes[index->value_];

    if (auto *insert =
            dynamic_cast<InsertElementInst *>(inst->get_operand(0))) {
        auto *insertIndex = dynamic_cast<ConstantInt *>(
            insert->get_operand(2));
        if (insertIndex) {
            if (insertIndex->value_ == index->value_)
                return insert->get_operand(1);
            auto *replacement = new ExtractElementInst(
                insert->get_operand(0), inst->get_operand(1), inst->parent_,
                true);
            inst->parent_->add_instruction_before_inst(replacement, inst);
            return replacement;
        }
    }

    if (auto *shuffle =
            dynamic_cast<ShuffleVectorInst *>(inst->get_operand(0))) {
        const int selected = shuffle->mask()[index->value_];
        if (selected < 0 ||
            static_cast<unsigned>(selected) >= 2 * type->num_elements_)
            return nullptr;
        Value *source = selected < static_cast<int>(type->num_elements_)
                            ? shuffle->get_operand(0)
                            : shuffle->get_operand(1);
        int sourceIndex = selected % type->num_elements_;
        auto *replacement = new ExtractElementInst(
            source, new ConstantInt(inst->parent_->parent_->parent_->int32_ty_,
                                    sourceIndex),
            inst->parent_, true);
        inst->parent_->add_instruction_before_inst(replacement, inst);
        return replacement;
    }
    return nullptr;
}

Value *visitShuffleVector(ShuffleVectorInst *inst) {
    auto *type = dynamic_cast<VectorType *>(inst->type_);
    if (!type || inst->mask().size() != type->num_elements_)
        return nullptr;

    bool identity = true;
    for (unsigned lane = 0; lane < type->num_elements_; ++lane)
        identity &= inst->mask()[lane] == static_cast<int>(lane);
    if (identity)
        return inst->get_operand(0);

    std::vector<Constant *> lhs;
    std::vector<Constant *> rhs;
    if (!lanesOf(inst->get_operand(0), type, lhs) ||
        !lanesOf(inst->get_operand(1), type, rhs))
        return nullptr;
    std::vector<Constant *> folded;
    folded.reserve(type->num_elements_);
    for (int selected : inst->mask()) {
        if (selected < 0 ||
            static_cast<unsigned>(selected) >= 2 * type->num_elements_)
            return nullptr;
        folded.push_back(selected < static_cast<int>(type->num_elements_)
                             ? lhs[selected]
                             : rhs[selected - type->num_elements_]);
    }
    return new ConstantVector(type, folded);
}
