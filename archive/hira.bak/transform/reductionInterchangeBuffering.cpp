#include "../../../include/mid/hira/transform/reductionInterchangeBuffering.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace hira::polyhedral {
namespace {

using Error = ReductionInterchangeBufferingError;
using Result = ReductionInterchangeBufferingResult;

Result reject(Error error, std::string detail) {
    Result result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

struct LoopControl {
    const HiraComputeOp *update = nullptr;
    const HiraYield *yield = nullptr;
};

std::optional<LoopControl>
analyzeLoopControl(const HiraLoop &loop, bool allowCarried) {
    const auto &nodes = loop.body().nodes();
    if (nodes.size() < 2 ||
        (!allowCarried && !loop.carriedValues().empty()) ||
        loop.yieldValues().size() !=
            loop.carriedValues().size() + 1)
        return std::nullopt;
    auto *update = dynamic_cast<const HiraComputeOp *>(
        nodes[nodes.size() - 2].get());
    auto *yield =
        dynamic_cast<const HiraYield *>(nodes.back().get());
    if (!update || !yield ||
        update->computeKind() != ComputeKind::Add ||
        update->operands().size() != 2 ||
        update->results().size() != 1 ||
        yield->operands().size() != loop.yieldValues().size() ||
        yield->operands().front() != update->results().front() ||
        loop.yieldValues().front() != update->results().front())
        return std::nullopt;
    const bool canonical =
        (update->operands()[0] == loop.induction() &&
         update->operands()[1] == loop.step()) ||
        (update->operands()[1] == loop.induction() &&
         update->operands()[0] == loop.step());
    return canonical ? std::optional<LoopControl>{{update, yield}}
                     : std::nullopt;
}

bool containsLoop(const HiraSequence &sequence) {
    for (const auto &node : sequence.nodes()) {
        if (dynamic_cast<const HiraLoop *>(node.get()))
            return true;
        auto *condition = dynamic_cast<const HiraIf *>(node.get());
        if (condition &&
            (containsLoop(condition->thenSequence()) ||
             containsLoop(condition->elseSequence())))
            return true;
    }
    return false;
}

bool cloneablePureNode(const HiraNode &node) {
    if (dynamic_cast<const HiraComputeOp *>(&node) ||
        dynamic_cast<const HiraLoad *>(&node))
        return true;
    auto *condition = dynamic_cast<const HiraIf *>(&node);
    if (!condition)
        return false;
    for (const auto &arm : condition->thenSequence().nodes())
        if (!cloneablePureNode(*arm))
            return false;
    for (const auto &arm : condition->elseSequence().nodes())
        if (!cloneablePureNode(*arm))
            return false;
    return true;
}

void collectNodes(const HiraSequence &sequence,
                  std::set<const HiraNode *> &nodes) {
    for (const auto &owner : sequence.nodes()) {
        nodes.insert(owner.get());
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(owner.get()))
            collectNodes(loop->body(), nodes);
        else if (auto *condition =
                     dynamic_cast<const HiraIf *>(owner.get())) {
            collectNodes(condition->thenSequence(), nodes);
            collectNodes(condition->elseSequence(), nodes);
        }
    }
}

void collectNodeTree(const HiraNode &node,
                     std::set<const HiraNode *> &nodes) {
    nodes.insert(&node);
    auto *condition = dynamic_cast<const HiraIf *>(&node);
    if (!condition)
        return;
    collectNodes(condition->thenSequence(), nodes);
    collectNodes(condition->elseSequence(), nodes);
}

const IterationDomain *domainFor(const PolyhedralModel &model,
                                 const HiraLoop *loop) {
    for (const IterationDomain &domain : model.domains())
        if (domain.loop == loop)
            return &domain;
    return nullptr;
}

const AccessRelation *accessFor(const PolyhedralModel &model,
                                const HiraNode *node) {
    const PolyhedralStatement *statement = nullptr;
    for (const PolyhedralStatement &candidate : model.statements()) {
        if (candidate.node != node)
            continue;
        if (statement)
            return nullptr;
        statement = &candidate;
    }
    if (!statement)
        return nullptr;
    const AccessRelation *result = nullptr;
    for (const AccessRelation &access : model.accesses()) {
        if (access.statement != statement->id)
            continue;
        if (result)
            return nullptr;
        result = &access;
    }
    return result;
}

bool exactDimension(const AffineExpr &expression,
                    AffineVariable dimension) {
    if (!expression.valid() || expression.constantTerm() != 0 ||
        expression.coefficients().size() != 1)
        return false;
    auto coefficient =
        expression.coefficients().find(dimension);
    return coefficient != expression.coefficients().end() &&
           coefficient->second == 1;
}

bool hasExactParentSlice(const AccessRelation &access,
                         AffineVariable parentDimension) {
    for (const AffineExpr &subscript : access.subscripts)
        if (exactDimension(subscript, parentDimension))
            return true;
    return false;
}

bool isAddOfIteration(const HiraValue *value,
                      const HiraValue *iteration) {
    auto *add =
        value ? dynamic_cast<const HiraComputeOp *>(
                    value->definingNode())
              : nullptr;
    return add && add->computeKind() == ComputeKind::Add &&
           add->results().size() == 1 &&
           add->results().front() == value &&
           add->operands().size() == 2 &&
           ((add->operands()[0] == iteration) ^
            (add->operands()[1] == iteration));
}

bool isSupportedAdditiveYield(const HiraValue *yielded,
                              const HiraValue *iteration) {
    if (isAddOfIteration(yielded, iteration))
        return true;
    auto *select =
        yielded ? dynamic_cast<const HiraComputeOp *>(
                      yielded->definingNode())
                : nullptr;
    if (!select ||
        select->computeKind() != ComputeKind::Select ||
        select->results().size() != 1 ||
        select->results().front() != yielded ||
        select->operands().size() != 3)
        return false;
    const HiraValue *thenValue = select->operands()[1];
    const HiraValue *elseValue = select->operands()[2];
    const bool thenKeeps = thenValue == iteration;
    const bool elseKeeps = elseValue == iteration;
    if (thenKeeps == elseKeeps)
        return false;
    return isAddOfIteration(
        thenKeeps ? elseValue : thenValue, iteration);
}

std::optional<std::int64_t>
scaledStride(const PolyhedralModel &model,
             const AccessRelation &access,
             AffineVariable dimension, std::int64_t step) {
    auto stride =
        analyzeLinearAccessStride(model, access, dimension);
    if (!stride || step <= 0 ||
        *stride > std::numeric_limits<std::int64_t>::max() / step)
        return std::nullopt;
    return *stride * step;
}

ArrayType *innermostStaticArray(const MemoryObject &object,
                                Type *elementType) {
    auto *pointer = object.base
                        ? dynamic_cast<PointerType *>(
                              object.base->type())
                        : nullptr;
    if (!pointer)
        return nullptr;
    Type *current = pointer->contained_;
    ArrayType *innermost = nullptr;
    while (auto *array = dynamic_cast<ArrayType *>(current)) {
        innermost = array;
        current = array->contained_;
    }
    return innermost && innermost->num_elements_ &&
                   innermost->contained_ == elementType
               ? innermost
               : nullptr;
}

struct Candidate {
    HiraLoop *parent = nullptr;
    HiraLoop *leaf = nullptr;
    std::size_t leafPosition = 0;
    std::size_t parentPayloadEnd = 0;
    std::size_t leafPayloadEnd = 0;
    HiraStore *store = nullptr;
    const HiraLoop::CarriedBinding *binding = nullptr;
    LoopControl parentControl;
    LoopControl leafControl;
};

void collectShapes(HiraSequence &sequence,
                   std::vector<Candidate> &candidates) {
    for (const auto &owner : sequence.nodes()) {
        auto *parent = dynamic_cast<HiraLoop *>(owner.get());
        if (!parent) {
            if (auto *condition =
                    dynamic_cast<HiraIf *>(owner.get())) {
                collectShapes(condition->thenSequence(), candidates);
                collectShapes(condition->elseSequence(), candidates);
            }
            continue;
        }

        auto parentControl = analyzeLoopControl(*parent, false);
        const auto &body = parent->body().nodes();
        if (parentControl && body.size() >= 3) {
            std::vector<std::pair<std::size_t, HiraLoop *>> children;
            for (std::size_t index = 0; index + 2 < body.size();
                 ++index)
                if (auto *loop =
                        dynamic_cast<HiraLoop *>(body[index].get()))
                    children.push_back({index, loop});
            if (children.size() == 1) {
                const auto [position, leaf] = children.front();
                HiraStore *store = nullptr;
                bool postShape = true;
                for (std::size_t index = position + 1;
                     index + 2 < body.size(); ++index) {
                    if (auto *candidateStore =
                            dynamic_cast<HiraStore *>(
                                body[index].get())) {
                        if (store ||
                            index + 3 != body.size())
                            postShape = false;
                        store = candidateStore;
                    } else if (!dynamic_cast<HiraComputeOp *>(
                                   body[index].get())) {
                        postShape = false;
                    }
                }
                auto leafControl =
                    analyzeLoopControl(*leaf, true);
                if (postShape && store && leafControl &&
                    !containsLoop(leaf->body())) {
                    Candidate candidate;
                    candidate.parent = parent;
                    candidate.leaf = leaf;
                    candidate.leafPosition = position;
                    candidate.parentPayloadEnd = body.size() - 2;
                    candidate.leafPayloadEnd =
                        leaf->body().nodes().size() - 2;
                    candidate.store = store;
                    candidate.parentControl = *parentControl;
                    candidate.leafControl = *leafControl;
                    candidates.push_back(candidate);
                }
            }
        }
        collectShapes(parent->body(), candidates);
    }
}

HiraValue *mappedValue(
    const std::map<const HiraValue *, HiraValue *> &mapped,
    const HiraValue *value) {
    auto found = mapped.find(value);
    return found == mapped.end()
               ? const_cast<HiraValue *>(value)
               : found->second;
}

void mapSourceNode(HiraRegion &region, HiraNode *target,
                   const HiraNode *source) {
    Instruction *instruction =
        region.sourceMapping().sourceInstruction(source);
    if (instruction)
        region.sourceMapping().mapNode(target, instruction);
}

HiraComputeOp *appendCompute(
    HiraSequence &sequence, ComputeKind kind, HiraValue *result,
    const std::vector<HiraValue *> &operands, int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(kind, predicate);
    for (HiraValue *operand : operands)
        owner->addOperand(operand);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.append(std::move(owner)));
}

void clonePureNode(
    HiraRegion &region, HiraSequence &target,
    const HiraNode &source,
    std::map<const HiraValue *, HiraValue *> &mapped) {
    if (auto *compute =
            dynamic_cast<const HiraComputeOp *>(&source)) {
        std::vector<HiraValue *> operands;
        for (const HiraValue *operand : compute->operands())
            operands.push_back(mappedValue(mapped, operand));
        HiraValue *result =
            region.createValue(compute->results().front()->type());
        HiraComputeOp *clone = appendCompute(
            target, compute->computeKind(), result, operands,
            compute->predicate());
        mapSourceNode(region, clone, compute);
        mapped[compute->results().front()] = result;
        return;
    }
    if (auto *load = dynamic_cast<const HiraLoad *>(&source)) {
        auto owner = std::make_unique<HiraLoad>(
            mappedValue(mapped, load->address()));
        HiraValue *result =
            region.createValue(load->results().front()->type());
        owner->addResult(result);
        HiraNode *clone = target.append(std::move(owner));
        mapSourceNode(region, clone, load);
        mapped[load->results().front()] = result;
        return;
    }

    auto *condition = static_cast<const HiraIf *>(&source);
    auto thenMapped = mapped;
    auto elseMapped = mapped;
    auto owner = std::make_unique<HiraIf>(
        mappedValue(mapped, condition->condition()));
    HiraIf *clone = owner.get();
    for (const auto &node : condition->thenSequence().nodes())
        clonePureNode(region, clone->thenSequence(), *node,
                      thenMapped);
    for (const auto &node : condition->elseSequence().nodes())
        clonePureNode(region, clone->elseSequence(), *node,
                      elseMapped);
    for (const HiraIf::ResultBinding &binding :
         condition->resultBindings()) {
        HiraValue *result =
            region.createValue(binding.result->type());
        clone->addResultBinding(
            mappedValue(thenMapped, binding.thenValue),
            mappedValue(elseMapped, binding.elseValue), result);
        mapped[binding.result] = result;
    }
    HiraNode *inserted = target.append(std::move(owner));
    mapSourceNode(region, inserted, condition);
}

void cloneRange(
    HiraRegion &region, HiraSequence &target,
    const HiraSequence &source, std::size_t begin, std::size_t end,
    std::map<const HiraValue *, HiraValue *> &mapped) {
    for (std::size_t index = begin; index < end; ++index)
        clonePureNode(region, target, *source.nodes()[index], mapped);
}

HiraValue *appendScratchAddress(
    HiraRegion &region, HiraSequence &sequence, HiraValue *scratch,
    HiraValue *index, Type *addressType, HiraValue *zero) {
    HiraValue *address = region.createValue(addressType);
    appendCompute(sequence, ComputeKind::GetElementPtr, address,
                  {scratch, zero, index});
    return address;
}

void appendControl(HiraRegion &region, HiraLoop &loop,
                   const LoopControl &source) {
    HiraValue *next =
        region.createValue(loop.induction()->type());
    HiraComputeOp *update = appendCompute(
        loop.body(), ComputeKind::Add, next,
        {loop.induction(), loop.step()});
    mapSourceNode(region, update, source.update);
    loop.addYieldValue(next);
    auto owner = std::make_unique<HiraYield>();
    owner->addOperand(next);
    HiraNode *yield = loop.body().append(std::move(owner));
    mapSourceNode(region, yield, source.yield);
}

std::unique_ptr<HiraLoop>
makeLoop(HiraRegion &region, const HiraLoop &source) {
    HiraValue *induction =
        region.createValue(source.induction()->type());
    auto loop = std::make_unique<HiraLoop>(
        induction, source.lowerBound(), source.upperBound(),
        source.step());
    loop->setRole(HiraLoop::Role::Ordinary);
    return loop;
}

void unmapSubtree(SourceMapping &mapping, HiraNode &node);

void unmapSubtree(SourceMapping &mapping, HiraSequence &sequence) {
    for (const auto &owner : sequence.nodes())
        unmapSubtree(mapping, *owner);
}

void unmapSubtree(SourceMapping &mapping, HiraNode &node) {
    if (auto *loop = dynamic_cast<HiraLoop *>(&node)) {
        unmapSubtree(mapping, loop->body());
        mapping.unmapLoop(loop);
    } else if (auto *condition =
                   dynamic_cast<HiraIf *>(&node)) {
        unmapSubtree(mapping, condition->thenSequence());
        unmapSubtree(mapping, condition->elseSequence());
    }
    mapping.unmapNode(&node);
}

} // namespace

ReductionInterchangeBufferingResult
bufferReductionInterchange(HiraRegion &region,
                           const PolyhedralModel &model) {
    std::vector<Candidate> candidates;
    collectShapes(region.rootSequence(), candidates);
    if (candidates.empty())
        return reject(Error::NoCandidate,
                      "no-parent-leaf-reduction-store-shape");
    if (candidates.size() != 1)
        return reject(Error::AmbiguousCandidate,
                      "multiple-reduction-interchange-candidates");
    Candidate candidate = candidates.front();
    HiraLoop &parent = *candidate.parent;
    HiraLoop &leaf = *candidate.leaf;

    if (leaf.carriedValues().size() != 1)
        return reject(Error::UnsupportedReduction,
                      "leaf-must-have-one-carried-binding");
    const HiraLoop::CarriedBinding &binding =
        leaf.carriedValues().front();
    candidate.binding = &binding;
    auto *integer =
        dynamic_cast<IntegerType *>(binding.iteration->type());
    if (!integer ||
        !isSupportedAdditiveYield(
            binding.yielded, binding.iteration))
        return reject(Error::UnsupportedReduction,
                      "carried-value-is-not-integer-add-reduction");
    if (candidate.store->value() != binding.result)
        return reject(Error::UnsupportedStructure,
                      "post-loop-store-does-not-use-reduction-result");

    const auto &parentBody = parent.body();
    const auto &leafBody = leaf.body();
    for (std::size_t index = 0;
         index < candidate.leafPosition; ++index)
        if (!cloneablePureNode(*parentBody.nodes()[index]))
            return reject(Error::UnsafeMemoryEffects,
                          "unsafe-parent-prefix");
    for (std::size_t index = 0;
         index < candidate.leafPayloadEnd; ++index)
        if (!cloneablePureNode(*leafBody.nodes()[index]))
            return reject(Error::UnsafeMemoryEffects,
                          "unsafe-reduction-payload");

    const IterationDomain *parentDomain =
        domainFor(model, &parent);
    const IterationDomain *leafDomain =
        domainFor(model, &leaf);
    if (!parentDomain || !leafDomain)
        return reject(Error::UnsupportedStructure,
                      "missing-loop-domain");

    std::set<const HiraNode *> parentNodes;
    collectNodes(parent.body(), parentNodes);
    for (const HiraValue *bound :
         {leaf.lowerBound(), leaf.upperBound()})
        if (bound && bound->definingNode() &&
            parentNodes.count(bound->definingNode()))
            return reject(Error::UnsupportedStructure,
                          "leaf-bound-not-available-before-parent");

    const AccessRelation *storeAccess =
        accessFor(model, candidate.store);
    if (!storeAccess ||
        storeAccess->kind != MemoryAccessKind::Write ||
        storeAccess->object >= model.memoryObjects().size() ||
        storeAccess->subscripts.empty() ||
        !exactDimension(storeAccess->subscripts.back(),
                        parentDomain->dimension))
        return reject(Error::UnprovenStoreback,
                      "store-last-index-is-not-parent-induction");

    std::set<const HiraNode *> readNodes;
    for (std::size_t index = 0;
         index < candidate.leafPosition; ++index)
        collectNodeTree(*parentBody.nodes()[index], readNodes);
    for (std::size_t index = 0;
         index < candidate.leafPayloadEnd; ++index)
        collectNodeTree(*leafBody.nodes()[index], readNodes);
    bool strictlyBetter = false;
    bool sawPayloadAccess = false;
    for (const HiraNode *node : readNodes) {
        auto *load = dynamic_cast<const HiraLoad *>(node);
        if (!load)
            continue;
        const AccessRelation *access = accessFor(model, load);
        if (!access || access->kind != MemoryAccessKind::Read)
            return reject(Error::UnsafeMemoryEffects,
                          "missing-load-access-relation");
        MemoryAliasKind alias =
            model.aliasRelation(access->object, storeAccess->object);
        if (alias != MemoryAliasKind::NoAlias) {
            if (access->object != storeAccess->object)
                return reject(
                    Error::UnprovenStoreback,
                    "may-alias-distinct-object-load");
            if (!hasExactParentSlice(
                    *access, parentDomain->dimension))
                return reject(
                    Error::UnprovenStoreback,
                    "same-object-load-has-no-parent-slice");
        }

        bool payloadAccess = false;
        for (std::size_t index = 0;
             index < candidate.leafPayloadEnd; ++index) {
            std::set<const HiraNode *> tree;
            collectNodeTree(*leafBody.nodes()[index], tree);
            if (tree.count(node)) {
                payloadAccess = true;
                break;
            }
        }
        if (!payloadAccess)
            continue;
        sawPayloadAccess = true;
        auto parentStride = scaledStride(
            model, *access, parentDomain->dimension,
            parent.step()->integerValue());
        auto leafStride = scaledStride(
            model, *access, leafDomain->dimension,
            leaf.step()->integerValue());
        if (!parentStride || !leafStride ||
            *parentStride > *leafStride)
            return reject(Error::UnprofitableInterchange,
                          "interchange-does-not-lower-all-access-strides");
        strictlyBetter |= *parentStride < *leafStride;
    }
    if (!sawPayloadAccess || !strictlyBetter)
        return reject(Error::UnprofitableInterchange,
                      "no-strictly-lower-payload-stride");

    ArrayType *scratchType = innermostStaticArray(
        model.memoryObjects()[storeAccess->object],
        binding.iteration->type());
    if (!scratchType)
        return reject(Error::UnsupportedScratchType,
                      "store-target-has-no-static-innermost-array");

    HiraSequence *owner = parent.parent();
    if (!owner)
        return reject(Error::UnsupportedStructure,
                      "parent-loop-has-no-owner");
    std::size_t parentPosition = 0;
    while (parentPosition < owner->nodes().size() &&
           owner->nodes()[parentPosition].get() != &parent)
        ++parentPosition;
    if (parentPosition == owner->nodes().size())
        return reject(Error::UnsupportedStructure,
                      "parent-loop-not-in-owner");

    // All validation is complete.  From here construction is infallible and
    // the original tree is replaced only after all three phases are ready.
    HiraValue *scratch = region.createScratch(scratchType);
    HiraValue *zero = region.createIntegerConstant(
        parent.induction()->type(), 0);
    Type *scratchAddressType = candidate.store->address()->type();

    auto clear = makeLoop(region, parent);
    HiraLoop *clearLoop = clear.get();
    {
        std::map<const HiraValue *, HiraValue *> mapped;
        mapped[parent.induction()] = clearLoop->induction();
        cloneRange(region, clearLoop->body(), parentBody, 0,
                   candidate.leafPosition, mapped);
        HiraValue *address = appendScratchAddress(
            region, clearLoop->body(), scratch,
            clearLoop->induction(), scratchAddressType, zero);
        clearLoop->body().append(std::make_unique<HiraStore>(
            mappedValue(mapped, binding.initial), address));
        appendControl(region, *clearLoop, candidate.parentControl);
    }

    auto computeOuter = makeLoop(region, leaf);
    HiraLoop *computeOuterLoop = computeOuter.get();
    auto computeInner = makeLoop(region, parent);
    HiraLoop *computeInnerLoop = computeInner.get();
    {
        std::map<const HiraValue *, HiraValue *> mapped;
        mapped[parent.induction()] = computeInnerLoop->induction();
        mapped[leaf.induction()] = computeOuterLoop->induction();
        cloneRange(region, computeInnerLoop->body(), parentBody, 0,
                   candidate.leafPosition, mapped);
        HiraValue *address = appendScratchAddress(
            region, computeInnerLoop->body(), scratch,
            computeInnerLoop->induction(), scratchAddressType, zero);
        auto loadOwner = std::make_unique<HiraLoad>(address);
        HiraValue *accumulator =
            region.createValue(binding.iteration->type());
        loadOwner->addResult(accumulator);
        computeInnerLoop->body().append(std::move(loadOwner));
        mapped[binding.iteration] = accumulator;
        cloneRange(region, computeInnerLoop->body(), leafBody, 0,
                   candidate.leafPayloadEnd, mapped);
        computeInnerLoop->body().append(std::make_unique<HiraStore>(
            mappedValue(mapped, binding.yielded), address));
        appendControl(region, *computeInnerLoop,
                      candidate.parentControl);
        computeOuterLoop->body().append(std::move(computeInner));
        appendControl(region, *computeOuterLoop,
                      candidate.leafControl);
    }

    auto storeback = makeLoop(region, parent);
    HiraLoop *storebackLoop = storeback.get();
    {
        std::map<const HiraValue *, HiraValue *> mapped;
        mapped[parent.induction()] = storebackLoop->induction();
        cloneRange(region, storebackLoop->body(), parentBody, 0,
                   candidate.leafPosition, mapped);
        HiraValue *scratchAddress = appendScratchAddress(
            region, storebackLoop->body(), scratch,
            storebackLoop->induction(), scratchAddressType, zero);
        auto loadOwner =
            std::make_unique<HiraLoad>(scratchAddress);
        HiraValue *result =
            region.createValue(binding.result->type());
        loadOwner->addResult(result);
        storebackLoop->body().append(std::move(loadOwner));
        mapped[binding.result] = result;
        cloneRange(region, storebackLoop->body(), parentBody,
                   candidate.leafPosition + 1,
                   candidate.parentPayloadEnd - 1, mapped);
        auto outputStore = std::make_unique<HiraStore>(
            result, mappedValue(mapped, candidate.store->address()));
        HiraNode *inserted =
            storebackLoop->body().append(std::move(outputStore));
        mapSourceNode(region, inserted, candidate.store);
        appendControl(region, *storebackLoop,
                      candidate.parentControl);
    }

    Loop *sourceParent =
        region.sourceMapping().sourceLoop(&parent);
    Loop *sourceLeaf =
        region.sourceMapping().sourceLoop(&leaf);
    if (sourceParent) {
        region.sourceMapping().mapLoop(clearLoop, sourceParent);
        region.sourceMapping().mapLoop(computeInnerLoop, sourceParent);
        region.sourceMapping().mapLoop(storebackLoop, sourceParent);
    }
    if (sourceLeaf)
        region.sourceMapping().mapLoop(computeOuterLoop, sourceLeaf);

    std::unique_ptr<HiraNode> old = owner->remove(&parent);
    owner->insert(parentPosition, std::move(clear));
    owner->insert(parentPosition + 1, std::move(computeOuter));
    owner->insert(parentPosition + 2, std::move(storeback));
    unmapSubtree(region.sourceMapping(), *old);
    old.reset();
    region.markModified();
    return {true, Error::None, {}};
}

const char *reductionInterchangeBufferingErrorName(Error error) {
    switch (error) {
    case Error::None:
        return "none";
    case Error::NoCandidate:
        return "no-candidate";
    case Error::AmbiguousCandidate:
        return "ambiguous-candidate";
    case Error::UnsupportedStructure:
        return "unsupported-structure";
    case Error::UnsupportedReduction:
        return "unsupported-reduction";
    case Error::UnsafeMemoryEffects:
        return "unsafe-memory-effects";
    case Error::UnprovenStoreback:
        return "unproven-storeback";
    case Error::UnprofitableInterchange:
        return "unprofitable-interchange";
    case Error::UnsupportedScratchType:
        return "unsupported-scratch-type";
    }
    return "unknown";
}

} // namespace hira::polyhedral
