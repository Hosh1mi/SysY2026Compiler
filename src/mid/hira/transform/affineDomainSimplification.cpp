#include "../../../include/mid/hira/transform/affineDomainSimplification.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <set>

namespace hira {
namespace {

enum class Relation {
    LT,
    LE,
    GT,
    GE,
};

Relation swap(Relation relation) {
    switch (relation) {
    case Relation::LT:
        return Relation::GT;
    case Relation::LE:
        return Relation::GE;
    case Relation::GT:
        return Relation::LT;
    case Relation::GE:
        return Relation::LE;
    }
    return relation;
}

Relation invert(Relation relation) {
    switch (relation) {
    case Relation::LT:
        return Relation::GE;
    case Relation::LE:
        return Relation::GT;
    case Relation::GT:
        return Relation::LE;
    case Relation::GE:
        return Relation::LT;
    }
    return relation;
}

bool decodePredicate(int predicate, Relation &relation) {
    switch (predicate) {
    case ICmpInst::ICMP_SLT:
        relation = Relation::LT;
        return true;
    case ICmpInst::ICMP_SLE:
        relation = Relation::LE;
        return true;
    case ICmpInst::ICMP_SGT:
        relation = Relation::GT;
        return true;
    case ICmpInst::ICMP_SGE:
        relation = Relation::GE;
        return true;
    default:
        return false;
    }
}

HiraComputeOp *insertCompute(
    HiraSequence &sequence, std::size_t position,
    ComputeKind kind, HiraValue *result,
    HiraValue *left, HiraValue *right,
    int predicate = 0) {
    auto owner =
        std::make_unique<HiraComputeOp>(
            kind, predicate);
    owner->addOperand(left);
    owner->addOperand(right);
    owner->addResult(result);
    return static_cast<HiraComputeOp *>(
        sequence.insert(position, std::move(owner)));
}

void collectDefinitions(
    const HiraSequence &sequence,
    std::set<const HiraNode *> &definitions) {
    for (const auto &node : sequence.nodes()) {
        definitions.insert(node.get());
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(node.get()))
            collectDefinitions(loop->body(), definitions);
        else if (auto *condition =
                     dynamic_cast<const HiraIf *>(node.get())) {
            collectDefinitions(
                condition->thenSequence(), definitions);
            collectDefinitions(
                condition->elseSequence(), definitions);
        }
    }
}

bool containsNode(const HiraSequence &sequence,
                  const HiraNode *target) {
    for (const auto &node : sequence.nodes()) {
        if (node.get() == target)
            return true;
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(node.get())) {
            if (containsNode(loop->body(), target))
                return true;
        } else if (auto *condition =
                       dynamic_cast<const HiraIf *>(
                           node.get())) {
            if (containsNode(
                    condition->thenSequence(), target) ||
                containsNode(
                    condition->elseSequence(), target))
                return true;
        }
    }
    return false;
}

std::size_t countUses(
    const HiraSequence &sequence,
    const HiraValue *value) {
    std::size_t uses = 0;
    for (const auto &node : sequence.nodes()) {
        for (const HiraValue *operand :
             node->operands())
            uses += operand == value;
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(node.get()))
            uses += countUses(loop->body(), value);
        else if (auto *condition =
                     dynamic_cast<const HiraIf *>(node.get())) {
            uses += countUses(
                condition->thenSequence(), value);
            uses += countUses(
                condition->elseSequence(), value);
        }
    }
    return uses;
}

bool restrictLoop(HiraRegion &region, HiraLoop &loop,
                  HiraIf &condition,
                  HiraSequence &active, bool activeTruth,
                  HiraSequence &root) {
    if (!condition.resultBindings().empty() ||
        !loop.carriedValues().empty())
        return false;
    auto control = analyzeCanonicalLoopControl(loop);
    if (!control)
        return false;
    for (const auto &node : loop.body().nodes()) {
        if (node.get() == &condition ||
            node.get() == control->inductionUpdate ||
            node.get() == control->yield)
            continue;
        if (!dynamic_cast<const HiraComputeOp *>(
                node.get()))
            return false;
    }
    auto *compare =
        dynamic_cast<HiraComputeOp *>(
            condition.condition()->definingNode());
    if (!compare ||
        compare->computeKind() != ComputeKind::ICmp ||
        compare->operands().size() != 2)
        return false;

    Relation relation;
    if (!decodePredicate(compare->predicate(), relation))
        return false;
    HiraValue *boundary = nullptr;
    if (compare->operands()[0] == loop.induction())
        boundary = compare->operands()[1];
    else if (compare->operands()[1] == loop.induction()) {
        boundary = compare->operands()[0];
        relation = swap(relation);
    } else {
        return false;
    }
    if (!activeTruth)
        relation = invert(relation);

    std::set<const HiraNode *> loopDefinitions;
    collectDefinitions(loop.body(), loopDefinitions);
    if (boundary->definingNode() &&
        loopDefinitions.count(boundary->definingNode()))
        return false;
    auto *indexType =
        dynamic_cast<IntegerType *>(
            loop.induction()->type());
    HiraSequence *parent = loop.parent();
    if (!indexType || indexType->num_bits_ != 32 ||
        !parent)
        return false;
    auto loopPosition = std::find_if(
        parent->nodes().begin(), parent->nodes().end(),
        [&loop](const std::unique_ptr<HiraNode> &node) {
            return node.get() == &loop;
        });
    if (loopPosition == parent->nodes().end())
        return false;
    std::size_t insertion =
        static_cast<std::size_t>(
            loopPosition - parent->nodes().begin());
    Module *module = nullptr;
    if (Loop *source =
            region.sourceMapping().sourceLoop(&loop))
        if (source->header && source->header->parent_)
            module = source->header->parent_->parent_;
    if (!module)
        return false;

    HiraValue *candidate = boundary;
    if (relation == Relation::LE ||
        relation == Relation::GT) {
        bool incrementCannotOverflow = false;
        if (boundary->kind() ==
            ValueKind::IntegerConstant)
            incrementCannotOverflow =
                boundary->integerValue() <
                std::numeric_limits<std::int32_t>::max();
        else if (auto *outerLoop =
                     dynamic_cast<HiraLoop *>(
                         boundary->definingNode()))
            incrementCannotOverflow =
                containsNode(
                    outerLoop->body(), &loop);
        if (!incrementCannotOverflow)
            return false;
        HiraValue *one =
            region.createIntegerConstant(indexType, 1);
        HiraValue *incremented =
            region.createValue(indexType);
        insertCompute(
            *parent, insertion++, ComputeKind::Add,
            incremented, boundary, one);
        candidate = incremented;
    }

    if (relation == Relation::LT ||
        relation == Relation::LE) {
        HiraValue *useCandidate =
            region.createValue(module->int1_ty_);
        insertCompute(
            *parent, insertion++, ComputeKind::ICmp,
            useCandidate, candidate, loop.upperBound(),
            ICmpInst::ICMP_SLT);
        HiraValue *restricted =
            region.createValue(indexType);
        auto select =
            std::make_unique<HiraComputeOp>(
                ComputeKind::Select);
        select->addOperand(useCandidate);
        select->addOperand(candidate);
        select->addOperand(loop.upperBound());
        select->addResult(restricted);
        parent->insert(insertion++, std::move(select));
        loop.setUpperBound(restricted);
    } else {
        HiraValue *useCandidate =
            region.createValue(module->int1_ty_);
        insertCompute(
            *parent, insertion++, ComputeKind::ICmp,
            useCandidate, loop.lowerBound(), candidate,
            ICmpInst::ICMP_SLT);
        HiraValue *restricted =
            region.createValue(indexType);
        auto select =
            std::make_unique<HiraComputeOp>(
                ComputeKind::Select);
        select->addOperand(useCandidate);
        select->addOperand(candidate);
        select->addOperand(loop.lowerBound());
        select->addResult(restricted);
        parent->insert(insertion++, std::move(select));
        loop.setLowerBound(restricted);
    }

    HiraSequence *body = condition.parent();
    auto position = std::find_if(
        body->nodes().begin(), body->nodes().end(),
        [&condition](const std::unique_ptr<HiraNode> &node) {
            return node.get() == &condition;
        });
    std::size_t bodyPosition =
        static_cast<std::size_t>(
            position - body->nodes().begin());
    while (!active.nodes().empty()) {
        HiraNode *front = active.nodes().front().get();
        body->insert(bodyPosition++,
                     active.remove(front));
    }
    HiraValue *guard = condition.condition();
    region.sourceMapping().unmapNode(&condition);
    body->remove(&condition);
    if (countUses(root, guard) == 0 &&
        compare->parent())
        compare->parent()->remove(compare);
    return true;
}

bool visitSequence(HiraRegion &region,
                   HiraSequence &sequence,
                   HiraSequence &root) {
    bool changed = false;
    std::vector<HiraNode *> nodes;
    for (const auto &node : sequence.nodes())
        nodes.push_back(node.get());
    for (HiraNode *node : nodes) {
        auto *loop = dynamic_cast<HiraLoop *>(node);
        if (!loop)
            continue;
        changed |= visitSequence(
            region, loop->body(), root);
        std::vector<HiraNode *> bodyNodes;
        for (const auto &bodyNode :
             loop->body().nodes())
            bodyNodes.push_back(bodyNode.get());
        for (HiraNode *bodyNode : bodyNodes) {
            auto *condition =
                dynamic_cast<HiraIf *>(bodyNode);
            if (!condition)
                continue;
            const bool thenEmpty =
                condition->thenSequence().nodes().empty();
            const bool elseEmpty =
                condition->elseSequence().nodes().empty();
            if (thenEmpty == elseEmpty)
                continue;
            HiraSequence &active =
                thenEmpty
                    ? condition->elseSequence()
                    : condition->thenSequence();
            if (restrictLoop(
                    region, *loop, *condition, active,
                    !thenEmpty, root)) {
                changed = true;
                break;
            }
        }
    }
    return changed;
}

} // namespace

bool extractAffineInteriorDomains(HiraRegion &region) {
    bool changed = visitSequence(
        region, region.rootSequence(),
        region.rootSequence());
    if (changed)
        region.markModified();
    return changed;
}

} // namespace hira
