#include "../../../include/mid/hira/transform/loopNativeUnroll.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/target/a53TargetModel.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace hira {
namespace {

constexpr std::size_t kMaxBodyNodes = 20;
constexpr std::size_t kFactorFourBodyLimit = 14;

std::optional<std::size_t> nodePosition(
    const HiraSequence &sequence, const HiraNode *node) {
    for (std::size_t index = 0;
         index < sequence.nodes().size(); ++index)
        if (sequence.nodes()[index].get() == node)
            return index;
    return std::nullopt;
}

HiraComputeOp *appendCompute(
    HiraSequence &sequence, ComputeKind kind,
    HiraValue *result,
    const std::vector<HiraValue *> &operands,
    int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        owner->addOperand(operand);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.append(std::move(owner)));
}

HiraComputeOp *insertCompute(
    HiraSequence &sequence, std::size_t position,
    ComputeKind kind, HiraValue *result,
    const std::vector<HiraValue *> &operands,
    int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        owner->addOperand(operand);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.insert(position, std::move(owner)));
}

bool hasNestedLoop(const HiraSequence &sequence) {
    for (const auto &owner : sequence.nodes())
        if (dynamic_cast<const HiraLoop *>(owner.get()))
            return true;
    return false;
}

std::size_t countPayloadNodes(const HiraSequence &sequence) {
    std::size_t count = 0;
    for (const auto &owner : sequence.nodes()) {
        if (dynamic_cast<const HiraYield *>(owner.get()))
            continue;
        if (dynamic_cast<const HiraLoop *>(owner.get()))
            return kMaxBodyNodes + 1;
        ++count;
        if (auto *condition =
                dynamic_cast<const HiraIf *>(owner.get())) {
            if (hasNestedLoop(condition->thenSequence()) ||
                hasNestedLoop(condition->elseSequence()))
                return kMaxBodyNodes + 1;
            count += countPayloadNodes(
                          condition->thenSequence()) +
                      countPayloadNodes(
                          condition->elseSequence());
        }
        if (count > kMaxBodyNodes)
            return count;
    }
    return count;
}

std::optional<std::int64_t> constantTripCount(
    const HiraLoop &loop) {
    if (!loop.lowerBound() || !loop.upperBound() ||
        !loop.step())
        return std::nullopt;
    if (loop.lowerBound()->kind() !=
            ValueKind::IntegerConstant ||
        loop.upperBound()->kind() !=
            ValueKind::IntegerConstant ||
        loop.step()->kind() != ValueKind::IntegerConstant)
        return std::nullopt;
    const std::int64_t lower =
        loop.lowerBound()->integerValue();
    const std::int64_t upper =
        loop.upperBound()->integerValue();
    const std::int64_t step = loop.step()->integerValue();
    if (step <= 0 || upper < lower)
        return 0;
    return (upper - lower + step - 1) / step;
}

bool cloneableNode(const HiraNode &node) {
    if (dynamic_cast<const HiraComputeOp *>(&node) ||
        dynamic_cast<const HiraLoad *>(&node) ||
        dynamic_cast<const HiraStore *>(&node))
        return true;
    if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
        for (const auto &owner :
             condition->thenSequence().nodes())
            if (!cloneableNode(*owner))
                return false;
        for (const auto &owner :
             condition->elseSequence().nodes())
            if (!cloneableNode(*owner))
                return false;
        return true;
    }
    return false;
}

bool clonePayloadNode(
    HiraRegion &region, HiraSequence &target,
    const HiraNode &source,
    const std::map<const HiraValue *, HiraValue *> &mapped) {
    if (auto *compute =
            dynamic_cast<const HiraComputeOp *>(&source)) {
        HiraValue *result =
            region.createValue(
                compute->results().front()->type());
        std::vector<HiraValue *> operands;
        operands.reserve(compute->operands().size());
        for (HiraValue *operand : compute->operands()) {
            auto found = mapped.find(operand);
            operands.push_back(
                found == mapped.end() ? operand
                                      : found->second);
        }
        appendCompute(
            target, compute->computeKind(), result,
            operands, compute->predicate());
        return true;
    }
    if (auto *load = dynamic_cast<const HiraLoad *>(&source)) {
        auto found = mapped.find(load->address());
        HiraValue *address =
            found == mapped.end() ? load->address()
                                  : found->second;
        HiraValue *result =
            region.createValue(load->results().front()->type());
        auto owner = std::make_unique<HiraLoad>(address);
        owner->addResult(result);
        target.append(std::move(owner));
        return true;
    }
    if (auto *store =
            dynamic_cast<const HiraStore *>(&source)) {
        auto valueFound = mapped.find(store->value());
        auto addressFound = mapped.find(store->address());
        HiraValue *value =
            valueFound == mapped.end() ? store->value()
                                       : valueFound->second;
        HiraValue *address =
            addressFound == mapped.end()
                ? store->address()
                : addressFound->second;
        target.append(std::make_unique<HiraStore>(
            value, address));
        return true;
    }
    if (auto *condition =
            dynamic_cast<const HiraIf *>(&source)) {
        auto found = mapped.find(condition->condition());
        HiraValue *mappedCondition =
            found == mapped.end() ? condition->condition()
                                  : found->second;
        auto owner =
            std::make_unique<HiraIf>(mappedCondition);
        HiraIf *clone = owner.get();
        target.append(std::move(owner));
        for (const auto &nodeOwner :
             condition->thenSequence().nodes())
            if (!clonePayloadNode(
                    region, clone->thenSequence(), *nodeOwner,
                    mapped))
                return false;
        for (const auto &nodeOwner :
             condition->elseSequence().nodes())
            if (!clonePayloadNode(
                    region, clone->elseSequence(), *nodeOwner,
                    mapped))
                return false;
        for (const HiraIf::ResultBinding &binding :
             condition->resultBindings()) {
            auto thenFound = mapped.find(binding.thenValue);
            auto elseFound = mapped.find(binding.elseValue);
            HiraValue *thenValue =
                thenFound == mapped.end()
                    ? binding.thenValue
                    : thenFound->second;
            HiraValue *elseValue =
                elseFound == mapped.end()
                    ? binding.elseValue
                    : elseFound->second;
            HiraValue *result =
                region.createValue(binding.result->type());
            clone->addResultBinding(
                thenValue, elseValue, result);
        }
        return true;
    }
    return false;
}

std::optional<std::uint32_t> chooseFactor(
    std::size_t bodyNodes) {
    if (bodyNodes == 0 || bodyNodes > kMaxBodyNodes)
        return std::nullopt;
    if (bodyNodes <= kFactorFourBodyLimit)
        return target::cortexA53().float32Lanes;
    return 2;
}

bool tryUnrollLoop(HiraRegion &region, HiraLoop &loop) {
    if (loop.role() != HiraLoop::Role::Ordinary &&
        loop.role() != HiraLoop::Role::ScalarRemainder)
        return false;
    if (!loop.carriedValues().empty())
        return false;
    if (!loop.lowerBound() || !loop.upperBound() ||
        !loop.step())
        return false;
    if (loop.lowerBound()->kind() !=
            ValueKind::IntegerConstant ||
        loop.step()->kind() != ValueKind::IntegerConstant ||
        loop.lowerBound()->integerValue() != 0 ||
        loop.step()->integerValue() != 1)
        return false;

    const auto &nodes = loop.body().nodes();
    if (nodes.size() < 2)
        return false;
    if (!dynamic_cast<const HiraYield *>(nodes.back().get()))
        return false;
    if (!dynamic_cast<const HiraComputeOp *>(
            nodes[nodes.size() - 2].get()))
        return false;

    std::vector<const HiraNode *> payload;
    payload.reserve(nodes.size() - 2);
    for (std::size_t index = 0; index + 2 < nodes.size();
         ++index) {
        if (!cloneableNode(*nodes[index]))
            return false;
        payload.push_back(nodes[index].get());
    }
    const std::size_t bodyNodes = countPayloadNodes(loop.body());
    const std::optional<std::uint32_t> factor =
        chooseFactor(bodyNodes);
    if (!factor || *factor < 2)
        return false;

    const std::optional<std::int64_t> trip =
        constantTripCount(loop);
    if (!trip || *trip < static_cast<std::int64_t>(*factor) ||
        (*trip % static_cast<std::int64_t>(*factor)) != 0)
        return false;

    HiraSequence *parent = loop.parent();
    auto position = parent ? nodePosition(*parent, &loop)
                           : std::nullopt;
    if (!parent || !position)
        return false;

    auto *indexType =
        dynamic_cast<IntegerType *>(loop.induction()->type());
    if (!indexType || indexType->num_bits_ != 32)
        return false;

    HiraValue *oldInduction = loop.induction();
    HiraValue *oldUpper = loop.upperBound();
    HiraValue *oldLower = loop.lowerBound();
    HiraValue *mainStep = region.createIntegerConstant(
        indexType, static_cast<std::int64_t>(*factor));
    HiraValue *mainInduction = region.createValue(indexType);
    auto mainOwner = std::make_unique<HiraLoop>(
        mainInduction, oldLower, oldUpper, mainStep);
    HiraLoop *mainLoop = mainOwner.get();
    HiraSequence &mainBody = mainLoop->body();
    for (std::uint32_t lane = 0; lane < *factor; ++lane) {
        std::map<const HiraValue *, HiraValue *> mapped;
        HiraValue *laneInduction = mainInduction;
        if (lane) {
            HiraValue *laneOffset = region.createIntegerConstant(
                indexType, static_cast<std::int64_t>(lane));
            laneInduction = region.createValue(indexType);
            appendCompute(
                mainBody, ComputeKind::Add, laneInduction,
                {mainInduction, laneOffset});
        }
        mapped[oldInduction] = laneInduction;
        for (const HiraNode *node : payload)
            if (!clonePayloadNode(region, mainBody, *node,
                                  mapped))
                return false;
    }

    HiraValue *next = region.createValue(indexType);
    appendCompute(
        mainBody, ComputeKind::Add, next,
        {mainInduction, mainStep});
    mainLoop->addYieldValue(next);
    parent->insert(*position, std::move(mainOwner));
    parent->remove(&loop);
    return true;
}

bool walkSequence(HiraRegion &region, HiraSequence &sequence,
                  std::size_t &unrolled) {
    bool changed = false;
    for (std::size_t index = 0;
         index < sequence.nodes().size(); ++index) {
        if (auto *loop =
                dynamic_cast<HiraLoop *>(
                    sequence.nodes()[index].get())) {
            changed |= walkSequence(region, loop->body(),
                                    unrolled);
            if (tryUnrollLoop(region, *loop)) {
                ++unrolled;
                changed = true;
                --index;
            }
        } else if (auto *condition =
                       dynamic_cast<HiraIf *>(
                           sequence.nodes()[index].get())) {
            changed |= walkSequence(
                region, condition->thenSequence(), unrolled);
            changed |= walkSequence(
                region, condition->elseSequence(), unrolled);
        }
    }
    return changed;
}

} // namespace

LoopNativeUnrollResult unrollCountedLoops(HiraRegion &region) {
    LoopNativeUnrollResult result;
    result.changed = walkSequence(
        region, region.rootSequence(), result.loops);
    return result;
}

} // namespace hira
