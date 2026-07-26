#include "../../../include/mid/hira/transform/conditionalIfConversion.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <memory>
#include <vector>

namespace hira {
namespace {

bool isSafeSpeculativeCompute(const HiraNode &node) {
    auto *compute = dynamic_cast<const HiraComputeOp *>(&node);
    if (!compute)
        return false;
    switch (compute->computeKind()) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::FAdd:
    case ComputeKind::FSub:
    case ComputeKind::FMul:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
    case ComputeKind::Shl:
    case ComputeKind::LShr:
    case ComputeKind::AShr:
    case ComputeKind::ICmp:
    case ComputeKind::Select:
    case ComputeKind::GetElementPtr:
    case ComputeKind::ZExt:
    case ComputeKind::BitCast:
    case ComputeKind::Splat:
        return true;
    default:
        return false;
    }
}

bool isPureResultDiamond(const HiraIf &condition) {
    if (condition.resultBindings().empty())
        return false;
    for (const auto &node : condition.thenSequence().nodes())
        if (!isSafeSpeculativeCompute(*node))
            return false;
    for (const auto &node : condition.elseSequence().nodes())
        if (!isSafeSpeculativeCompute(*node))
            return false;
    return true;
}

bool convertSequence(HiraRegion &region, HiraSequence &sequence) {
    bool changed = false;
    for (std::size_t index = 0;
         index < sequence.nodes().size();) {
        HiraNode *node = sequence.nodes()[index].get();
        if (auto *loop = dynamic_cast<HiraLoop *>(node)) {
            changed |= convertSequence(region, loop->body());
            ++index;
            continue;
        }
        auto *condition = dynamic_cast<HiraIf *>(node);
        if (!condition || !isPureResultDiamond(*condition)) {
            if (condition) {
                changed |= convertSequence(
                    region, condition->thenSequence());
                changed |= convertSequence(
                    region, condition->elseSequence());
            }
            ++index;
            continue;
        }

        HiraValue *guard = condition->condition();
        std::vector<HiraIf::ResultBinding> bindings =
            condition->releaseResultBindings();
        auto moveArm = [&](HiraSequence &arm) {
            while (!arm.nodes().empty()) {
                HiraNode *front = arm.nodes().front().get();
                sequence.insert(index++, arm.remove(front));
            }
        };
        moveArm(condition->thenSequence());
        moveArm(condition->elseSequence());
        for (const HiraIf::ResultBinding &binding : bindings) {
            auto select =
                std::make_unique<HiraComputeOp>(
                    ComputeKind::Select);
            select->addOperand(guard);
            select->addOperand(binding.thenValue);
            select->addOperand(binding.elseValue);
            select->addResult(binding.result);
            sequence.insert(index++, std::move(select));
        }
        region.sourceMapping().unmapNode(condition);
        sequence.remove(condition);
        changed = true;
    }
    return changed;
}

} // namespace

bool convertPureConditionals(HiraRegion &region) {
    bool changed =
        convertSequence(region, region.rootSequence());
    if (changed)
        region.markModified();
    return changed;
}

} // namespace hira
