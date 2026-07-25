#include "../../../include/mid/hira/polyhedral/statementDependenceGraph.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <sstream>
#include <utility>

namespace hira::polyhedral {
namespace {

class StrongComponentBuilder {
public:
    StrongComponentBuilder(
        std::size_t statementCount,
        const std::vector<StatementDependenceEdge> &edges)
        : adjacency_(statementCount), index_(statementCount, -1),
          lowlink_(statementCount, -1),
          onStack_(statementCount, false) {
        for (const StatementDependenceEdge &edge : edges)
            adjacency_[edge.source].push_back(edge.sink);
        for (auto &successors : adjacency_) {
            std::sort(successors.begin(), successors.end());
            successors.erase(
                std::unique(successors.begin(),
                            successors.end()),
                successors.end());
        }
    }

    std::vector<std::vector<StatementId>> run() {
        for (StatementId statement = 0;
             statement < adjacency_.size(); ++statement)
            if (index_[statement] < 0)
                visit(statement);
        for (auto &component : components_)
            std::sort(component.begin(), component.end());
        std::sort(
            components_.begin(), components_.end(),
            [](const auto &left, const auto &right) {
                return left.front() < right.front();
            });
        return std::move(components_);
    }

private:
    void visit(StatementId statement) {
        index_[statement] = nextIndex_;
        lowlink_[statement] = nextIndex_;
        ++nextIndex_;
        stack_.push_back(statement);
        onStack_[statement] = true;

        for (StatementId successor : adjacency_[statement]) {
            if (index_[successor] < 0) {
                visit(successor);
                lowlink_[statement] =
                    std::min(lowlink_[statement],
                             lowlink_[successor]);
            } else if (onStack_[successor]) {
                lowlink_[statement] =
                    std::min(lowlink_[statement],
                             index_[successor]);
            }
        }

        if (lowlink_[statement] != index_[statement])
            return;
        components_.push_back({});
        while (true) {
            StatementId member = stack_.back();
            stack_.pop_back();
            onStack_[member] = false;
            components_.back().push_back(member);
            if (member == statement)
                break;
        }
    }

    std::vector<std::vector<StatementId>> adjacency_;
    std::vector<std::int64_t> index_;
    std::vector<std::int64_t> lowlink_;
    std::vector<bool> onStack_;
    std::vector<StatementId> stack_;
    std::vector<std::vector<StatementId>> components_;
    std::int64_t nextIndex_ = 0;
};

bool hasSelfEdge(
    StatementId statement,
    const std::vector<StatementDependenceEdge> &edges) {
    return std::any_of(
        edges.begin(), edges.end(),
        [statement](const StatementDependenceEdge &edge) {
            return edge.source == statement &&
                   edge.sink == statement;
        });
}

bool sameGraph(const StatementDependenceGraph &left,
               const StatementDependenceGraph &right) {
    if (left.edges().size() != right.edges().size() ||
        left.components().size() !=
            right.components().size() ||
        left.componentByStatement() !=
            right.componentByStatement())
        return false;
    for (std::size_t index = 0;
         index < left.edges().size(); ++index) {
        const auto &a = left.edges()[index];
        const auto &b = right.edges()[index];
        if (a.dependence != b.dependence ||
            a.source != b.source || a.sink != b.sink)
            return false;
    }
    for (std::size_t index = 0;
         index < left.components().size(); ++index) {
        const auto &a = left.components()[index];
        const auto &b = right.components()[index];
        if (a.id != b.id ||
            a.statements != b.statements ||
            a.cyclic != b.cyclic)
            return false;
    }
    return true;
}

} // namespace

StatementDependenceGraph buildStatementDependenceGraph(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility) {
    StatementDependenceGraph graph;
    for (std::size_t index = 0;
         index < dependences.relations().size(); ++index) {
        if (feasibility.relations()[index].kind ==
            DependenceFeasibilityKind::ProvenEmpty)
            continue;
        const DependenceRelation &relation =
            dependences.relations()[index];
        if (!relation.sourceStatement ||
            !relation.sinkStatement)
            continue;
        graph.edges_.push_back(
            {relation.id, *relation.sourceStatement,
             *relation.sinkStatement});
    }

    StrongComponentBuilder builder(
        model.statements().size(), graph.edges_);
    auto components = builder.run();
    graph.componentByStatement_.assign(
        model.statements().size(),
        std::numeric_limits<StatementComponentId>::max());
    for (std::size_t index = 0;
         index < components.size(); ++index) {
        StatementComponent component;
        component.id =
            static_cast<StatementComponentId>(index);
        component.statements = std::move(components[index]);
        component.cyclic =
            component.statements.size() > 1 ||
            hasSelfEdge(component.statements.front(),
                        graph.edges_);
        for (StatementId statement :
             component.statements)
            graph.componentByStatement_[statement] =
                component.id;
        graph.components_.push_back(std::move(component));
    }
    return graph;
}

bool verifyStatementDependenceGraph(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const StatementDependenceGraph &graph,
    std::string &detail) {
    StatementDependenceGraph expected =
        buildStatementDependenceGraph(
            model, dependences, feasibility);
    if (!sameGraph(graph, expected)) {
        detail = "invalid-statement-dependence-graph";
        return false;
    }
    return true;
}

std::string printStatementDependenceGraph(
    const StatementDependenceGraph &graph) {
    std::ostringstream out;
    out << "polyhedral.statement_graph {\n";
    for (const StatementComponent &component :
         graph.components()) {
        out << "  scc" << component.id << " = [";
        for (std::size_t index = 0;
             index < component.statements.size(); ++index) {
            if (index)
                out << ", ";
            out << "S" << component.statements[index];
        }
        out << "]";
        if (component.cyclic)
            out << " cyclic";
        out << "\n";
    }
    for (const StatementDependenceEdge &edge :
         graph.edges())
        out << "  D" << edge.dependence << ": S"
            << edge.source << " -> S" << edge.sink << "\n";
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
