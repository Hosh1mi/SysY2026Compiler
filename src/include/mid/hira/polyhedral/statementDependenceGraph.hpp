#pragma once

#include "dependenceFeasibility.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hira::polyhedral {

using StatementComponentId = std::uint32_t;

struct StatementDependenceEdge {
    DependenceId dependence = 0;
    StatementId source = 0;
    StatementId sink = 0;
};

struct StatementComponent {
    StatementComponentId id = 0;
    std::vector<StatementId> statements;
    bool cyclic = false;
};

class StatementDependenceGraph {
public:
    const std::vector<StatementDependenceEdge> &edges() const {
        return edges_;
    }
    const std::vector<StatementComponent> &components() const {
        return components_;
    }
    const std::vector<StatementComponentId> &
    componentByStatement() const {
        return componentByStatement_;
    }

private:
    friend StatementDependenceGraph
    buildStatementDependenceGraph(
        const PolyhedralModel &model,
        const DependenceSet &dependences,
        const DependenceFeasibilityResult &feasibility);

    std::vector<StatementDependenceEdge> edges_;
    std::vector<StatementComponent> components_;
    std::vector<StatementComponentId> componentByStatement_;
};

StatementDependenceGraph buildStatementDependenceGraph(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility);
bool verifyStatementDependenceGraph(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const StatementDependenceGraph &graph,
    std::string &detail);
std::string printStatementDependenceGraph(
    const StatementDependenceGraph &graph);

} // namespace hira::polyhedral
