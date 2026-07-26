#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

enum class ValueKind {
    Temporary,
    Parameter,
    Scratch,
    IntegerConstant,
    FloatConstant,
};

class HiraValue {
public:
    HiraValue(ValueId id, Type *type, ValueKind kind = ValueKind::Temporary)
        : id_(id), type_(type), kind_(kind) {}

    ValueId id() const { return id_; }
    Type *type() const { return type_; }
    ValueKind kind() const { return kind_; }
    Type *allocatedType() const { return allocatedType_; }
    std::int64_t integerValue() const { return integerValue_; }
    float floatValue() const { return floatValue_; }
    HiraNode *definingNode() const { return definingNode_; }

private:
    friend class HiraRegion;
    friend class HiraNode;

    ValueId id_;
    Type *type_;
    ValueKind kind_;
    Type *allocatedType_ = nullptr;
    std::int64_t integerValue_ = 0;
    float floatValue_ = 0.0f;
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
    void replaceOperand(std::size_t index, HiraValue *value);
    void replaceResult(std::size_t index, HiraValue *value);
    void clearResults();

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
    SDiv,
    SRem,
    UDiv,
    URem,
    FAdd,
    FSub,
    FMul,
    FDiv,
    And,
    Or,
    Xor,
    Shl,
    LShr,
    AShr,
    ICmp,
    Select,
    GetElementPtr,
    ZExt,
    BitCast,
    Splat,
    ExtractElement,
};

class HiraComputeOp final : public HiraNode {
public:
    explicit HiraComputeOp(ComputeKind computeKind, int predicate = 0)
        : HiraNode(NodeKind::Compute), computeKind_(computeKind),
          predicate_(predicate) {}

    ComputeKind computeKind() const { return computeKind_; }
    int predicate() const { return predicate_; }

private:
    ComputeKind computeKind_;
    int predicate_;
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
    struct ResultBinding {
        HiraValue *thenValue = nullptr;
        HiraValue *elseValue = nullptr;
        HiraValue *result = nullptr;
    };

    explicit HiraIf(HiraValue *condition) : HiraNode(NodeKind::If) {
        addOperand(condition);
    }

    HiraValue *condition() const { return operands().front(); }
    void addResultBinding(HiraValue *thenValue,
                          HiraValue *elseValue,
                          HiraValue *result) {
        bindings_.push_back({thenValue, elseValue, result});
        addOperand(thenValue);
        addOperand(elseValue);
        addResult(result);
    }
    const std::vector<ResultBinding> &resultBindings() const {
        return bindings_;
    }
    std::vector<ResultBinding> releaseResultBindings() {
        std::vector<ResultBinding> result = std::move(bindings_);
        clearResults();
        return result;
    }
    HiraSequence &thenSequence() { return thenSequence_; }
    HiraSequence &elseSequence() { return elseSequence_; }
    const HiraSequence &thenSequence() const { return thenSequence_; }
    const HiraSequence &elseSequence() const { return elseSequence_; }

private:
    HiraSequence thenSequence_;
    HiraSequence elseSequence_;
    std::vector<ResultBinding> bindings_;
};

class HiraLoop final : public HiraNode {
public:
    enum class Role {
        Ordinary,
        VectorMain,
        ScalarRemainder,
        Parallel,
        RepetitionFolded,
    };

    struct CarriedBinding {
        HiraValue *initial = nullptr;
        HiraValue *iteration = nullptr;
        HiraValue *yielded = nullptr;
        HiraValue *result = nullptr;
    };

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
    void setLowerBound(HiraValue *value) {
        lowerBound_ = value;
        replaceOperand(0, value);
    }
    void setUpperBound(HiraValue *value) {
        upperBound_ = value;
        replaceOperand(1, value);
    }
    HiraSequence &body() { return body_; }
    const HiraSequence &body() const { return body_; }

    const std::vector<CarriedBinding> &carriedValues() const {
        return carriedValues_;
    }
    const std::vector<HiraValue *> &yieldValues() const {
        return yieldValues_;
    }
    std::size_t addCarriedValue(HiraValue *initial, HiraValue *iteration,
                                HiraValue *result);
    void setCarriedInitial(std::size_t index, HiraValue *value);
    void setCarriedResult(std::size_t index, HiraValue *value);
    void setCarriedYield(std::size_t index, HiraValue *value);
    void addYieldValue(HiraValue *value);
    void setYieldValue(std::size_t index, HiraValue *value);
    Role role() const { return role_; }
    void setRole(Role role) { role_ = role; }

private:
    HiraValue *induction_;
    HiraValue *lowerBound_;
    HiraValue *upperBound_;
    HiraValue *step_;
    HiraSequence body_;
    std::vector<CarriedBinding> carriedValues_;
    std::vector<HiraValue *> yieldValues_;
    Role role_ = Role::Ordinary;
};

class SourceMapping {
public:
    void mapValue(HiraValue *hiraValue, ::Value *llvmValue);
    void mapNode(HiraNode *hiraNode, Instruction *llvmInstruction);
    void mapLoop(HiraLoop *hiraLoop, Loop *llvmLoop);
    void unmapNode(HiraNode *hiraNode);
    void unmapLoop(HiraLoop *hiraLoop);

    ::Value *sourceValue(const HiraValue *value) const;
    HiraValue *hiraValue(const ::Value *value) const;
    Instruction *sourceInstruction(const HiraNode *node) const;
    HiraNode *hiraNode(const Instruction *instruction) const;
    Loop *sourceLoop(const HiraLoop *loop) const;
    HiraLoop *hiraLoop(const Loop *loop) const;

private:
    std::map<const HiraValue *, ::Value *> valueToSource_;
    std::map<const ::Value *, std::vector<HiraValue *>> sourceToValues_;
    std::map<const HiraNode *, Instruction *> nodeToSource_;
    std::map<const Instruction *, HiraNode *> sourceToNode_;
    std::map<const HiraLoop *, Loop *> loopToSource_;
    std::map<const Loop *, HiraLoop *> sourceToLoop_;
};

enum class ParallelReductionOp {
    BitAnd,
    BitOr,
    BitXor,
};

// One exact reduction carried by the parallel outer band.  Each worker
// accumulates a private partial starting from the identity; the partials are
// combined in band order once all workers join, which reproduces the
// sequential result exactly for associative operators.
struct HiraParallelReduction {
    std::size_t carriedIndex = 0;
    ParallelReductionOp op = ParallelReductionOp::BitXor;
    std::int64_t identity = 0;
};

// Lowering plan for a proven-parallel outer band: the band loop runs inside a
// worker body function over a [lo, hi) chunk while the source function calls
// the parallel runtime with the full bounds.
struct HiraParallelPlan {
    HiraLoop *loop = nullptr;
    int bodyId = 0;
    std::vector<HiraParallelReduction> reductions;
    std::vector<HiraValue *> privateParameters;
};

class HiraRegion {
public:
    explicit HiraRegion(Loop *sourceLoop = nullptr);

    HiraRegion(const HiraRegion &) = delete;
    HiraRegion &operator=(const HiraRegion &) = delete;

    HiraValue *createValue(Type *type);
    HiraValue *createParameter(Type *type);
    HiraValue *createScratch(Type *allocatedType);
    HiraValue *createIntegerConstant(Type *type, std::int64_t value);
    HiraValue *createFloatConstant(Type *type, float value);
    void addParameter(HiraValue *value);
    void addResult(HiraValue *value);

    const std::vector<HiraValue *> &parameters() const { return parameters_; }
    const std::vector<HiraValue *> &scratches() const { return scratches_; }
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

    const HiraParallelPlan *parallelPlan() const {
        return parallelPlan_ ? &*parallelPlan_ : nullptr;
    }
    void setParallelPlan(HiraParallelPlan plan) {
        parallelPlan_ = std::move(plan);
    }

private:
    ValueId nextValueId_ = 0;
    std::vector<std::unique_ptr<HiraValue>> values_;
    std::vector<HiraValue *> parameters_;
    std::vector<HiraValue *> scratches_;
    std::vector<HiraValue *> results_;
    HiraSequence rootSequence_;
    SourceMapping sourceMapping_;
    Loop *sourceLoop_ = nullptr;
    BasicBlock *sourcePreheader_ = nullptr;
    std::vector<BasicBlock *> sourceExits_;
    bool modified_ = false;
    std::optional<HiraParallelPlan> parallelPlan_;
};

} // namespace hira
