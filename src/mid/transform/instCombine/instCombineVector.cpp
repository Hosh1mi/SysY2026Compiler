/**
 * @file instCombineVector.cpp
 * @brief 实现向量常量、构造、抽取、插入、广播与归约指令的规范化和折叠。
 * @details 所有折叠都校验向量类型与 lane 数；常量零向量按元素类型构造，避免整数/浮点零混用。
 */

// Vector IR canonicalization and constant folding shared by source vectors
// and vectors synthesized by loop/SLP transforms.

#include "instCombineInternal.hpp"

#include <cstdint>
#include <vector>

namespace {

/**
 * @brief 构造与向量元素类型匹配的零常量。
 * @param type 向量的标量元素类型，当前支持整数和浮点类型。
 * @return 新建的整数零或浮点零常量。
 */
Constant *zeroLane(Type *type) {
    if (type->tid_ == Type::FloatTyID)
        return new ConstantFloat(type, 0.0f);
    return new ConstantInt(type, 0);
}

/**
 * @brief 实现 lanesOf 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param type 相关 IR 类型。
 * @param lanes 参数 `lanes`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
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

/**
 * @brief 判断 isSplat 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param type 相关 IR 类型。
 * @param integerValue 参数 `integerValue`，用于本函数的分析、匹配或 IR 构造。
 * @param floatValue 参数 `floatValue`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
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

/**
 * @brief 对一个整数向量 lane 执行常量二元运算折叠。
 * @param op 需要折叠的整数二元操作码。
 * @param lhs 左侧 lane 常量。
 * @param rhs 右侧 lane 常量。
 * @param type 折叠结果的标量整数类型。
 * @return 折叠成功时返回新常量；操作数类型或操作码不受支持时返回 nullptr。
 */
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

/**
 * @brief 对一个浮点向量 lane 执行常量二元运算折叠。
 * @param op 需要折叠的浮点二元操作码。
 * @param lhs 左侧 lane 常量。
 * @param rhs 右侧 lane 常量。
 * @param type 折叠结果的标量浮点类型。
 * @return 折叠成功时返回新浮点常量；类型不匹配或求值器拒绝时返回 nullptr。
 */
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

/**
 * @brief 构造所有 lane 均为对应元素类型零值的常量向量。
 * @param type 目标向量类型，决定元素类型和 lane 数量。
 * @return 新建的全零常量向量。
 */
ConstantVector *zeroVector(VectorType *type) {
    std::vector<Constant *> lanes(type->num_elements_);
    for (Constant *&lane : lanes)
        lane = zeroLane(type->contained_);
    return new ConstantVector(type, lanes);
}

} // namespace

/**
 * @brief 化简或逐 lane 折叠向量二元运算。
 * @param inst 待处理的向量二元指令。
 * @return 成功时返回等价的常量向量或原操作数；没有可用化简时返回 nullptr。
 */
Value *visitVectorBinary(BinaryInst *inst) {
    // 常量向量逐 lane 求值，任一 lane 无法安全折叠就放弃整个向量。
    // 非常量情形只应用与标量类型无关的零元、单位元和自消去规则。
    auto *type = dynamic_cast<VectorType *>(inst->type_);
    if (!type || inst->get_operand(0)->type_ != type ||
        inst->get_operand(1)->type_ != type)
        return nullptr;

    std::vector<Constant *> lhs;
    std::vector<Constant *> rhs;
    if (lanesOf(inst->get_operand(0), type, lhs) &&
        lanesOf(inst->get_operand(1), type, rhs)) {
        // 两侧都是常量向量时逐 lane 折叠；任何 lane 不支持该操作就整体放弃，
        // 不能生成一部分常量、一部分未定义的混合结果。
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

/**
 * @brief 折叠常量向量插入，并消除对同一 lane 的重复覆盖写入。
 * @param inst 待处理的 insertelement 指令。
 * @return 成功时返回折叠后的常量向量或新插入指令；无法化简时返回 nullptr。
 */
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
        // 同一 lane 的后一次插入完全覆盖前一次插入，旁路被覆盖的中间节点。
        auto *replacement = new InsertElementInst(
            previous->get_operand(0), inst->get_operand(1),
            inst->get_operand(2), inst->parent_, true);
        inst->parent_->add_instruction_before_inst(replacement, inst);
        return replacement;
    }
    return nullptr;
}

/**
 * @brief 折叠向量元素抽取，并沿 insert/shuffle 追溯实际来源 lane。
 * @param inst 待处理的 extractelement 指令。
 * @return 成功时返回对应常量、已插入的标量或新的抽取指令；无法化简时返回 nullptr。
 */
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
            // 抽取刚写入的同一 lane 时可直接转发标量值。
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

/**
 * @brief 消除恒等向量重排，或在两个输入均为常量时按掩码折叠结果。
 * @param inst 待处理的 shufflevector 指令。
 * @return 成功时返回原输入向量或折叠后的常量向量；无法化简时返回 nullptr。
 */
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
