#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class BasicBlock;
class Instruction;
class Loop;
class Type;
class Value;

namespace hira {

class HiraLoop;
class HiraNode;
class HiraRegion;
class HiraSequence;

using ValueId = std::uint32_t;

class HiraValue {
public:
    HiraValue(ValueId id, Type *type) : id_(id), type_(type) {}

    ValueId id() const { return id_; }
    Type *type() const { return type_; }
    HiraNode *definingNode() const { return definingNode_; }

private:
    friend class HiraNode;

    ValueId id_;
    Type *type_;
    HiraNode *definingNode_ = nullptr;
};

enum class NodeKind {
    Loop,
    If,
    Compute,
    Load,
    Store,
    Yield,
};

class HiraNode {
public:
    explicit HiraNode(NodeKind kind) : kind_(kind) {}
    virtual ~HiraNode() = default;

    HiraNode(const HiraNode &) = delete;
    HiraNode &operator=(const HiraNode &) = delete;

    NodeKind kind() const { return kind_; }
    HiraSequence *parent() const { return parent_; }
    const std::vector<HiraValue *> &operands() const { return operands_; }
    const std::vector<HiraValue *> &results() const { return results_; }

    void addOperand(HiraValue *value);
    void addResult(HiraValue *value);

private:
    friend class HiraSequence;

    NodeKind kind_;
    HiraSequence *parent_ = nullptr;
    std::vector<HiraValue *> operands_;
    std::vector<HiraValue *> results_;
};

class HiraSequence {
public:
    HiraSequence() = default;

    HiraSequence(const HiraSequence &) = delete;
    HiraSequence &operator=(const HiraSequence &) = delete;

    const std::vector<std::unique_ptr<HiraNode>> &nodes() const {
        return nodes_;
    }

    HiraNode *append(std::unique_ptr<HiraNode> node);
    HiraNode *insert(std::size_t position, std::unique_ptr<HiraNode> node);
    std::unique_ptr<HiraNode> remove(HiraNode *node);

private:
    std::vector<std::unique_ptr<HiraNode>> nodes_;
};

enum class ComputeKind {
    Add,
    Sub,
    Mul,
    And,
    Or,
    Xor,
    Shl,
    LShr,
    AShr,
    ICmp,
    Select,
};

class HiraComputeOp final : public HiraNode {
public:
    explicit HiraComputeOp(ComputeKind computeKind)
        : HiraNode(NodeKind::Compute), computeKind_(computeKind) {}

    ComputeKind computeKind() const { return computeKind_; }

private:
    ComputeKind computeKind_;
};

class HiraLoad final : public HiraNode {
public:
    explicit HiraLoad(HiraValue *address) : HiraNode(NodeKind::Load) {
        addOperand(address);
    }

    HiraValue *address() const { return operands().front(); }
};

class HiraStore final : public HiraNode {
public:
    HiraStore(HiraValue *value, HiraValue *address)
        : HiraNode(NodeKind::Store) {
        addOperand(value);
        addOperand(address);
    }

    HiraValue *value() const { return operands()[0]; }
    HiraValue *address() const { return operands()[1]; }
};

class HiraYield final : public HiraNode {
public:
    HiraYield() : HiraNode(NodeKind::Yield) {}
};

class HiraIf final : public HiraNode {
public:
    explicit HiraIf(HiraValue *condition) : HiraNode(NodeKind::If) {
        addOperand(condition);
    }

    HiraValue *condition() const { return operands().front(); }
    HiraSequence &thenSequence() { return thenSequence_; }
    HiraSequence &elseSequence() { return elseSequence_; }
    const HiraSequence &thenSequence() const { return thenSequence_; }
    const HiraSequence &elseSequence() const { return elseSequence_; }

private:
    HiraSequence thenSequence_;
    HiraSequence elseSequence_;
};

class HiraLoop final : public HiraNode {
public:
    HiraLoop(HiraValue *induction, HiraValue *lowerBound,
             HiraValue *upperBound, HiraValue *step)
        : HiraNode(NodeKind::Loop), induction_(induction),
          lowerBound_(lowerBound), upperBound_(upperBound), step_(step) {
        addResult(induction);
        addOperand(lowerBound);
        addOperand(upperBound);
        addOperand(step);
    }

    HiraValue *induction() const { return induction_; }
    HiraValue *lowerBound() const { return lowerBound_; }
    HiraValue *upperBound() const { return upperBound_; }
    HiraValue *step() const { return step_; }
    HiraSequence &body() { return body_; }
    const HiraSequence &body() const { return body_; }

    const std::vector<HiraValue *> &carriedValues() const {
        return carriedValues_;
    }
    const std::vector<HiraValue *> &yieldValues() const {
        return yieldValues_;
    }
    void addCarriedValue(HiraValue *value);
    void addYieldValue(HiraValue *value);

private:
    HiraValue *induction_;
    HiraValue *lowerBound_;
    HiraValue *upperBound_;
    HiraValue *step_;
    HiraSequence body_;
    std::vector<HiraValue *> carriedValues_;
    std::vector<HiraValue *> yieldValues_;
};

class SourceMapping {
public:
    void mapValue(HiraValue *hiraValue, ::Value *llvmValue);
    void mapNode(HiraNode *hiraNode, Instruction *llvmInstruction);
    void mapLoop(HiraLoop *hiraLoop, Loop *llvmLoop);

    ::Value *sourceValue(const HiraValue *value) const;
    HiraValue *hiraValue(const ::Value *value) const;
    Instruction *sourceInstruction(const HiraNode *node) const;
    HiraNode *hiraNode(const Instruction *instruction) const;
    Loop *sourceLoop(const HiraLoop *loop) const;
    HiraLoop *hiraLoop(const Loop *loop) const;

private:
    std::map<const HiraValue *, ::Value *> valueToSource_;
    std::map<const ::Value *, HiraValue *> sourceToValue_;
    std::map<const HiraNode *, Instruction *> nodeToSource_;
    std::map<const Instruction *, HiraNode *> sourceToNode_;
    std::map<const HiraLoop *, Loop *> loopToSource_;
    std::map<const Loop *, HiraLoop *> sourceToLoop_;
};

class HiraRegion {
public:
    explicit HiraRegion(Loop *sourceLoop = nullptr);

    HiraRegion(const HiraRegion &) = delete;
    HiraRegion &operator=(const HiraRegion &) = delete;

    HiraValue *createValue(Type *type);
    void addParameter(HiraValue *value);
    void addResult(HiraValue *value);

    const std::vector<HiraValue *> &parameters() const { return parameters_; }
    const std::vector<HiraValue *> &results() const { return results_; }
    HiraSequence &rootSequence() { return rootSequence_; }
    const HiraSequence &rootSequence() const { return rootSequence_; }
    SourceMapping &sourceMapping() { return sourceMapping_; }
    const SourceMapping &sourceMapping() const { return sourceMapping_; }

    Loop *sourceLoop() const { return sourceLoop_; }
    BasicBlock *sourcePreheader() const { return sourcePreheader_; }
    const std::vector<BasicBlock *> &sourceExits() const { return sourceExits_; }

    bool modified() const { return modified_; }
    void markModified() { modified_ = true; }

private:
    ValueId nextValueId_ = 0;
    std::vector<std::unique_ptr<HiraValue>> values_;
    std::vector<HiraValue *> parameters_;
    std::vector<HiraValue *> results_;
    HiraSequence rootSequence_;
    SourceMapping sourceMapping_;
    Loop *sourceLoop_ = nullptr;
    BasicBlock *sourcePreheader_ = nullptr;
    std::vector<BasicBlock *> sourceExits_;
    bool modified_ = false;
};

} // namespace hira
