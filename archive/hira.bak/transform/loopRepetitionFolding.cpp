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
#include <limits>
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

struct ModularRecurrence {
    HiraValue *initial = nullptr;
    HiraValue *bound = nullptr;
    HiraValue *stateResult = nullptr;
    IntegerType *type = nullptr;
    std::int64_t increment = 0;
    std::int64_t modulus = 0;
    std::int64_t safeBound = 0;
};

std::optional<ModularRecurrence>
analyzeModularRecurrence(HiraLoop &loop) {
    if (loop.role() != HiraLoop::Role::Ordinary ||
        loop.carriedValues().size() != 1 ||
        !loop.lowerBound() ||
        loop.lowerBound()->kind() !=
            ValueKind::IntegerConstant ||
        loop.lowerBound()->integerValue() != 0 ||
        !loop.step() ||
        loop.step()->kind() !=
            ValueKind::IntegerConstant ||
        loop.step()->integerValue() != 1 ||
        hasMemoryWriteOrConditional(loop.body()))
        return std::nullopt;

    auto control = loopControl(loop);
    HiraSequence *parent = loop.parent();
    if (!control || !parent ||
        !nodePosition(*parent, &loop) ||
        countUses(loop.body(), loop.induction()) != 1 ||
        countUses(*parent, loop.induction()) != 1)
        return std::nullopt;

    const HiraLoop::CarriedBinding &binding =
        loop.carriedValues().front();
    auto *indexType = dynamic_cast<IntegerType *>(
        loop.induction()->type());
    auto *stateType = binding.iteration
                          ? dynamic_cast<IntegerType *>(
                                binding.iteration->type())
                          : nullptr;
    if (!binding.initial || !binding.yielded ||
        !binding.result || !indexType ||
        indexType->num_bits_ != 32 || !stateType ||
        stateType->num_bits_ != 32 ||
        loop.upperBound()->type() != indexType ||
        binding.initial->type() != stateType ||
        binding.result->type() != stateType)
        return std::nullopt;

    auto *remainder = dynamic_cast<HiraComputeOp *>(
        binding.yielded->definingNode());
    if (!remainder ||
        remainder->computeKind() != ComputeKind::SRem ||
        remainder->operands().size() != 2 ||
        remainder->results().size() != 1 ||
        remainder->results().front() != binding.yielded)
        return std::nullopt;
    HiraValue *modulusValue = remainder->operands()[1];
    if (!modulusValue ||
        modulusValue->kind() !=
            ValueKind::IntegerConstant ||
        modulusValue->type() != stateType ||
        modulusValue->integerValue() <= 0)
        return std::nullopt;

    HiraValue *addResult = remainder->operands()[0];
    auto *addition = addResult
                         ? dynamic_cast<HiraComputeOp *>(
                               addResult->definingNode())
                         : nullptr;
    if (!addition ||
        addition->computeKind() != ComputeKind::Add ||
        addition->operands().size() != 2 ||
        addition->results().size() != 1 ||
        addition->results().front() != addResult)
        return std::nullopt;

    HiraValue *incrementValue = nullptr;
    if (addition->operands()[0] == binding.iteration)
        incrementValue = addition->operands()[1];
    else if (addition->operands()[1] == binding.iteration)
        incrementValue = addition->operands()[0];
    if (!incrementValue ||
        incrementValue->kind() !=
            ValueKind::IntegerConstant ||
        incrementValue->type() != stateType ||
        incrementValue->integerValue() <= 0 ||
        countUses(loop.body(), binding.iteration) != 1)
        return std::nullopt;

    const std::int64_t increment =
        incrementValue->integerValue();
    const std::int64_t modulus =
        modulusValue->integerValue();
    const std::int64_t safeBound =
        (static_cast<std::int64_t>(
             std::numeric_limits<int>::max()) -
         modulus) /
        increment;
    if (safeBound < 1)
        return std::nullopt;

    return ModularRecurrence{
        binding.initial, loop.upperBound(), binding.result,
        stateType, increment, modulus, safeBound};
}

bool foldModularRecurrence(HiraRegion &region,
                           HiraLoop &loop, Module &module) {
    std::optional<ModularRecurrence> recurrence =
        analyzeModularRecurrence(loop);
    if (!recurrence)
        return false;

    HiraSequence *parent = loop.parent();
    std::optional<std::size_t> position =
        nodePosition(*parent, &loop);
    Loop *source = region.sourceMapping().sourceLoop(&loop);
    if (!position || !source)
        return false;

    // All checks above are read-only.  From here onward every created value
    // is immediately owned by the replacement diamond, so rejection never
    // leaves a partially changed region.
    HiraValue *zero =
        region.createIntegerConstant(recurrence->type, 0);
    HiraValue *one =
        region.createIntegerConstant(recurrence->type, 1);
    HiraValue *boundConstant =
        region.createIntegerConstant(
            recurrence->type, recurrence->safeBound);
    // The bound proves every post-remainder step and the closed form safe.
    // The first source add still sees the unreduced initial value, so
    // also guard it explicitly; init >= 0 alone is insufficient near INT_MAX.
    HiraValue *largestSafeInitial =
        region.createIntegerConstant(
            recurrence->type,
            static_cast<std::int64_t>(
                std::numeric_limits<int>::max()) -
                recurrence->increment);
    HiraValue *incrementConstant =
        region.createIntegerConstant(
            recurrence->type, recurrence->increment);
    HiraValue *modulusConstant =
        region.createIntegerConstant(
            recurrence->type, recurrence->modulus);

    HiraValue *nonnegative =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::ICmp,
                  nonnegative, {recurrence->initial, zero},
                  ICmpInst::ICMP_SGE);
    HiraValue *initialAdditionSafe =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::ICmp,
                  initialAdditionSafe,
                  {recurrence->initial, largestSafeInitial},
                  ICmpInst::ICMP_SLE);
    HiraValue *nonempty =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::ICmp,
                  nonempty, {recurrence->bound, one},
                  ICmpInst::ICMP_SGE);
    HiraValue *withinBound =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::ICmp,
                  withinBound,
                  {recurrence->bound, boundConstant},
                  ICmpInst::ICMP_SLE);
    HiraValue *safeInitial =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::And,
                  safeInitial,
                  {nonnegative, initialAdditionSafe});
    HiraValue *safeInitialAndNonempty =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::And,
                  safeInitialAndNonempty,
                  {safeInitial, nonempty});
    HiraValue *guard =
        region.createValue(module.int1_ty_);
    insertCompute(*parent, (*position)++, ComputeKind::And,
                  guard,
                  {safeInitialAndNonempty, withinBound});

    auto conditional = std::make_unique<HiraIf>(guard);
    HiraIf *insertedConditional = conditional.get();

    HiraValue *reducedInitial =
        region.createValue(recurrence->type);
    insertCompute(insertedConditional->thenSequence(), 0,
                  ComputeKind::SRem, reducedInitial,
                  {recurrence->initial, modulusConstant});
    HiraValue *scaledIncrement =
        region.createValue(recurrence->type);
    insertCompute(insertedConditional->thenSequence(), 1,
                  ComputeKind::Mul, scaledIncrement,
                  {incrementConstant, recurrence->bound});
    HiraValue *sum = region.createValue(recurrence->type);
    insertCompute(insertedConditional->thenSequence(), 2,
                  ComputeKind::Add, sum,
                  {reducedInitial, scaledIncrement});
    HiraValue *fastResult =
        region.createValue(recurrence->type);
    insertCompute(insertedConditional->thenSequence(), 3,
                  ComputeKind::SRem, fastResult,
                  {sum, modulusConstant});

    HiraValue *slowResult =
        region.createValue(recurrence->type);
    loop.setCarriedResult(0, slowResult);

    // Encode that this loop is now only a preserved fallback in the
    // structured IR itself.  Hira runs to a CFG fixed point; without this
    // inert carried token, a later import would rediscover and wrap the same
    // fallback indefinitely.  The token has no external result or operation.
    LoopControl slowControl = *loopControl(loop);
    HiraValue *fallbackTokenIteration =
        region.createValue(recurrence->type);
    HiraValue *fallbackTokenResult =
        region.createValue(recurrence->type);
    const std::size_t fallbackToken =
        loop.addCarriedValue(
            zero, fallbackTokenIteration,
            fallbackTokenResult);
    loop.setCarriedYield(
        fallbackToken, fallbackTokenIteration);
    loop.addYieldValue(fallbackTokenIteration);
    slowControl.yield->addOperand(fallbackTokenIteration);

    insertedConditional->elseSequence().append(
        parent->remove(&loop));

    // The exporter requires one mapped loop at the root.  Keep that
    // structural contract with a one-trip wrapper; the guarded fast formula
    // and the complete original loop remain mutually exclusive inside it.
    HiraValue *wrapperInduction =
        region.createValue(recurrence->type);
    auto wrapper = std::make_unique<HiraLoop>(
        wrapperInduction, zero, one, one);
    HiraLoop *insertedWrapper = wrapper.get();
    HiraValue *wrapperIteration =
        region.createValue(recurrence->type);
    insertedWrapper->addCarriedValue(
        recurrence->initial, wrapperIteration,
        recurrence->stateResult);
    HiraValue *mergedResult =
        region.createValue(recurrence->type);
    insertedConditional->addResultBinding(
        fastResult, slowResult, mergedResult);
    insertedWrapper->body().append(std::move(conditional));

    HiraValue *wrapperNext =
        region.createValue(recurrence->type);
    insertCompute(insertedWrapper->body(),
                  insertedWrapper->body().nodes().size(),
                  ComputeKind::Add, wrapperNext,
                  {wrapperInduction, one});
    insertedWrapper->setCarriedYield(0, mergedResult);
    insertedWrapper->addYieldValue(wrapperNext);
    insertedWrapper->addYieldValue(mergedResult);
    auto wrapperYield = std::make_unique<HiraYield>();
    wrapperYield->addOperand(wrapperNext);
    wrapperYield->addOperand(mergedResult);
    insertedWrapper->body().append(std::move(wrapperYield));
    insertedWrapper->setRole(
        HiraLoop::Role::RepetitionFolded);
    parent->insert(*position, std::move(wrapper));

    region.sourceMapping().unmapLoop(&loop);
    region.sourceMapping().mapLoop(insertedWrapper, source);

    region.markModified();
    if (std::getenv("DEBUG_HIRA_REPETITION")) {
        std::cerr
            << "// hira.repetition_folding = modular-realized";
        if (source && source->header)
            std::cerr << " header=" << source->header->name_;
        std::cerr << " c=" << recurrence->increment
                  << " m=" << recurrence->modulus
                  << " bound=" << recurrence->safeBound << "\n";
    }
    return true;
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
    if (foldModularRecurrence(region, loop, module))
        return true;
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
