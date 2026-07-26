#include "../../../include/mid/hira/analysis/loopInvarianceAnalysis.hpp"

#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"

namespace hira {
namespace {

bool isSafePureCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::FAdd:
    case ComputeKind::FSub:
    case ComputeKind::FMul:
    case ComputeKind::FDiv:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
    case ComputeKind::ICmp:
    case ComputeKind::Select:
    case ComputeKind::GetElementPtr:
    case ComputeKind::BitCast:
        return true;
    case ComputeKind::SDiv:
    case ComputeKind::SRem:
    case ComputeKind::UDiv:
    case ComputeKind::URem:
    case ComputeKind::Shl:
    case ComputeKind::LShr:
    case ComputeKind::AShr:
    case ComputeKind::ZExt:
    case ComputeKind::Splat:
        return false;
    }
    return false;
}

const HiraValue *underlyingAddress(const HiraValue *value) {
    const HiraValue *current = value;
    std::set<const HiraValue *> visited;
    while (current && visited.insert(current).second) {
        auto *compute = dynamic_cast<const HiraComputeOp *>(
            current->definingNode());
        if (!compute ||
            (compute->computeKind() !=
                 ComputeKind::GetElementPtr &&
             compute->computeKind() != ComputeKind::BitCast) ||
            compute->operands().empty())
            break;
        current = compute->operands().front();
    }
    return current;
}

} // namespace

LoopInvarianceAnalysis::LoopInvarianceAnalysis(
    const HiraLoop &loop,
    const SourceMapping *sourceMapping,
    const ::ArgumentAliasAnalysis *aliasAnalysis)
    : loop_(loop), sourceMapping_(sourceMapping),
      aliasAnalysis_(aliasAnalysis) {
    internalNodes_.insert(&loop_);
    collectSequence(loop_.body());
}

void LoopInvarianceAnalysis::collectSequence(
    const HiraSequence &sequence) {
    for (const auto &node : sequence.nodes())
        collectNode(*node);
}

void LoopInvarianceAnalysis::collectNode(const HiraNode &node) {
    internalNodes_.insert(&node);
    if (auto *store = dynamic_cast<const HiraStore *>(&node))
        storedBases_.insert(
            underlyingAddress(store->address()));
    if (auto *loop = dynamic_cast<const HiraLoop *>(&node)) {
        collectSequence(loop->body());
        return;
    }
    if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
        collectSequence(condition->thenSequence());
        collectSequence(condition->elseSequence());
    }
}

bool LoopInvarianceAnalysis::isInvariant(
    const HiraValue *value,
    const std::set<const HiraNode *> &assumedInvariant) const {
    if (!value)
        return false;
    if (value->kind() == ValueKind::IntegerConstant ||
        value->kind() == ValueKind::FloatConstant ||
        value->kind() == ValueKind::Parameter)
        return true;

    const HiraNode *definition = value->definingNode();
    if (!definition)
        return false;
    return !internalNodes_.count(definition) ||
           assumedInvariant.count(definition);
}

bool LoopInvarianceAnalysis::isHoistable(
    const HiraNode &node,
    const std::set<const HiraNode *> &assumedInvariant) const {
    if (auto *compute =
            dynamic_cast<const HiraComputeOp *>(&node)) {
        if (compute->results().size() != 1 ||
            !isSafePureCompute(compute->computeKind()))
            return false;
        for (const HiraValue *operand : compute->operands())
            if (!isInvariant(operand, assumedInvariant))
                return false;
        return true;
    }

    auto *load = dynamic_cast<const HiraLoad *>(&node);
    if (!load || load->results().size() != 1 ||
        !isInvariant(load->address(), assumedInvariant))
        return false;
    const HiraValue *loadBase =
        underlyingAddress(load->address());
    for (const HiraValue *storeBase : storedBases_) {
        if (!loadBase || !storeBase || loadBase == storeBase)
            return false;
        if (!sourceMapping_ || !aliasAnalysis_)
            return false;
        ::Value *loadSource =
            sourceMapping_->sourceValue(loadBase);
        ::Value *storeSource =
            sourceMapping_->sourceValue(storeBase);
        if (!loadSource || !storeSource ||
            !aliasAnalysis_->noAlias(loadSource, storeSource))
            return false;
    }
    return true;
}

} // namespace hira
