#include "../../../include/mid/hira/analysis/loopInvarianceAnalysis.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"

namespace hira {
namespace {

bool isSafePureCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
    case ComputeKind::ICmp:
    case ComputeKind::Select:
        return true;
    case ComputeKind::Shl:
    case ComputeKind::LShr:
    case ComputeKind::AShr:
    case ComputeKind::GetElementPtr:
    case ComputeKind::ZExt:
        return false;
    }
    return false;
}

} // namespace

LoopInvarianceAnalysis::LoopInvarianceAnalysis(const HiraLoop &loop)
    : loop_(loop) {
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
    auto *compute = dynamic_cast<const HiraComputeOp *>(&node);
    if (!compute || compute->results().size() != 1 ||
        !isSafePureCompute(compute->computeKind()))
        return false;
    for (const HiraValue *operand : compute->operands())
        if (!isInvariant(operand, assumedInvariant))
            return false;
    return true;
}

} // namespace hira
