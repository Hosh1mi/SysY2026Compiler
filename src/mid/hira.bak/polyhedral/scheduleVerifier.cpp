#include "../../../include/mid/hira/polyhedral/scheduleVerifier.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

namespace hira::polyhedral {
namespace {

bool sameComponent(const ScheduleComponent &left,
                   const ScheduleComponent &right) {
    return left.kind == right.kind &&
           left.position == right.position &&
           (left.kind != ScheduleComponentKind::Iteration ||
            left.dimension == right.dimension);
}

bool isKnownBand(
    const PolyhedralModel &model,
    const std::vector<AffineVariable> &dimensions) {
    if (dimensions.size() < 2)
        return false;
    for (const IterationDomain &domain : model.domains()) {
        if (domain.dimensions == dimensions)
            return true;
        if (domain.dimensions.size() < dimensions.size())
            continue;
        for (std::size_t index = 0;
             index + dimensions.size() <=
             domain.dimensions.size();
             ++index) {
            if (std::equal(
                    domain.dimensions.begin() +
                        static_cast<std::ptrdiff_t>(index),
                    domain.dimensions.begin() +
                        static_cast<std::ptrdiff_t>(
                            index + dimensions.size()),
                    dimensions.begin()))
                return true;
        }
    }
    return false;
}

bool contains(
    const std::vector<ScheduleComponent> &components,
    AffineVariable dimension) {
    for (const ScheduleComponent &component : components)
        if (component.kind == ScheduleComponentKind::Iteration &&
            component.dimension == dimension)
            return true;
    return false;
}

AffineVariable expectedDimension(
    AffineVariable original,
    const std::vector<ScheduleComponent> &identity,
    const std::vector<AffineVariable> &originalBand,
    const std::vector<AffineVariable> &scheduledBand) {
    if (originalBand.empty() ||
        originalBand.size() != scheduledBand.size())
        return original;
    for (AffineVariable dimension : originalBand)
        if (!contains(identity, dimension))
            return original;
    auto position = std::find(
        originalBand.begin(), originalBand.end(), original);
    if (position != originalBand.end())
        return scheduledBand[
            static_cast<std::size_t>(
                position - originalBand.begin())];
    return original;
}

std::vector<AffineVariable> scheduledDimensions(
    const StatementSchedule &statement) {
    std::vector<AffineVariable> result;
    for (const ScheduleComponent &component :
         statement.components)
        if (component.kind ==
            ScheduleComponentKind::Iteration)
            result.push_back(component.dimension);
    return result;
}

bool verifyScheduleTree(const ScheduleCandidate &candidate,
                        std::string &detail) {
    const ScheduleTree &tree = candidate.tree;
    const ScheduleTreeNode *root = tree.node(tree.root());
    if (!root ||
        root->kind != ScheduleTreeNodeKind::Sequence ||
        root->parent) {
        detail = "invalid-schedule-tree-root";
        return false;
    }

    std::vector<std::uint32_t> statementUses(
        candidate.statements.size(), 0);
    for (std::size_t index = 0;
         index < tree.nodes().size(); ++index) {
        const ScheduleTreeNode &node = tree.nodes()[index];
        if (node.id != index ||
            (node.id != tree.root() && !node.parent)) {
            detail = "invalid-schedule-tree-node";
            return false;
        }
        for (ScheduleTreeNodeId child : node.children) {
            const ScheduleTreeNode *childNode =
                tree.node(child);
            if (!childNode || childNode->parent != node.id) {
                detail = "invalid-schedule-tree-edge";
                return false;
            }
        }
        if (node.kind != ScheduleTreeNodeKind::Filter)
            continue;
        if (!node.parent) {
            detail = "orphan-schedule-filter";
            return false;
        }
        const ScheduleTreeNode *band =
            tree.node(*node.parent);
        if (!band ||
            band->kind != ScheduleTreeNodeKind::Band) {
            detail = "filter-without-band";
            return false;
        }
        for (StatementId statement : node.statements) {
            if (statement >= candidate.statements.size() ||
                scheduledDimensions(
                    candidate.statements[statement]) !=
                    band->band.dimensions) {
                detail = "invalid-schedule-filter";
                return false;
            }
            ++statementUses[statement];
        }
    }
    for (std::uint32_t uses : statementUses)
        if (uses != 1) {
            detail = "incomplete-schedule-tree";
            return false;
        }
    return true;
}

} // namespace

bool verifyScheduleCandidates(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    std::string &detail) {
    if (schedules.candidates().empty()) {
        detail = "missing-identity-schedule";
        return false;
    }
    for (std::size_t candidateIndex = 0;
         candidateIndex < schedules.candidates().size();
         ++candidateIndex) {
        const ScheduleCandidate &candidate =
            schedules.candidates()[candidateIndex];
        if (candidate.id != candidateIndex ||
            candidate.statements.size() !=
                model.statements().size()) {
            detail = "invalid-schedule-candidate";
            return false;
        }
        if ((candidateIndex == 0) !=
            (candidate.kind ==
             ScheduleCandidateKind::Identity)) {
            detail = "invalid-identity-schedule";
            return false;
        }
        if (!verifyScheduleTree(candidate, detail))
            return false;

        if (candidate.kind !=
            ScheduleCandidateKind::Identity) {
            std::multiset<AffineVariable> original(
                candidate.originalDimensions.begin(),
                candidate.originalDimensions.end());
            std::multiset<AffineVariable> scheduled(
                candidate.scheduledDimensions.begin(),
                candidate.scheduledDimensions.end());
            if (candidate.originalDimensions.size() < 2 ||
                original != scheduled ||
                !isKnownBand(
                    model, candidate.originalDimensions)) {
                detail = "invalid-permutation-dimensions";
                return false;
            }
        }

        for (const PolyhedralStatement &statement :
             model.statements()) {
            const StatementSchedule &scheduled =
                candidate.statements[statement.id];
            if (scheduled.statement != statement.id ||
                scheduled.components.size() !=
                    statement.identitySchedule.size()) {
                detail = "invalid-statement-schedule";
                return false;
            }

            std::multiset<AffineVariable> dimensions;
            for (std::size_t componentIndex = 0;
                 componentIndex < scheduled.components.size();
                 ++componentIndex) {
                const ScheduleComponent &original =
                    statement.identitySchedule[componentIndex];
                const ScheduleComponent &component =
                    scheduled.components[componentIndex];
                if (original.kind ==
                    ScheduleComponentKind::Iteration) {
                    if (component.kind !=
                            ScheduleComponentKind::Iteration ||
                        !(component.dimension ==
                          expectedDimension(
                              original.dimension,
                              statement.identitySchedule,
                              candidate.originalDimensions,
                              candidate.scheduledDimensions))) {
                        detail = "invalid-iteration-component";
                        return false;
                    }
                    dimensions.insert(component.dimension);
                } else if (!sameComponent(original, component)) {
                    detail = "modified-static-component";
                    return false;
                }
            }
            if (dimensions !=
                std::multiset<AffineVariable>(
                    statement.dimensions.begin(),
                    statement.dimensions.end())) {
                detail = "invalid-dimension-permutation";
                return false;
            }
        }
    }
    return true;
}

} // namespace hira::polyhedral
