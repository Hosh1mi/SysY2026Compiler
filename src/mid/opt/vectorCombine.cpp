// 典型示例：
//   优化前：逐 lane extract 两个向量、执行四次标量 add，再逐 lane insert。
//   优化后：用一条向量 add 产生完整结果向量。
// lane 索引、操作码和来源向量全部匹配时，标量拼装链可被整体替换。

// VectorCombine 从 extractelement/insertelement 形成的标量链中恢复向量运算。
// 它按 lane 核对来源向量、操作码和索引完整性，在成本模型确认有收益后，以
// 一条向量指令替换整组标量指令，并清理只为拼装结果存在的中间值。

#include "../../include/mid/opt/vectorCombine.hpp"

#include "../../include/mid/analysis/vectorizationCostModel.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <array>
#include <optional>
#include <vector>

namespace {

constexpr unsigned VectorWidth = VectorizationCostModel::VectorWidth;

// 提取常量 lane 下标，动态索引无法参与固定宽度模式匹配。
std::optional<unsigned> constantLane(Value *value) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    if (!constant || constant->value_ < 0 ||
        constant->value_ >= static_cast<std::int64_t>(VectorWidth))
        return std::nullopt;
    return static_cast<unsigned>(constant->value_);
}

// 检查向量类型和宽度是否受当前成本模型与后端支持。
bool isSupportedVectorType(Type *type) {
    auto *vector = dynamic_cast<VectorType *>(type);
    if (!vector || vector->num_elements_ != VectorWidth)
        return false;
    if (vector->contained_->tid_ == Type::FloatTyID)
        return true;
    auto *integer = dynamic_cast<IntegerType *>(vector->contained_);
    return integer && integer->num_bits_ == 32;
}

// 确认中间值只服务于当前拼装链，替换后可安全删除。
bool onlyUsedBy(Value *value, Instruction *user) {
    if (value->use_list_.empty())
        return false;
    for (const Use &use : value->use_list_)
        if (use.user_ != user)
            return false;
    return true;
}

// 判断操作码能否逐 lane 映射为整数向量二元指令。
bool integerLaneBinary(Instruction::OpID opcode) {
    switch (opcode) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
        return true;
    default:
        return false;
    }
}

// 判断操作码能否逐 lane 映射为浮点向量二元指令。
bool floatingLaneBinary(Instruction::OpID opcode) {
    switch (opcode) {
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
        return true;
    default:
        return false;
    }
}

// 匹配每个 lane 上结构相同的二元运算，并生成对应向量运算。
bool combineLaneBinary(BinaryInst *binary,
                       const VectorizationCostModel &costs) {
    if ((!integerLaneBinary(binary->op_id_) &&
         !floatingLaneBinary(binary->op_id_)) ||
        binary->use_list_.empty())
        return false;

    auto *lhs = dynamic_cast<ExtractElementInst *>(binary->get_operand(0));
    auto *rhs = dynamic_cast<ExtractElementInst *>(binary->get_operand(1));
    if (!lhs || !rhs || !onlyUsedBy(lhs, binary) ||
        !onlyUsedBy(rhs, binary))
        return false;

    auto lhsLane = constantLane(lhs->get_operand(1));
    auto rhsLane = constantLane(rhs->get_operand(1));
    Value *lhsVector = lhs->get_operand(0);
    Value *rhsVector = rhs->get_operand(0);
    if (!lhsLane || !rhsLane || *lhsLane != *rhsLane ||
        lhsVector->type_ != rhsVector->type_ ||
        !isSupportedVectorType(lhsVector->type_))
        return false;

    Type *elementType =
        static_cast<VectorType *>(lhsVector->type_)->contained_;
    const bool matchingInteger =
        integerLaneBinary(binary->op_id_) &&
        binary->type_->tid_ == Type::IntegerTyID &&
        elementType->tid_ == Type::IntegerTyID;
    const bool matchingFloat =
        floatingLaneBinary(binary->op_id_) &&
        binary->type_->tid_ == Type::FloatTyID &&
        elementType->tid_ == Type::FloatTyID;
    if (!matchingInteger && !matchingFloat)
        return false;

    const int removedExtracts = lhs == rhs ? 1 : 2;
    const int oldCost = costs.scalarInstructionCost(binary) +
                        removedExtracts * costs.extractElementCost();
    const int newCost = costs.vectorInstructionCost(binary) +
                        costs.extractElementCost();
    if (newCost >= oldCost)
        return false;

    BasicBlock *block = binary->parent_;
    auto *vectorBinary = new BinaryInst(
        lhsVector->type_, binary->op_id_, lhsVector, rhsVector, block, false);
    vectorBinary->copySemFlagsFrom(binary);
    auto *extract = new ExtractElementInst(
        vectorBinary, lhs->get_operand(1), block, false);
    if (!block->add_instruction_before_inst(vectorBinary, binary) ||
        !block->add_instruction_before_inst(extract, binary))
        return false;

    binary->replace_all_use_with(extract);
    return true;
}

struct LaneSource {
    Value *vector = nullptr;
    unsigned lane = 0;
};

// 将逐 lane 常量取余拼装链合并成可等价表达的向量计算。
bool combinePackedConstantRemainder(InsertElementInst *root) {
    auto *vectorType = dynamic_cast<VectorType *>(root->type_);
    auto *elementType = vectorType
                            ? dynamic_cast<IntegerType *>(
                                  vectorType->contained_)
                            : nullptr;
    if (!elementType || elementType->num_bits_ != 32 ||
        vectorType->num_elements_ != VectorWidth)
        return false;

    std::array<bool, VectorWidth> populated{};
    Value *sourceVector = nullptr;
    std::int64_t divisor = 0;
    unsigned populatedCount = 0;
    Instruction *expectedUser = nullptr;
    Value *cursor = root;

    while (auto *insert = dynamic_cast<InsertElementInst *>(cursor)) {
        if (insert != root && !onlyUsedBy(insert, expectedUser))
            return false;
        auto destinationLane = constantLane(insert->get_operand(2));
        if (!destinationLane)
            return false;

        if (!populated[*destinationLane]) {
            auto *remainder = dynamic_cast<BinaryInst *>(
                insert->get_operand(1));
            auto *extract = remainder
                                ? dynamic_cast<ExtractElementInst *>(
                                      remainder->get_operand(0))
                                : nullptr;
            auto *constant = remainder
                                 ? dynamic_cast<ConstantInt *>(
                                       remainder->get_operand(1))
                                 : nullptr;
            auto sourceLane = extract
                                  ? constantLane(extract->get_operand(1))
                                  : std::nullopt;
            if (!remainder || remainder->op_id_ != Instruction::SRem ||
                !extract || !constant || !sourceLane ||
                *sourceLane != *destinationLane ||
                !onlyUsedBy(remainder, insert) ||
                constant->value_ <= 1 ||
                (constant->value_ & (constant->value_ - 1)) == 0)
                return false;

            if (!sourceVector) {
                sourceVector = extract->get_operand(0);
                divisor = constant->value_;
            } else if (sourceVector != extract->get_operand(0) ||
                       divisor != constant->value_) {
                return false;
            }
            populated[*destinationLane] = true;
            ++populatedCount;
        }

        if (populatedCount == VectorWidth)
            break;
        expectedUser = insert;
        cursor = insert->get_operand(0);
    }

    if (populatedCount != VectorWidth || !sourceVector ||
        sourceVector->type_ != root->type_)
        return false;

    std::vector<Constant *> divisors;
    divisors.reserve(VectorWidth);
    for (unsigned lane = 0; lane < VectorWidth; ++lane)
        divisors.push_back(new ConstantInt(elementType, divisor));
    auto *divisorVector = new ConstantVector(vectorType, divisors);
    auto *vectorRemainder = new BinaryInst(
        vectorType, Instruction::SRem, sourceVector, divisorVector,
        root->parent_, false);
    if (!root->parent_->add_instruction_before_inst(vectorRemainder, root))
        return false;
    root->replace_all_use_with(vectorRemainder);
    return true;
}

// 从最终 insertelement 逆向收集完整 lane 链并尝试整体合并。
bool combineInsertChain(InsertElementInst *root,
                        const VectorizationCostModel &costs) {
    if (!isSupportedVectorType(root->type_))
        return false;

    std::array<std::optional<LaneSource>, VectorWidth> lanes;
    std::vector<InsertElementInst *> chain;
    Instruction *expectedUser = nullptr;
    Value *cursor = root;
    unsigned populated = 0;

    while (auto *insert = dynamic_cast<InsertElementInst *>(cursor)) {
        if (insert != root && !onlyUsedBy(insert, expectedUser))
            return false;
        auto destinationLane = constantLane(insert->get_operand(2));
        if (!destinationLane)
            return false;

        if (!lanes[*destinationLane]) {
            auto *extract = dynamic_cast<ExtractElementInst *>(
                insert->get_operand(1));
            auto sourceLane = extract
                                  ? constantLane(extract->get_operand(1))
                                  : std::nullopt;
            if (!extract || !sourceLane ||
                extract->get_operand(0)->type_ != root->type_)
                return false;
            lanes[*destinationLane] =
                LaneSource{extract->get_operand(0), *sourceLane};
            ++populated;
        }

        chain.push_back(insert);
        if (populated == VectorWidth)
            break;
        expectedUser = insert;
        cursor = insert->get_operand(0);
    }
    if (populated != VectorWidth)
        return false;

    std::array<Value *, 2> sources{};
    unsigned sourceCount = 0;
    std::vector<int> mask(VectorWidth);
    for (unsigned lane = 0; lane < VectorWidth; ++lane) {
        Value *source = lanes[lane]->vector;
        unsigned sourceIndex = 0;
        while (sourceIndex < sourceCount && sources[sourceIndex] != source)
            ++sourceIndex;
        if (sourceIndex == sourceCount) {
            if (sourceCount == sources.size())
                return false;
            sources[sourceCount++] = source;
        }
        mask[lane] = static_cast<int>(lanes[lane]->lane +
                                      sourceIndex * VectorWidth);
    }
    if (sourceCount == 1)
        sources[1] = sources[0];

    const int oldCost = static_cast<int>(chain.size()) *
                        costs.insertElementCost();
    if (costs.shuffleCost(mask) >= oldCost)
        return false;

    BasicBlock *block = root->parent_;
    auto *shuffle = new ShuffleVectorInst(
        sources[0], sources[1], mask, block, false);
    shuffle->copySemFlagsFrom(root);
    if (!block->add_instruction_before_inst(shuffle, root))
        return false;
    root->replace_all_use_with(shuffle);
    return true;
}

// 识别由嵌套 shuffle 构成的固定重排，并折叠为更直接的向量形式。
bool combineShuffleChain(ShuffleVectorInst *outer,
                         const VectorizationCostModel &costs) {
    if (!isSupportedVectorType(outer->type_))
        return false;

    ShuffleVectorInst *inner = nullptr;
    unsigned outerOperand = 0;
    bool selectsFirst = true;
    bool selectsSecond = true;
    for (int lane : outer->mask()) {
        selectsFirst &= lane >= 0 && lane < static_cast<int>(VectorWidth);
        selectsSecond &= lane >= static_cast<int>(VectorWidth) &&
                         lane < static_cast<int>(2 * VectorWidth);
    }
    if (selectsFirst) {
        inner = dynamic_cast<ShuffleVectorInst *>(outer->get_operand(0));
    } else if (selectsSecond) {
        inner = dynamic_cast<ShuffleVectorInst *>(outer->get_operand(1));
        outerOperand = 1;
    }
    if (!inner || !onlyUsedBy(inner, outer) ||
        inner->mask().size() != VectorWidth)
        return false;

    std::vector<int> composed(VectorWidth);
    for (unsigned lane = 0; lane < VectorWidth; ++lane) {
        unsigned selected = static_cast<unsigned>(outer->mask()[lane]) -
                            outerOperand * VectorWidth;
        composed[lane] = inner->mask()[selected];
    }
    if (costs.shuffleCost(composed) >=
        costs.shuffleCost(outer->mask()) + costs.shuffleCost(inner->mask()))
        return false;

    BasicBlock *block = outer->parent_;
    auto *shuffle = new ShuffleVectorInst(
        inner->get_operand(0), inner->get_operand(1), composed, block, false);
    shuffle->copySemFlagsFrom(outer);
    if (!block->add_instruction_before_inst(shuffle, outer))
        return false;
    outer->replace_all_use_with(shuffle);
    return true;
}

} // namespace

// 在单个函数内迭代匹配，直到本轮没有新的标量链可合并。
bool VectorCombine::runOnFunction(Function *function) {
    bool changed = false;
    const VectorizationCostModel costs;
    for (BasicBlock *block : function->basic_blocks_) {
        std::vector<Instruction *> instructions(
            block->instr_list_.begin(), block->instr_list_.end());
        for (Instruction *instruction : instructions) {
            if (instruction->parent_ != block)
                continue;
            if (auto *binary = dynamic_cast<BinaryInst *>(instruction))
                changed |= combineLaneBinary(binary, costs);
            else if (auto *insert =
                         dynamic_cast<InsertElementInst *>(instruction)) {
                changed |= combinePackedConstantRemainder(insert);
                changed |= combineInsertChain(insert, costs);
            } else if (auto *shuffle =
                         dynamic_cast<ShuffleVectorInst *>(instruction))
                changed |= combineShuffleChain(shuffle, costs);
        }
    }
    return changed;
}

// 模块入口：逐函数执行向量组合。
void VectorCombine::execute(Module *module) {
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            runOnFunction(function);
}

PreservedAnalyses VectorCombine::execute(Module *module, AnalysisManager &) {
    bool changed = false;
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function);
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}
