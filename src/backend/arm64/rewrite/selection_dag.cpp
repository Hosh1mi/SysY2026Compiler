#include "../../../include/backend/arm64/rewrite/selection_dag.hpp"
#include "../../../include/mid/ir/intrinsics.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace backend::aarch64 {
namespace {

SDOpcode binaryOpcode(Instruction::OpID opcode) {
    switch (opcode) {
    case Instruction::Add: return SDOpcode::Add;
    case Instruction::Sub: return SDOpcode::Sub;
    case Instruction::Mul: return SDOpcode::Mul;
    case Instruction::SDiv: return SDOpcode::SDiv;
    case Instruction::SRem: return SDOpcode::SRem;
    case Instruction::UDiv: return SDOpcode::UDiv;
    case Instruction::URem: return SDOpcode::URem;
    case Instruction::FAdd: return SDOpcode::FAdd;
    case Instruction::FSub: return SDOpcode::FSub;
    case Instruction::FMul: return SDOpcode::FMul;
    case Instruction::FDiv: return SDOpcode::FDiv;
    case Instruction::Shl: return SDOpcode::Shl;
    case Instruction::LShr: return SDOpcode::LShr;
    case Instruction::AShr: return SDOpcode::AShr;
    case Instruction::And: return SDOpcode::And;
    case Instruction::Or: return SDOpcode::Or;
    case Instruction::Xor: return SDOpcode::Xor;
    default: return SDOpcode::Invalid;
    }
}

const char *opcodeName(SDOpcode opcode) {
    switch (opcode) {
#define NAME(OP) case SDOpcode::OP: return #OP
    NAME(Invalid); NAME(EntryToken); NAME(Argument); NAME(Constant);
    NAME(FPConstant); NAME(GlobalAddress); NAME(FrameIndex); NAME(Add);
    NAME(Sub); NAME(Mul); NAME(SDiv); NAME(SRem); NAME(UDiv); NAME(URem);
    NAME(FAdd); NAME(FSub); NAME(FMul); NAME(FDiv); NAME(FNeg); NAME(Shl);
    NAME(LShr); NAME(AShr); NAME(And); NAME(Or); NAME(Xor); NAME(ICmp);
    NAME(FCmp); NAME(Select); NAME(GEP); NAME(Load); NAME(Store); NAME(ZExt);
    NAME(SExt); NAME(Trunc);
    NAME(FPToSI); NAME(SIToFP); NAME(Bitcast); NAME(Clz);
    NAME(Splat); NAME(InsertElement); NAME(ExtractElement);
    NAME(ShuffleVector); NAME(Phi); NAME(Call); NAME(TailCall); NAME(Branch);
    NAME(BranchCond); NAME(Return); NAME(MAdd); NAME(MSub);
    NAME(VectorReduceAdd); NAME(SMin); NAME(SMax); NAME(MulMod);
#undef NAME
    }
    return "Invalid";
}

const char *typeName(ValueType type) {
    switch (type) {
    case ValueType::I1: return "i1";
    case ValueType::I32: return "i32";
    case ValueType::I64: return "i64";
    case ValueType::F32: return "f32";
    case ValueType::Ptr: return "ptr";
    case ValueType::V4I32: return "v4i32";
    case ValueType::V4F32: return "v4f32";
    case ValueType::Flags: return "flags";
    default: return "invalid";
    }
}

bool isSupportedValueType(ValueType type) {
    return type == ValueType::I1 || type == ValueType::I32 ||
           type == ValueType::I64 ||
           type == ValueType::F32 || type == ValueType::Ptr ||
           type == ValueType::V4I32 || type == ValueType::V4F32 ||
           type == ValueType::Flags;
}

// AArch64 register-argument boundary: each bank has 8 slots.  Stack-passed
// args cannot be emitted as sibling TCO with the current frame layout.
bool callArgsFitInRegisters(Instruction *call) {
    unsigned integerArgs = 0;
    unsigned floatArgs = 0;
    for (unsigned i = 0; i + 1 < call->num_ops_; ++i) {
        ValueType type = SelectionDAGBuilder::valueType(
            call->get_operand(i)->type_);
        bool vectorBank = type == ValueType::F32 ||
                          type == ValueType::V4F32 ||
                          type == ValueType::V4I32;
        if (vectorBank)
            ++floatArgs;
        else
            ++integerArgs;
    }
    return integerArgs <= 8 && floatArgs <= 8;
}

bool sameSplatScalar(SDValue lhs, SDValue rhs) {
    if (lhs == rhs)
        return true;
    if (!lhs.node || !rhs.node ||
        lhs.result >= lhs.node->resultTypes().size() ||
        rhs.result >= rhs.node->resultTypes().size() ||
        lhs.node->resultTypes()[lhs.result] !=
            rhs.node->resultTypes()[rhs.result] ||
        lhs.node->opcode() != rhs.node->opcode())
        return false;
    if (lhs.node->opcode() == SDOpcode::Constant)
        return lhs.node->integer == rhs.node->integer;
    if (lhs.node->opcode() == SDOpcode::FPConstant)
        return lhs.node->floatingBits == rhs.node->floatingBits;
    return false;
}

struct FullSplatMatch {
    SDValue scalar;
    SDNode *base = nullptr;
    std::vector<SDNode *> inserts;
    std::vector<SDNode *> indices;
    std::vector<SDNode *> redundantScalars;
};

bool matchFullSplat(
    SDNode &root,
    const std::unordered_map<SDNode *, unsigned> &useCount,
    FullSplatMatch &match) {
    if (root.opcode() != SDOpcode::InsertElement ||
        root.resultTypes().empty() ||
        (root.resultTypes().front() != ValueType::V4I32 &&
         root.resultTypes().front() != ValueType::V4F32))
        return false;

    auto hasOneUse = [&](SDNode *node) {
        auto found = useCount.find(node);
        return found != useCount.end() && found->second == 1;
    };

    SDNode *current = &root;
    unsigned laneMask = 0;
    for (unsigned depth = 0; depth < 4; ++depth) {
        if (!current ||
            current->opcode() != SDOpcode::InsertElement ||
            current->operands().size() != 3 ||
            current->resultTypes().empty() ||
            current->resultTypes().front() != root.resultTypes().front())
            return false;

        SDValue inserted = current->operands()[1];
        if (depth == 0)
            match.scalar = inserted;
        else if (!sameSplatScalar(inserted, match.scalar))
            return false;
        else if (inserted.node != match.scalar.node)
            match.redundantScalars.push_back(inserted.node);

        SDNode *index = current->operands()[2].node;
        if (!index || index->opcode() != SDOpcode::Constant ||
            index->integer < 0 || index->integer >= 4)
            return false;
        unsigned laneBit = 1U << index->integer;
        if (laneMask & laneBit)
            return false;
        laneMask |= laneBit;
        match.inserts.push_back(current);
        match.indices.push_back(index);

        SDNode *previous = current->operands()[0].node;
        if (depth != 3 && (!previous || !hasOneUse(previous)))
            return false;
        current = previous;
    }

    if (laneMask != 0xf || !match.scalar.node ||
        match.scalar.result >= match.scalar.node->resultTypes().size())
        return false;
    ValueType scalarType =
        match.scalar.node->resultTypes()[match.scalar.result];
    if ((root.resultTypes().front() == ValueType::V4I32 &&
         scalarType != ValueType::I32) ||
        (root.resultTypes().front() == ValueType::V4F32 &&
         scalarType != ValueType::F32))
        return false;

    match.base = current;
    return true;
}

unsigned naturalAlignment(Type *type) {
    unsigned size = SelectionDAGBuilder::typeSize(type);
    if (size >= 16)
        return 16;
    if (size >= 8)
        return 8;
    return size >= 4 ? 4 : 1;
}

} // namespace

SelectionDAG::SelectionDAG() {
    SDNode &entry = createNode(SDOpcode::EntryToken, {});
    entryToken_ = SDValue{&entry, 0};
}

SDNode &SelectionDAG::createNode(SDOpcode opcode,
                                 std::vector<ValueType> resultTypes,
                                 std::vector<SDValue> operands) {
    nodes_.push_back(std::make_unique<SDNode>(
        nextNodeId_++, opcode, std::move(resultTypes), std::move(operands)));
    return *nodes_.back();
}

ValueType SelectionDAGBuilder::valueType(Type *type) {
    if (!type)
        return ValueType::Invalid;
    if (auto *integer = dynamic_cast<IntegerType *>(type)) {
        if (integer->num_bits_ == 1)
            return ValueType::I1;
        if (integer->num_bits_ == 32)
            return ValueType::I32;
        if (integer->num_bits_ == 64)
            return ValueType::I64;
        return ValueType::Invalid;
    }
    if (type->tid_ == Type::FloatTyID)
        return ValueType::F32;
    if (type->tid_ == Type::PointerTyID)
        return ValueType::Ptr;
    if (auto *vector = dynamic_cast<VectorType *>(type)) {
        if (vector->num_elements_ != 4)
            return ValueType::Invalid;
        if (vector->contained_->tid_ == Type::FloatTyID)
            return ValueType::V4F32;
        auto *element = dynamic_cast<IntegerType *>(vector->contained_);
        if (element && element->num_bits_ == 32)
            return ValueType::V4I32;
    }
    return ValueType::Invalid;
}

unsigned SelectionDAGBuilder::typeSize(Type *type) {
    if (!type)
        return 0;
    if (auto *integer = dynamic_cast<IntegerType *>(type))
        return integer->num_bits_ == 1 ? 1 : integer->num_bits_ / 8;
    if (type->tid_ == Type::FloatTyID)
        return 4;
    if (type->tid_ == Type::PointerTyID)
        return 8;
    if (auto *array = dynamic_cast<ArrayType *>(type))
        return array->num_elements_ * typeSize(array->contained_);
    if (auto *vector = dynamic_cast<VectorType *>(type))
        return vector->num_elements_ * typeSize(vector->contained_);
    return 0;
}

SDValue SelectionDAGBuilder::getValue(FunctionDAG &functionDAG,
                                      SelectionDAG &dag, Value *value) const {
    auto found = functionDAG.values.find(value);
    if (found != functionDAG.values.end())
        return found->second;

    ValueType type = valueType(value ? value->type_ : nullptr);
    if (auto *integer = dynamic_cast<ConstantInt *>(value)) {
        SDNode &node = dag.createNode(SDOpcode::Constant, {type});
        node.integer = integer->value_;
        return SDValue{&node, 0};
    }
    if (auto *floating = dynamic_cast<ConstantFloat *>(value)) {
        SDNode &node = dag.createNode(SDOpcode::FPConstant, {type});
        std::memcpy(&node.floatingBits, &floating->value_,
                    sizeof(node.floatingBits));
        return SDValue{&node, 0};
    }
    if (dynamic_cast<ConstantZero *>(value)) {
        SDNode &node = dag.createNode(
            type == ValueType::F32 ? SDOpcode::FPConstant : SDOpcode::Constant,
            {type});
        return SDValue{&node, 0};
    }
    if (auto *vector = dynamic_cast<ConstantVector *>(value)) {
        SDNode &node = dag.createNode(SDOpcode::Constant, {type});
        node.shuffleMask.reserve(vector->elements_.size());
        for (Constant *element : vector->elements_) {
            if (auto *constant = dynamic_cast<ConstantInt *>(element))
                node.shuffleMask.push_back(constant->value_);
            else if (auto *constant =
                         dynamic_cast<ConstantFloat *>(element)) {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &constant->value_, sizeof(bits));
                node.shuffleMask.push_back(static_cast<int>(bits));
            } else {
                node.shuffleMask.push_back(0);
            }
        }
        return SDValue{&node, 0};
    }
    if (auto *global = dynamic_cast<GlobalVariable *>(value)) {
        SDNode &node = dag.createNode(SDOpcode::GlobalAddress, {ValueType::Ptr});
        node.symbol = global->name_;
        return SDValue{&node, 0};
    }
    if (auto *argument = dynamic_cast<Argument *>(value)) {
        SDNode &node = dag.createNode(SDOpcode::Argument, {type});
        node.index = argument->arg_no_;
        SDValue result{&node, 0};
        functionDAG.values.emplace(value, result);
        return result;
    }

    throw std::logic_error("SelectionDAG use has no available definition");
}

std::unique_ptr<FunctionDAG>
SelectionDAGBuilder::build(Function *function) const {
    if (!function || function->is_declaration())
        throw std::invalid_argument("cannot build a DAG for a declaration");

    auto result = std::make_unique<FunctionDAG>(function);
    std::unordered_set<BasicBlock *> visited;
    std::vector<BasicBlock *> postOrder;
    std::function<void(BasicBlock *)> visit = [&](BasicBlock *block) {
        if (!block || !visited.insert(block).second)
            return;
        for (BasicBlock *successor : block->succ_bbs_)
            visit(successor);
        postOrder.push_back(block);
    };
    if (!function->basic_blocks_.empty())
        visit(function->basic_blocks_.front());
    std::reverse(postOrder.begin(), postOrder.end());
    result->blockOrder = std::move(postOrder);
    for (BasicBlock *block : function->basic_blocks_)
        if (!visited.count(block))
            result->blockOrder.push_back(block);
    for (BasicBlock *block : result->blockOrder)
        result->blocks.emplace(block, std::make_unique<SelectionDAG>());

    SelectionDAG &entryDAG = *result->blocks.at(result->blockOrder.front());
    for (Argument *argument : function->arguments_)
        getValue(*result, entryDAG, argument);

    unsigned nextFrameIndex = 0;
    // PHIs and allocas must be available before normal traversal because loop
    // backedges and non-entry allocas can be referenced outside linear order.
    for (BasicBlock *block : result->blockOrder) {
        SelectionDAG &dag = *result->blocks.at(block);
        for (Instruction *instruction : block->instr_list_) {
            if (auto *phi = dynamic_cast<PhiInst *>(instruction)) {
                SDNode &node =
                    dag.createNode(SDOpcode::Phi, {valueType(phi->type_)});
                node.origin = phi;
                result->values.emplace(phi, SDValue{&node, 0});
            } else if (auto *alloca =
                           dynamic_cast<AllocaInst *>(instruction)) {
                SDNode &node =
                    dag.createNode(SDOpcode::FrameIndex, {ValueType::Ptr});
                node.index = nextFrameIndex++;
                node.memorySize = typeSize(alloca->alloca_ty_);
                node.alignment = naturalAlignment(alloca->alloca_ty_);
                node.origin = alloca;
                result->values.emplace(alloca, SDValue{&node, 0});
            }
        }
    }

    std::unordered_map<PhiInst *, std::vector<SDValue>>
        deferredPhiOperands;
    for (BasicBlock *block : result->blockOrder) {
        for (Instruction *instruction : block->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(instruction);
            if (!phi)
                break;
            auto &operands = deferredPhiOperands[phi];
            operands.resize(phi->num_ops_ / 2);
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                Value *value = phi->get_operand(i);
                if (dynamic_cast<Constant *>(value) ||
                    dynamic_cast<GlobalVariable *>(value))
                    operands[i / 2] =
                        getValue(*result, entryDAG, value);
            }
        }
    }

    for (BasicBlock *block : result->blockOrder) {
        SelectionDAG &dag = *result->blocks.at(block);
        SDValue chain = dag.entryToken();
        Instruction *skipInstruction = nullptr;

        for (Instruction *instruction : block->instr_list_) {
            if (instruction == skipInstruction) {
                skipInstruction = nullptr;
                continue;
            }
            if (dynamic_cast<PhiInst *>(instruction))
                continue;
            if (dynamic_cast<AllocaInst *>(instruction))
                continue;

            auto operand = [&](unsigned index) {
                return getValue(*result, dag, instruction->get_operand(index));
            };
            SDNode *created = nullptr;
            SDOpcode binary = binaryOpcode(instruction->op_id_);
            if (binary != SDOpcode::Invalid) {
                created = &dag.createNode(binary, {valueType(instruction->type_)},
                                          {operand(0), operand(1)});
            } else {
                switch (instruction->op_id_) {
                case Instruction::FNeg:
                    created = &dag.createNode(
                        SDOpcode::FNeg, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::ICmp: {
                    created = &dag.createNode(SDOpcode::ICmp,
                                              {valueType(instruction->type_)},
                                              {operand(0), operand(1)});
                    created->predicate =
                        static_cast<int>(
                            static_cast<ICmpInst *>(instruction)->icmp_op_);
                    break;
                }
                case Instruction::FCmp: {
                    created = &dag.createNode(SDOpcode::FCmp,
                                              {valueType(instruction->type_)},
                                              {operand(0), operand(1)});
                    created->predicate =
                        static_cast<int>(
                            static_cast<FCmpInst *>(instruction)->fcmp_op_);
                    break;
                }
                case Instruction::Select:
                    created = &dag.createNode(
                        SDOpcode::Select, {valueType(instruction->type_)},
                        {operand(0), operand(1), operand(2)});
                    break;
                case Instruction::GetElementPtr: {
                    created = &dag.createNode(
                        SDOpcode::GEP, {ValueType::Ptr}, {operand(0)});
                    Type *current =
                        static_cast<PointerType *>(
                            instruction->get_operand(0)->type_)->contained_;
                    for (unsigned i = 1; i < instruction->num_ops_; ++i) {
                        created->operands().push_back(operand(i));
                        if (i > 1 && current &&
                            current->tid_ == Type::ArrayTyID)
                            current =
                                static_cast<ArrayType *>(current)->contained_;
                        created->gepScales.push_back(typeSize(current));
                    }
                    break;
                }
                case Instruction::Load: {
                    created = &dag.createNode(
                        SDOpcode::Load,
                        {valueType(instruction->type_), ValueType::Invalid},
                        {chain, operand(0)});
                    created->memorySize = typeSize(instruction->type_);
                    created->alignment = naturalAlignment(instruction->type_);
                    chain = SDValue{created, 1};
                    break;
                }
                case Instruction::Store: {
                    Type *storedType = instruction->get_operand(0)->type_;
                    created = &dag.createNode(SDOpcode::Store,
                                              {ValueType::Invalid},
                                              {chain, operand(0), operand(1)});
                    created->memorySize = typeSize(storedType);
                    created->alignment = naturalAlignment(storedType);
                    chain = SDValue{created, 0};
                    break;
                }
                case Instruction::ZExt:
                    created = &dag.createNode(
                        SDOpcode::ZExt, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::SExt:
                    created = &dag.createNode(
                        SDOpcode::SExt, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::Trunc:
                    created = &dag.createNode(
                        SDOpcode::Trunc, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::FPtoSI:
                    created = &dag.createNode(
                        SDOpcode::FPToSI, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::SItoFP:
                    created = &dag.createNode(
                        SDOpcode::SIToFP, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::BitCast:
                    created = &dag.createNode(
                        SDOpcode::Bitcast, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::Clz:
                    created = &dag.createNode(
                        SDOpcode::Clz, {valueType(instruction->type_)},
                        {operand(0)});
                    break;
                case Instruction::InsertElement: {
                    SDValue vector = operand(0);
                    ValueType resultType =
                        valueType(instruction->type_);
                    if (valueType(
                            instruction->get_operand(0)->type_) !=
                        resultType) {
                        SDNode &undef = dag.createNode(
                            SDOpcode::Constant, {resultType});
                        undef.shuffleMask.assign(4, 0);
                        vector = SDValue{&undef, 0};
                    }
                    created = &dag.createNode(
                        SDOpcode::InsertElement,
                        {resultType},
                        {vector, operand(1), operand(2)});
                    break;
                }
                case Instruction::ExtractElement:
                    created = &dag.createNode(
                        SDOpcode::ExtractElement,
                        {valueType(instruction->type_)},
                        {operand(0), operand(1)});
                    break;
                case Instruction::ShuffleVector: {
                    created = &dag.createNode(
                        SDOpcode::ShuffleVector,
                        {valueType(instruction->type_)},
                        {operand(0), operand(1)});
                    created->shuffleMask =
                        static_cast<ShuffleVectorInst *>(instruction)->mask();
                    break;
                }
                case Instruction::Call: {
                    auto *callee = dynamic_cast<Function *>(
                        instruction->get_operand(instruction->num_ops_ - 1));
                    SignedMinMaxIntrinsic minMaxKind;
                    if (isSignedMinMaxIntrinsic(callee, &minMaxKind)) {
                        if (instruction->num_ops_ != 3)
                            throw std::logic_error(
                                "signed min/max intrinsic must have two operands");
                        created = &dag.createNode(
                            minMaxKind == SignedMinMaxIntrinsic::SMin
                                ? SDOpcode::SMin
                                : SDOpcode::SMax,
                            {valueType(instruction->type_)},
                            {operand(0), operand(1)});
                        break;
                    }
                    if (isMulModIntrinsic(callee)) {
                        if (instruction->num_ops_ != 4)
                            throw std::logic_error(
                                "mulmod intrinsic must have three operands");
                        created = &dag.createNode(
                            SDOpcode::MulMod,
                            {valueType(instruction->type_)},
                            {operand(0), operand(1), operand(2)});
                        break;
                    }
                    auto *callInst = static_cast<CallInst *>(instruction);
                    Instruction *term = block->get_terminator();
                    bool emitTail =
                        callInst->is_tail() && callArgsFitInRegisters(callInst) &&
                        term && term->is_ret() &&
                        ((callInst->is_void() && term->num_ops_ == 0) ||
                         (term->num_ops_ > 0 &&
                          term->get_operand(0) == callInst));
                    std::vector<SDValue> operands = {chain};
                    for (unsigned i = 0; i + 1 < instruction->num_ops_; ++i)
                        operands.push_back(operand(i));
                    if (emitTail) {
                        // Terminator: reuse the frame / LR of the caller.
                        created = &dag.createNode(SDOpcode::TailCall,
                                                  {ValueType::Invalid},
                                                  std::move(operands));
                        created->symbol =
                            callee ? callee->name_ : std::string();
                        chain = SDValue{created, 0};
                        skipInstruction = term;
                        break;
                    }
                    std::vector<ValueType> results;
                    if (!instruction->is_void())
                        results.push_back(valueType(instruction->type_));
                    results.push_back(ValueType::Invalid);
                    created = &dag.createNode(SDOpcode::Call,
                                              std::move(results),
                                              std::move(operands));
                    created->symbol = callee ? callee->name_ : std::string();
                    chain = SDValue{
                        created,
                        static_cast<unsigned>(created->resultTypes().size() - 1)};
                    break;
                }
                case Instruction::Br: {
                    if (instruction->num_ops_ == 1) {
                        created = &dag.createNode(SDOpcode::Branch,
                                                  {ValueType::Invalid},
                                                  {chain});
                        created->incomingBlocks.push_back(
                            dynamic_cast<BasicBlock *>(
                                instruction->get_operand(0)));
                    } else {
                        created = &dag.createNode(SDOpcode::BranchCond,
                                                  {ValueType::Invalid},
                                                  {chain, operand(0)});
                        created->incomingBlocks.push_back(
                            dynamic_cast<BasicBlock *>(
                                instruction->get_operand(1)));
                        created->incomingBlocks.push_back(
                            dynamic_cast<BasicBlock *>(
                                instruction->get_operand(2)));
                    }
                    chain = SDValue{created, 0};
                    break;
                }
                case Instruction::Ret: {
                    std::vector<SDValue> operands = {chain};
                    if (instruction->num_ops_)
                        operands.push_back(operand(0));
                    created = &dag.createNode(SDOpcode::Return,
                                              {ValueType::Invalid},
                                              std::move(operands));
                    chain = SDValue{created, 0};
                    break;
                }
                default:
                    throw std::logic_error(
                        "SelectionDAGBuilder does not cover an IR opcode");
                }
            }

            if (created) {
                created->origin = instruction;
                if (created->opcode() != SDOpcode::TailCall &&
                    !instruction->is_void() &&
                    instruction->op_id_ != Instruction::Store &&
                    instruction->op_id_ != Instruction::Br &&
                    instruction->op_id_ != Instruction::Ret)
                    result->values[instruction] = SDValue{created, 0};
            }
        }
    }

    // Resolve PHI inputs only after all ordinary SSA definitions have DAG
    // identities.  Loop backedges necessarily refer to values selected later
    // in reverse-postorder traversal.
    for (BasicBlock *block : result->blockOrder) {
        SelectionDAG &dag = *result->blocks.at(block);
        for (Instruction *instruction : block->instr_list_) {
            auto *phi = dynamic_cast<PhiInst *>(instruction);
            if (!phi)
                break;
            SDNode *node = result->values.at(phi).node;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                SDValue incoming =
                    deferredPhiOperands[phi][i / 2];
                if (!incoming)
                    incoming = getValue(
                        *result, dag, phi->get_operand(i));
                node->operands().push_back(incoming);
                node->incomingBlocks.push_back(
                    dynamic_cast<BasicBlock *>(phi->get_operand(i + 1)));
            }
        }
    }
    return result;
}

void DAGLegalizer::run(FunctionDAG &functionDAG) const {
    for (BasicBlock *block : functionDAG.blockOrder) {
        SelectionDAG &dag = *functionDAG.blocks.at(block);
        for (const auto &node : dag.nodes()) {
            for (ValueType type : node->resultTypes()) {
                if (type != ValueType::Invalid && !isSupportedValueType(type))
                    throw std::logic_error(
                        "DAG contains a type outside the SysY backend scope");
            }
            if ((node->opcode() == SDOpcode::InsertElement ||
                 node->opcode() == SDOpcode::ExtractElement ||
                 node->opcode() == SDOpcode::ShuffleVector) &&
                node->resultTypes().front() != ValueType::V4I32 &&
                node->resultTypes().front() != ValueType::V4F32 &&
                node->opcode() != SDOpcode::ExtractElement)
                throw std::logic_error("unsupported vector legalization");
            if ((node->opcode() == SDOpcode::SMin ||
                 node->opcode() == SDOpcode::SMax) &&
                node->resultTypes().front() != ValueType::I32 &&
                node->resultTypes().front() != ValueType::V4I32)
                throw std::logic_error("unsupported signed min/max type");
        }
    }
}

// Fusing vmul into fmla/fmls collapses two roundings into one, which can
// change IEEE-754 results by a few ulps.  Kept behind an environment flag so
// the default build stays bit-exact with the uncontracted code; enable only
// when the application permits relaxed floating-point contraction.
static bool fpContractionEnabled() {
    return std::getenv("SYSY_ENABLE_FP_CONTRACT") != nullptr;
}

bool DAGCombiner::run(FunctionDAG &functionDAG,
                      bool enableOptimizations) const {
    if (!enableOptimizations)
        return false;

    std::unordered_map<SDNode *, unsigned> useCount;
    for (BasicBlock *block : functionDAG.blockOrder)
        for (const auto &node : functionDAG.blocks.at(block)->nodes())
            for (SDValue operand : node->operands())
                if (operand.node)
                    ++useCount[operand.node];

    bool changed = false;
    for (BasicBlock *block : functionDAG.blockOrder) {
        for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
            SDNode &node = *owned;
            if (node.opcode() == SDOpcode::Add &&
                node.operands().size() == 2 &&
                (node.resultTypes().front() == ValueType::I32 ||
                 node.resultTypes().front() == ValueType::V4I32)) {
                for (unsigned multiplyIndex = 0; multiplyIndex < 2;
                     ++multiplyIndex) {
                    SDNode *multiply = node.operands()[multiplyIndex].node;
                    if (!multiply || multiply->opcode() != SDOpcode::Mul ||
                        useCount[multiply] != 1)
                        continue;
                    SDValue addend = node.operands()[1 - multiplyIndex];
                    node.setOpcode(SDOpcode::MAdd);
                    node.operands() = {multiply->operands()[0],
                                       multiply->operands()[1], addend};
                    multiply->setOpcode(SDOpcode::Invalid);
                    changed = true;
                    break;
                }
            } else if (node.opcode() == SDOpcode::Sub &&
                       node.operands().size() == 2 &&
                       (node.resultTypes().front() == ValueType::I32 ||
                        node.resultTypes().front() == ValueType::V4I32)) {
                SDNode *multiply = node.operands()[1].node;
                if (multiply && multiply->opcode() == SDOpcode::Mul &&
                    useCount[multiply] == 1) {
                    SDValue minuend = node.operands()[0];
                    node.setOpcode(SDOpcode::MSub);
                    node.operands() = {multiply->operands()[0],
                                       multiply->operands()[1], minuend};
                    multiply->setOpcode(SDOpcode::Invalid);
                    changed = true;
                }
            } else if (node.opcode() == SDOpcode::FAdd &&
                       node.operands().size() == 2 &&
                       node.resultTypes().front() == ValueType::V4F32 &&
                       fpContractionEnabled()) {
                for (unsigned multiplyIndex = 0; multiplyIndex < 2;
                     ++multiplyIndex) {
                    SDNode *multiply =
                        node.operands()[multiplyIndex].node;
                    if (!multiply ||
                        multiply->opcode() != SDOpcode::FMul ||
                        useCount[multiply] != 1)
                        continue;
                    SDValue addend = node.operands()[1 - multiplyIndex];
                    node.setOpcode(SDOpcode::FMAdd);
                    node.operands() = {multiply->operands()[0],
                                       multiply->operands()[1], addend};
                    multiply->setOpcode(SDOpcode::Invalid);
                    changed = true;
                    break;
                }
            } else if (node.opcode() == SDOpcode::FSub &&
                       node.operands().size() == 2 &&
                       node.resultTypes().front() == ValueType::V4F32 &&
                       fpContractionEnabled()) {
                SDNode *multiply = node.operands()[1].node;
                if (multiply && multiply->opcode() == SDOpcode::FMul &&
                    useCount[multiply] == 1) {
                    SDValue minuend = node.operands()[0];
                    node.setOpcode(SDOpcode::FMSub);
                    node.operands() = {multiply->operands()[0],
                                       multiply->operands()[1], minuend};
                    multiply->setOpcode(SDOpcode::Invalid);
                    changed = true;
                }
            }
        }
    }

    // A complete chain that overwrites all four lanes with the same scalar
    // does not depend on its original vector value.  Keep this combine at DAG
    // level so every vector-producing mid-end pass benefits from the target's
    // native scalar-to-vector DUP instruction.
    for (BasicBlock *block : functionDAG.blockOrder) {
        for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
            SDNode &root = *owned;
            FullSplatMatch match;
            if (!matchFullSplat(root, useCount, match))
                continue;

            root.setOpcode(SDOpcode::Splat);
            root.operands() = {match.scalar};
            for (SDNode *insert : match.inserts)
                if (insert != &root)
                    insert->setOpcode(SDOpcode::Invalid);
            for (SDNode *index : match.indices)
                if (useCount[index] == 1)
                    index->setOpcode(SDOpcode::Invalid);
            for (SDNode *redundant : match.redundantScalars)
                if (useCount[redundant] == 1)
                    redundant->setOpcode(SDOpcode::Invalid);
            if (match.base &&
                match.base->opcode() == SDOpcode::Constant &&
                useCount[match.base] == 1)
                match.base->setOpcode(SDOpcode::Invalid);
            changed = true;
        }
    }

    for (BasicBlock *block : functionDAG.blockOrder) {
        for (const auto &owned : functionDAG.blocks.at(block)->nodes()) {
            SDNode &root = *owned;
            if (root.opcode() != SDOpcode::Add ||
                root.resultTypes().front() != ValueType::I32)
                continue;
            SDNode *vector = nullptr;
            std::set<int> lanes;
            std::vector<SDNode *> consumed;
            std::function<bool(SDNode *, bool)> collect =
                [&](SDNode *node, bool isRoot) {
                    if (!node)
                        return false;
                    if (node->opcode() == SDOpcode::Add) {
                        if (!isRoot && useCount[node] != 1)
                            return false;
                        consumed.push_back(node);
                        return collect(node->operands()[0].node, false) &&
                               collect(node->operands()[1].node, false);
                    }
                    if (node->opcode() !=
                            SDOpcode::ExtractElement ||
                        node->operands().size() != 2 ||
                        useCount[node] != 1)
                        return false;
                    SDNode *index = node->operands()[1].node;
                    if (!index ||
                        index->opcode() != SDOpcode::Constant ||
                        index->integer < 0 || index->integer >= 4)
                        return false;
                    SDNode *source = node->operands()[0].node;
                    if (!source ||
                        source->resultTypes().front() !=
                            ValueType::V4I32 ||
                        (vector && vector != source))
                        return false;
                    vector = source;
                    lanes.insert(static_cast<int>(index->integer));
                    consumed.push_back(node);
                    return true;
                };
            if (!collect(&root, true) || lanes.size() != 4 ||
                consumed.size() != 7)
                continue;
            root.setOpcode(SDOpcode::VectorReduceAdd);
            root.operands() = {SDValue{vector, 0}};
            for (SDNode *node : consumed)
                if (node != &root)
                    node->setOpcode(SDOpcode::Invalid);
            changed = true;
        }
    }
    return changed;
}

void printSelectionDAG(const FunctionDAG &functionDAG, std::ostream &output) {
    output << "selection-dag " << functionDAG.function->name_ << " {\n";
    for (BasicBlock *block : functionDAG.blockOrder) {
        output << "  block " << block->name_ << ":\n";
        for (const auto &node : functionDAG.blocks.at(block)->nodes()) {
            output << "    t" << node->id() << " = "
                   << opcodeName(node->opcode());
            if (!node->resultTypes().empty()) {
                output << ':';
                for (ValueType type : node->resultTypes())
                    output << ' ' << typeName(type);
            }
            for (SDValue operand : node->operands())
                output << " t" << (operand.node ? operand.node->id() : 0)
                       << '.' << operand.result;
            output << '\n';
        }
    }
    output << "}\n";
}

std::string printSelectionDAG(const FunctionDAG &functionDAG) {
    std::ostringstream output;
    printSelectionDAG(functionDAG, output);
    return output.str();
}

} // namespace backend::aarch64
