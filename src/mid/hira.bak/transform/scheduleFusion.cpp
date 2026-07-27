#include "../../../include/mid/hira/transform/scheduleFusion.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace hira::polyhedral {
namespace {

bool sameValue(const HiraValue *left, const HiraValue *right) {
    if (left == right)
        return true;
    if (!left || !right || left->kind() != right->kind() ||
        left->type() != right->type())
        return false;
    if (left->kind() == ValueKind::IntegerConstant)
        return left->integerValue() == right->integerValue();
    if (left->kind() == ValueKind::FloatConstant)
        return left->floatValue() == right->floatValue();
    return false;
}

const IterationDomain *domainFor(
    const PolyhedralModel &model, const HiraLoop &loop) {
    for (const IterationDomain &domain : model.domains())
        if (domain.loop == &loop)
            return &domain;
    return nullptr;
}

void collectNodes(const HiraSequence &sequence,
                  std::set<const HiraNode *> &nodes) {
    for (const auto &owner : sequence.nodes()) {
        const HiraNode *node = owner.get();
        nodes.insert(node);
        if (auto *loop = dynamic_cast<const HiraLoop *>(node))
            collectNodes(loop->body(), nodes);
        else if (auto *conditional =
                     dynamic_cast<const HiraIf *>(node)) {
            collectNodes(conditional->thenSequence(), nodes);
            collectNodes(conditional->elseSequence(), nodes);
        }
    }
}

std::vector<const AccessRelation *> accessesIn(
    const PolyhedralModel &model, const HiraLoop &loop) {
    std::set<const HiraNode *> nodes;
    collectNodes(loop.body(), nodes);
    std::set<StatementId> statements;
    for (const PolyhedralStatement &statement : model.statements())
        if (nodes.count(statement.node))
            statements.insert(statement.id);

    std::vector<const AccessRelation *> accesses;
    for (const AccessRelation &access : model.accesses())
        if (statements.count(access.statement))
            accesses.push_back(&access);
    return accesses;
}

bool isOuterSeparatingSubscript(
    const AffineExpr &left, AffineVariable leftDimension,
    const AffineExpr &right, AffineVariable rightDimension) {
    if (!left.valid() || !right.valid() ||
        left.constantTerm() != right.constantTerm())
        return false;

    std::map<AffineVariable, std::int64_t> leftTerms =
        left.coefficients();
    std::map<AffineVariable, std::int64_t> rightTerms =
        right.coefficients();
    auto leftCoefficient = leftTerms.find(leftDimension);
    auto rightCoefficient = rightTerms.find(rightDimension);
    if (leftCoefficient == leftTerms.end() ||
        rightCoefficient == rightTerms.end() ||
        !leftCoefficient->second ||
        leftCoefficient->second != rightCoefficient->second)
        return false;
    leftTerms.erase(leftCoefficient);
    rightTerms.erase(rightCoefficient);

    for (const auto &[variable, coefficient] : leftTerms)
        if (coefficient &&
            variable.kind == AffineVariableKind::Dimension)
            return false;
    for (const auto &[variable, coefficient] : rightTerms)
        if (coefficient &&
            variable.kind == AffineVariableKind::Dimension)
            return false;
    return leftTerms == rightTerms;
}

bool differentOuterIterationsAreDisjoint(
    const AccessRelation &left, AffineVariable leftDimension,
    const AccessRelation &right, AffineVariable rightDimension) {
    if (left.subscripts.size() != right.subscripts.size())
        return false;
    for (std::size_t index = 0;
         index < left.subscripts.size(); ++index)
        if (isOuterSeparatingSubscript(
                left.subscripts[index], leftDimension,
                right.subscripts[index], rightDimension))
            return true;
    return false;
}

bool loopIterationsAreDisjoint(
    const PolyhedralModel &model, const HiraLoop &loop,
    AffineVariable dimension) {
    std::vector<const AccessRelation *> accesses =
        accessesIn(model, loop);
    for (const AccessRelation *write : accesses) {
        if (write->kind != MemoryAccessKind::Write)
            continue;
        for (const AccessRelation *other : accesses) {
            MemoryAliasKind alias =
                model.aliasRelation(
                    write->object, other->object);
            if (alias == MemoryAliasKind::NoAlias)
                continue;
            if (alias != MemoryAliasKind::MustAlias ||
                !differentOuterIterationsAreDisjoint(
                    *write, dimension,
                    *other, dimension))
                return false;
        }
    }
    return true;
}

bool fusionIsLegal(const PolyhedralModel &model,
                   const HiraLoop &first,
                   const HiraLoop &second) {
    if (first.role() != HiraLoop::Role::Ordinary ||
        second.role() != HiraLoop::Role::Ordinary ||
        !first.carriedValues().empty() ||
        !second.carriedValues().empty() ||
        !analyzeCanonicalLoopControl(first) ||
        !analyzeCanonicalLoopControl(second) ||
        !sameValue(first.lowerBound(), second.lowerBound()) ||
        !sameValue(first.upperBound(), second.upperBound()) ||
        !sameValue(first.step(), second.step()))
        return false;

    const IterationDomain *firstDomain =
        domainFor(model, first);
    const IterationDomain *secondDomain =
        domainFor(model, second);
    if (!firstDomain || !secondDomain ||
        !loopIterationsAreDisjoint(
            model, first, firstDomain->dimension) ||
        !loopIterationsAreDisjoint(
            model, second, secondDomain->dimension))
        return false;

    std::vector<const AccessRelation *> firstAccesses =
        accessesIn(model, first);
    std::vector<const AccessRelation *> secondAccesses =
        accessesIn(model, second);
    for (const AccessRelation *left : firstAccesses) {
        for (const AccessRelation *right : secondAccesses) {
            if (left->kind == MemoryAccessKind::Read &&
                right->kind == MemoryAccessKind::Read)
                continue;
            MemoryAliasKind alias =
                model.aliasRelation(left->object, right->object);
            if (alias == MemoryAliasKind::NoAlias)
                continue;
            if (alias != MemoryAliasKind::MustAlias ||
                !differentOuterIterationsAreDisjoint(
                    *left, firstDomain->dimension,
                    *right, secondDomain->dimension))
                return false;
        }
    }
    return true;
}

bool replaceValue(HiraNode &node, HiraValue *from, HiraValue *to);

bool canReplaceValue(const HiraNode &node, const HiraValue *from);

bool canReplaceInSequence(const HiraSequence &sequence,
                         const HiraValue *from) {
    for (const auto &owner : sequence.nodes())
        if (!canReplaceValue(*owner, from))
            return false;
    return true;
}

bool canReplaceValue(const HiraNode &node, const HiraValue *from) {
    if (auto *loop = dynamic_cast<const HiraLoop *>(&node)) {
        if (loop->induction() == from ||
            loop->step() == from)
            return false;
        for (const auto &binding : loop->carriedValues())
            if (binding.iteration == from ||
                binding.yielded == from ||
                binding.result == from)
                return false;
        return canReplaceInSequence(loop->body(), from);
    }
    if (auto *conditional =
            dynamic_cast<const HiraIf *>(&node)) {
        for (const HiraIf::ResultBinding &binding :
             conditional->resultBindings())
            if (binding.thenValue == from ||
                binding.elseValue == from ||
                binding.result == from)
                return false;
        return canReplaceInSequence(
                   conditional->thenSequence(), from) &&
               canReplaceInSequence(
                   conditional->elseSequence(), from);
    }
    return true;
}

bool replaceInSequence(HiraSequence &sequence,
                       HiraValue *from, HiraValue *to) {
    for (const auto &owner : sequence.nodes())
        if (!replaceValue(*owner, from, to))
            return false;
    return true;
}

bool replaceValue(HiraNode &node, HiraValue *from, HiraValue *to) {
    if (auto *loop = dynamic_cast<HiraLoop *>(&node)) {
        if (loop->induction() == from ||
            loop->step() == from)
            return false;
        if (loop->lowerBound() == from)
            loop->setLowerBound(to);
        if (loop->upperBound() == from)
            loop->setUpperBound(to);
        for (std::size_t index = 0;
             index < loop->carriedValues().size(); ++index) {
            const auto &binding = loop->carriedValues()[index];
            if (binding.iteration == from ||
                binding.yielded == from ||
                binding.result == from)
                return false;
            if (binding.initial == from)
                loop->setCarriedInitial(index, to);
        }
        for (std::size_t index = 0;
             index < loop->yieldValues().size(); ++index)
            if (loop->yieldValues()[index] == from)
                loop->setYieldValue(index, to);
        if (!replaceInSequence(loop->body(), from, to))
            return false;
    } else if (auto *conditional =
                   dynamic_cast<HiraIf *>(&node)) {
        for (const HiraIf::ResultBinding &binding :
             conditional->resultBindings())
            if (binding.thenValue == from ||
                binding.elseValue == from ||
                binding.result == from)
                return false;
        if (!replaceInSequence(
                conditional->thenSequence(), from, to) ||
            !replaceInSequence(
                conditional->elseSequence(), from, to))
            return false;
    }

    for (std::size_t index = 0;
         index < node.operands().size(); ++index)
        if (node.operands()[index] == from)
            node.replaceOperand(index, to);
    return true;
}

bool realizeFusion(HiraSequence &sequence,
                   HiraLoop &first, HiraLoop &second) {
    const std::size_t payloadCount =
        second.body().nodes().size() - 2;
    for (std::size_t index = 0;
         index < payloadCount; ++index)
        if (!canReplaceValue(
                *second.body().nodes()[index],
                second.induction()))
            return false;

    std::unique_ptr<HiraNode> secondOwner =
        sequence.remove(&second);
    if (!secondOwner)
        return false;

    std::vector<std::unique_ptr<HiraNode>> payload;
    payload.reserve(payloadCount);
    for (std::size_t index = 0; index < payloadCount; ++index) {
        std::unique_ptr<HiraNode> node =
            second.body().remove(
                second.body().nodes().front().get());
        if (!node ||
            !replaceValue(
                *node, second.induction(),
                first.induction()))
            return false;
        payload.push_back(std::move(node));
    }

    std::size_t insertion = first.body().nodes().size() - 2;
    for (auto &node : payload)
        first.body().insert(insertion++, std::move(node));
    return true;
}

bool fuseInSequence(HiraSequence &sequence,
                    const PolyhedralModel &model) {
    for (std::size_t index = 0;
         index + 1 < sequence.nodes().size(); ++index) {
        auto *first =
            dynamic_cast<HiraLoop *>(
                sequence.nodes()[index].get());
        auto *second =
            dynamic_cast<HiraLoop *>(
                sequence.nodes()[index + 1].get());
        if (first && second &&
            fusionIsLegal(model, *first, *second))
            return realizeFusion(sequence, *first, *second);
    }
    for (const auto &owner : sequence.nodes()) {
        if (auto *loop =
                dynamic_cast<HiraLoop *>(owner.get())) {
            if (fuseInSequence(loop->body(), model))
                return true;
        } else if (auto *conditional =
                       dynamic_cast<HiraIf *>(owner.get())) {
            if (fuseInSequence(
                    conditional->thenSequence(), model) ||
                fuseInSequence(
                    conditional->elseSequence(), model))
                return true;
        }
    }
    return false;
}

} // namespace

ScheduleFusionResult fuseProvablyDisjointAdjacentBands(
    HiraRegion &region, const PolyhedralModel &model) {
    ScheduleFusionResult result;
    result.changed =
        fuseInSequence(region.rootSequence(), model);
    if (result.changed) {
        result.fusedBands = 1;
        region.markModified();
    } else {
        result.detail = "no-provably-disjoint-adjacent-bands";
    }
    return result;
}

} // namespace hira::polyhedral
