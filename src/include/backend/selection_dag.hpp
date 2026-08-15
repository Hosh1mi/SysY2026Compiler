#pragma once

#include "backend/aarch64_dag_opcodes.hpp"
#include "machine_ir.hpp"
#include "../mid/ir/ir.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

class SDNode;

struct SDValue {
    SDNode *node = nullptr;
    unsigned result = 0;

    explicit operator bool() const { return node != nullptr; }
    bool operator==(const SDValue &other) const {
        return node == other.node && result == other.result;
    }
};

class SDNode {
public:
    SDNode(unsigned id, SDOpcode opcode, std::vector<ValueType> resultTypes,
           std::vector<SDValue> operands)
        : id_(id), opcode_(opcode), resultTypes_(std::move(resultTypes)),
          operands_(std::move(operands)) {}

    unsigned id() const { return id_; }
    SDOpcode opcode() const { return opcode_; }
    void setOpcode(SDOpcode opcode) { opcode_ = opcode; }
    const std::vector<ValueType> &resultTypes() const { return resultTypes_; }
    std::vector<SDValue> &operands() { return operands_; }
    const std::vector<SDValue> &operands() const { return operands_; }

    std::int64_t integer = 0;
    std::uint32_t floatingBits = 0;
    unsigned index = 0;
    unsigned alignment = 1;
    unsigned memorySize = 0;
    int predicate = 0;
    std::string symbol;
    std::vector<int> shuffleMask;
    std::vector<unsigned> gepScales;
    std::vector<BasicBlock *> incomingBlocks;
    Instruction *origin = nullptr;

private:
    unsigned id_;
    SDOpcode opcode_;
    std::vector<ValueType> resultTypes_;
    std::vector<SDValue> operands_;
};

class SelectionDAG {
public:
    SelectionDAG();

    SDNode &createNode(SDOpcode opcode, std::vector<ValueType> resultTypes,
                       std::vector<SDValue> operands = {});
    SDValue entryToken() const { return entryToken_; }
    const std::vector<std::unique_ptr<SDNode>> &nodes() const { return nodes_; }
    std::vector<std::unique_ptr<SDNode>> &nodes() { return nodes_; }

private:
    unsigned nextNodeId_ = 0;
    std::vector<std::unique_ptr<SDNode>> nodes_;
    SDValue entryToken_;
};

struct FunctionDAG {
    explicit FunctionDAG(Function *function) : function(function) {}

    Function *function;
    bool legalized = false;
    std::vector<BasicBlock *> blockOrder;
    std::unordered_map<BasicBlock *, std::unique_ptr<SelectionDAG>> blocks;
    std::unordered_map<Value *, SDValue> values;
};

class SelectionDAGBuilder {
public:
    std::unique_ptr<FunctionDAG> build(Function *function) const;

    static ValueType valueType(Type *type);
    static unsigned typeSize(Type *type);
    SDValue getValue(FunctionDAG &functionDAG, SelectionDAG &dag,
                     Value *value) const;
};

class DAGLegalizer {
public:
    void run(FunctionDAG &functionDAG) const;
};

class DAGCombiner {
public:
    bool run(FunctionDAG &functionDAG, bool enableOptimizations) const;
};

void printSelectionDAG(const FunctionDAG &functionDAG, std::ostream &output);

} // namespace backend::aarch64
