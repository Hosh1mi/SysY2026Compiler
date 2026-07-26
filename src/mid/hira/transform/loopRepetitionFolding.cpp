#include "../../../include/mid/hira/transform/loopRepetitionFolding.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <set>

namespace hira {
namespace {

struct LoopControl {
    HiraComputeOp *inductionUpdate = nullptr;
    HiraYield *yield = nullptr;
};

std::optional<LoopControl> loopControl(HiraLoop &loop) {
    const auto &nodes = loop.body().nodes();
    if (nodes.size() < 2 ||
        loop.yieldValues().size() !=
            loop.carriedValues().size() + 1)
        return std::nullopt;
    auto *update = dynamic_cast<HiraComputeOp *>(
        nodes[nodes.size() - 2].get());
    auto *yield =
        dynamic_cast<HiraYield *>(nodes.back().get());
    if (!update || !yield ||
        update->computeKind() != ComputeKind::Add ||
        update->operands().size() != 2 ||
        update->results().size() != 1 ||
        yield->operands().size() !=
            loop.yieldValues().size() ||
        yield->operands().front() !=
            update->results().front())
        return std::nullopt;
    HiraValue *left = update->operands()[0];
    HiraValue *right = update->operands()[1];
    if (!((left == loop.induction() &&
           right == loop.step()) ||
          (right == loop.induction() &&
           left == loop.step())))
        return std::nullopt;
    return LoopControl{update, yield};
}

std::size_t countUses(const HiraSequence &sequence,
                      const HiraValue *value) {
    std::size_t uses = 0;
    for (const auto &owner : sequence.nodes()) {
        for (const HiraValue *operand : owner->operands())
            uses += operand == value;
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(owner.get()))
            uses += countUses(loop->body(), value);
        else if (auto *condition =
                     dynamic_cast<const HiraIf *>(owner.get())) {
            uses += countUses(condition->thenSequence(), value);
            uses += countUses(condition->elseSequence(), value);
        }
    }
    return uses;
}

bool hasMemoryWriteOrConditional(
    const HiraSequence &sequence) {
    for (const auto &owner : sequence.nodes()) {
        if (dynamic_cast<const HiraStore *>(owner.get()) ||
            dynamic_cast<const HiraIf *>(owner.get()))
            return true;
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(owner.get()))
            if (hasMemoryWriteOrConditional(loop->body()))
                return true;
    }
    return false;
}

bool dependsOn(const HiraValue *value,
               const HiraValue *target,
               std::set<const HiraValue *> &active) {
    if (value == target)
        return true;
    if (!value ||
        value->kind() == ValueKind::Parameter ||
        value->kind() == ValueKind::IntegerConstant ||
        value->kind() == ValueKind::FloatConstant)
        return false;
    if (!active.insert(value).second)
        return false;
    const HiraNode *definition = value->definingNode();
    bool dependent = false;
    if (auto *compute =
            dynamic_cast<const HiraComputeOp *>(definition)) {
        for (const HiraValue *operand : compute->operands())
            dependent |= dependsOn(operand, target, active);
    } else if (auto *load =
                   dynamic_cast<const HiraLoad *>(definition)) {
        dependent =
            dependsOn(load->address(), target, active);
    } else if (auto *loop =
                   dynamic_cast<const HiraLoop *>(definition)) {
        if (value != loop->induction()) {
            for (const auto &binding :
                 loop->carriedValues()) {
                if (value == binding.result)
                    dependent |=
                        dependsOn(binding.initial, target,
                                  active) ||
                        dependsOn(binding.yielded, target,
                                  active);
            }
        }
    } else {
        dependent = definition != nullptr;
    }
    active.erase(value);
    return dependent;
}

bool dependsOn(const HiraValue *value,
               const HiraValue *target) {
    std::set<const HiraValue *> active;
    return dependsOn(value, target, active);
}

bool additiveCarriedChain(HiraLoop &loop) {
    if (loop.carriedValues().size() != 1 ||
        !loopControl(loop))
        return false;
    const HiraLoop::CarriedBinding &binding =
        loop.carriedValues().front();
    if (!binding.initial || !binding.iteration ||
        !binding.yielded || !binding.result ||
        !dynamic_cast<IntegerType *>(
            binding.iteration->type()))
        return false;

    HiraNode *definition =
        binding.yielded->definingNode();
    if (auto *compute =
            dynamic_cast<HiraComputeOp *>(definition)) {
        if (compute->computeKind() != ComputeKind::Add ||
            compute->operands().size() != 2)
            return false;
        HiraValue *increment = nullptr;
        if (compute->operands()[0] == binding.iteration)
            increment = compute->operands()[1];
        else if (compute->operands()[1] ==
                 binding.iteration)
            increment = compute->operands()[0];
        if (!increment ||
            dependsOn(increment, binding.iteration) ||
            countUses(loop.body(), binding.iteration) != 1)
            return false;
        return true;
    }

    auto *nested =
        dynamic_cast<HiraLoop *>(definition);
    if (!nested ||
        nested->carriedValues().size() != 1 ||
        nested->carriedValues().front().result !=
            binding.yielded ||
        nested->carriedValues().front().initial !=
            binding.iteration ||
        countUses(loop.body(), binding.iteration) != 1)
        return false;
    return additiveCarriedChain(*nested);
}

std::optional<std::size_t> nodePosition(
    const HiraSequence &sequence, const HiraNode *node) {
    for (std::size_t index = 0;
         index < sequence.nodes().size(); ++index)
        if (sequence.nodes()[index].get() == node)
            return index;
    return std::nullopt;
}

HiraComputeOp *insertCompute(
    HiraSequence &sequence, std::size_t position,
    ComputeKind kind, HiraValue *result,
    std::initializer_list<HiraValue *> operands,
    int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        owner->addOperand(operand);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.insert(position, std::move(owner)));
}

bool foldLoop(HiraRegion &region, HiraLoop &loop,
              Module &module) {
    auto reject = [&](const char *reason) {
        if (std::getenv("DEBUG_HIRA_REPETITION")) {
            Loop *source =
                region.sourceMapping().sourceLoop(&loop);
            std::cerr
                << "// hira.repetition_folding = rejected";
            if (source && source->header)
                std::cerr << " header="
                          << source->header->name_;
            std::cerr << " reason=" << reason << "\n";
        }
        return false;
    };
    if (loop.role() != HiraLoop::Role::Ordinary)
        return reject("transformed-loop");
    if (loop.carriedValues().size() != 1)
        return reject("unsupported-carried-values");
    if (!loop.lowerBound() ||
        loop.lowerBound()->kind() !=
            ValueKind::IntegerConstant ||
        loop.lowerBound()->integerValue() != 0)
        return reject("nonzero-lower-bound");
    if (!loop.step() ||
        loop.step()->kind() !=
            ValueKind::IntegerConstant ||
        loop.step()->integerValue() != 1)
        return reject("nonunit-step");
    if (hasMemoryWriteOrConditional(loop.body()))
        return reject("side-effect-or-conditional");
    if (!additiveCarriedChain(loop))
        return reject("non-additive-carried-chain");

    auto control = loopControl(loop);
    HiraSequence *parent = loop.parent();
    auto position =
        parent ? nodePosition(*parent, &loop)
               : std::nullopt;
    if (!control || !parent || !position)
        return reject("invalid-loop-control");

    // The repeated increment must be independent of the repetition IV.
    // The only legal use is the structural induction update.
    if (countUses(loop.body(), loop.induction()) != 1)
        return reject("repetition-iv-used-in-body");

    HiraValue *originalUpper = loop.upperBound();
    auto *indexType =
        dynamic_cast<IntegerType *>(
            loop.induction()->type());
    const auto &binding = loop.carriedValues().front();
    auto *valueType =
        dynamic_cast<IntegerType *>(
            binding.iteration->type());
    if (!indexType || indexType->num_bits_ != 32 ||
        !valueType || valueType->num_bits_ != 32 ||
        originalUpper->type() != indexType)
        return reject("unsupported-integer-type");

    HiraValue *zero =
        region.createIntegerConstant(indexType, 0);
    HiraValue *one =
        region.createIntegerConstant(indexType, 1);
    HiraValue *positive =
        region.createValue(module.int1_ty_);
    insertCompute(
        *parent, (*position)++, ComputeKind::ICmp,
        positive, {originalUpper, zero},
        ICmpInst::ICMP_SGT);
    HiraValue *singleTripUpper =
        region.createValue(indexType);
    insertCompute(
        *parent, (*position)++, ComputeKind::Select,
        singleTripUpper, {positive, one, zero});
    loop.setUpperBound(singleTripUpper);

    const std::size_t insertion =
        loop.body().nodes().size() - 2;
    HiraValue *delta =
        region.createValue(valueType);
    insertCompute(
        loop.body(), insertion, ComputeKind::Sub,
        delta, {binding.yielded, binding.iteration});
    HiraValue *scaled =
        region.createValue(valueType);
    insertCompute(
        loop.body(), insertion + 1, ComputeKind::Mul,
        scaled, {delta, originalUpper});
    HiraValue *folded =
        region.createValue(valueType);
    insertCompute(
        loop.body(), insertion + 2, ComputeKind::Add,
        folded, {binding.iteration, scaled});

    loop.setCarriedYield(0, folded);
    loop.setYieldValue(1, folded);
    control->yield->replaceOperand(1, folded);
    loop.setRole(HiraLoop::Role::RepetitionFolded);
    region.markModified();
    if (std::getenv("DEBUG_HIRA_REPETITION")) {
        Loop *source =
            region.sourceMapping().sourceLoop(&loop);
        std::cerr
            << "// hira.repetition_folding = realized";
        if (source && source->header)
            std::cerr << " header="
                      << source->header->name_;
        std::cerr << "\n";
    }
    return true;
}

bool foldSequence(HiraRegion &region,
                  HiraSequence &sequence,
                  Module &module) {
    for (const auto &owner : sequence.nodes()) {
        auto *loop = dynamic_cast<HiraLoop *>(owner.get());
        if (!loop)
            continue;
        if (foldLoop(region, *loop, module))
            return true;
        if (foldSequence(region, loop->body(), module))
            return true;
    }
    return false;
}

} // namespace

bool foldRepeatedAdditiveLoops(HiraRegion &region) {
    Loop *sourceLoop = region.sourceLoop();
    Module *module =
        sourceLoop && sourceLoop->header &&
                sourceLoop->header->parent_
            ? sourceLoop->header->parent_->parent_
            : nullptr;
    return module &&
           foldSequence(region, region.rootSequence(),
                        *module);
}

} // namespace hira
