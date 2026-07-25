#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"

namespace hira {
namespace {

bool isSameIntegerValue(
    const HiraValue *left, const HiraValue *right) {
    if (left == right)
        return true;
    return left && right &&
           left->kind() == ValueKind::IntegerConstant &&
           right->kind() == ValueKind::IntegerConstant &&
           left->integerValue() == right->integerValue();
}

} // namespace

std::optional<CanonicalLoopControl>
analyzeCanonicalLoopControl(const HiraLoop &loop) {
    const auto &nodes = loop.body().nodes();
    if (nodes.size() < 2 || !loop.carriedValues().empty() ||
        loop.yieldValues().size() != 1)
        return std::nullopt;

    auto *update = dynamic_cast<const HiraComputeOp *>(
        nodes[nodes.size() - 2].get());
    auto *yield =
        dynamic_cast<const HiraYield *>(nodes.back().get());
    if (!update || !yield ||
        update->computeKind() != ComputeKind::Add ||
        update->operands().size() != 2 ||
        update->results().size() != 1 ||
        yield->operands().size() != 1 ||
        yield->operands().front() !=
            update->results().front() ||
        loop.yieldValues().front() !=
            update->results().front())
        return std::nullopt;

    const HiraValue *left = update->operands()[0];
    const HiraValue *right = update->operands()[1];
    if (!((left == loop.induction() &&
           isSameIntegerValue(right, loop.step())) ||
          (right == loop.induction() &&
           isSameIntegerValue(left, loop.step()))))
        return std::nullopt;
    return CanonicalLoopControl{update, yield};
}

bool isPerfectLoopNest(
    const HiraLoop &outer, const HiraLoop &inner) {
    const auto &nodes = outer.body().nodes();
    return inner.parent() == &outer.body() &&
           nodes.size() == 3 &&
           nodes.front().get() == &inner;
}

} // namespace hira
