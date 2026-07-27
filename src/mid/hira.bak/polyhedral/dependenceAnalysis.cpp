#include "../../../include/mid/hira/polyhedral/dependenceAnalysis.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace hira::polyhedral {
namespace {

const char *kindName(DependenceKind kind) {
    switch (kind) {
    case DependenceKind::ScalarFlow:
        return "scalar";
    case DependenceKind::RecurrenceCarried:
        return "recurrence-carried";
    case DependenceKind::RecurrenceResult:
        return "recurrence-result";
    case DependenceKind::RecurrenceInitialization:
        return "recurrence-init";
    case DependenceKind::MemoryRAW:
        return "RAW";
    case DependenceKind::MemoryWAR:
        return "WAR";
    case DependenceKind::MemoryWAW:
        return "WAW";
    }
    return "unknown";
}

const char *precisionName(DependencePrecision precision) {
    switch (precision) {
    case DependencePrecision::Exact:
        return "exact";
    case DependencePrecision::ConservativeDomain:
        return "opaque-domain";
    case DependencePrecision::ConservativeAlias:
        return "may-alias";
    case DependencePrecision::ConservativeShape:
        return "shape";
    }
    return "unknown";
}

const char *orderingName(DependenceOrdering ordering) {
    switch (ordering) {
    case DependenceOrdering::IdentityBefore:
        return "identity-before";
    case DependenceOrdering::NextIteration:
        return "next-iteration";
    case DependenceOrdering::LastIteration:
        return "last-iteration";
    case DependenceOrdering::LoopEntry:
        return "loop-entry";
    }
    return "unknown";
}

const char *directionName(DependenceDirection direction) {
    switch (direction) {
    case DependenceDirection::Equal:
        return "=";
    case DependenceDirection::Forward:
        return "<";
    case DependenceDirection::Backward:
        return ">";
    case DependenceDirection::Unknown:
        return "*";
    }
    return "?";
}

std::string variableName(AffineVariable variable) {
    return std::string(
               variable.kind == AffineVariableKind::Dimension
                   ? "d"
                   : "s") +
           std::to_string(variable.position);
}

std::string expressionName(const AffineExpr &expression,
                           const char *dimensionPrefix) {
    std::ostringstream out;
    bool emitted = false;
    auto emit = [&](std::int64_t coefficient,
                    const std::string &term) {
        if (!coefficient)
            return;
        if (emitted)
            out << (coefficient < 0 ? " - " : " + ");
        else if (coefficient < 0)
            out << "-";
        std::uint64_t magnitude =
            coefficient < 0
                ? static_cast<std::uint64_t>(
                      -(coefficient + 1)) +
                      1
                : static_cast<std::uint64_t>(coefficient);
        if (magnitude != 1 || term.empty())
            out << magnitude;
        if (!term.empty()) {
            if (magnitude != 1)
                out << "*";
            out << term;
        }
        emitted = true;
    };
    for (const auto &[variable, coefficient] :
         expression.coefficients()) {
        std::string term =
            variable.kind == AffineVariableKind::Dimension
                ? std::string(dimensionPrefix) +
                      std::to_string(variable.position)
                : variableName(variable);
        emit(coefficient, term);
    }
    emit(expression.constantTerm(), {});
    if (!emitted)
        out << "0";
    return out.str();
}

} // namespace

class DependenceBuilder {
public:
    explicit DependenceBuilder(const PolyhedralModel &model)
        : model_(model),
          result_(std::make_unique<DependenceSet>()) {}

    DependenceBuildResult run() {
        if (!buildScalar() || !buildMemory())
            return std::move(failure_);
        DependenceBuildResult result;
        result.dependences = std::move(result_);
        return result;
    }

private:
    bool reject(std::string detail) {
        failure_.error =
            DependenceBuildError::InvalidModelReference;
        failure_.detail = std::move(detail);
        return false;
    }

    DependenceRelation &append(DependenceKind kind) {
        DependenceId id = static_cast<DependenceId>(
            result_->relations_.size());
        result_->relations_.push_back({});
        DependenceRelation &relation =
            result_->relations_.back();
        relation.id = id;
        relation.kind = kind;
        return relation;
    }

    void addEqualDimensions(
        DependenceRelation &relation,
        const std::vector<AffineVariable> &dimensions,
        std::optional<AffineVariable> carried = std::nullopt) {
        for (AffineVariable dimension : dimensions) {
            std::int64_t distance =
                carried && *carried == dimension ? 1 : 0;
            relation.dimensionDistances.push_back(
                {dimension, dimension, distance});
            relation.directionVector.push_back(
                {dimension,
                 distance == 0
                     ? DependenceDirection::Equal
                     : DependenceDirection::Forward,
                 distance});
        }
    }

    void addUnknownCommonDimensions(
        DependenceRelation &relation,
        const PolyhedralStatement &source,
        const PolyhedralStatement &sink) {
        for (AffineVariable sourceDimension :
             source.dimensions)
            if (std::find(sink.dimensions.begin(),
                          sink.dimensions.end(),
                          sourceDimension) !=
                sink.dimensions.end())
                relation.directionVector.push_back(
                    {sourceDimension,
                     DependenceDirection::Unknown,
                     std::nullopt});
    }

    std::optional<std::pair<AffineVariable, std::int64_t>>
    directDistance(const AffineEquality &equality) const {
        std::map<AffineVariable, std::int64_t> sourceSymbols;
        std::map<AffineVariable, std::int64_t> sinkSymbols;
        std::vector<std::pair<AffineVariable, std::int64_t>>
            sourceDimensions;
        std::vector<std::pair<AffineVariable, std::int64_t>>
            sinkDimensions;
        for (const auto &term :
             equality.source.coefficients())
            if (term.first.kind ==
                AffineVariableKind::Dimension)
                sourceDimensions.push_back(term);
            else
                sourceSymbols.insert(term);
        for (const auto &term :
             equality.sink.coefficients())
            if (term.first.kind ==
                AffineVariableKind::Dimension)
                sinkDimensions.push_back(term);
            else
                sinkSymbols.insert(term);
        if (sourceSymbols != sinkSymbols ||
            sourceDimensions.size() != 1 ||
            sinkDimensions.size() != 1 ||
            !(sourceDimensions.front().first ==
              sinkDimensions.front().first) ||
            sourceDimensions.front().second !=
                sinkDimensions.front().second)
            return std::nullopt;

        std::int64_t coefficient =
            sourceDimensions.front().second;
        __int128 difference =
            static_cast<__int128>(
                equality.source.constantTerm()) -
            static_cast<__int128>(
                equality.sink.constantTerm());
        if (!coefficient || difference % coefficient)
            return std::nullopt;
        __int128 distance = difference / coefficient;
        if (distance <
                std::numeric_limits<std::int64_t>::min() ||
            distance >
                std::numeric_limits<std::int64_t>::max())
            return std::nullopt;
        return std::make_pair(
            sourceDimensions.front().first,
            static_cast<std::int64_t>(distance));
    }

    void refineDirections(DependenceRelation &relation) {
        for (const AffineEquality &equality :
             relation.accessEqualities) {
            auto distance = directDistance(equality);
            if (!distance)
                continue;
            for (DirectionComponent &component :
                 relation.directionVector) {
                if (!(component.dimension ==
                      distance->first))
                    continue;
                component.distance = distance->second;
                component.direction =
                    distance->second == 0
                        ? DependenceDirection::Equal
                        : distance->second > 0
                              ? DependenceDirection::Forward
                              : DependenceDirection::Backward;
            }
        }
    }

    bool buildScalar() {
        for (const ScalarFlowRelation &flow :
             model_.scalarFlows()) {
            if (flow.producer >= model_.statements().size() ||
                flow.consumer >= model_.statements().size())
                return reject("scalar-flow");
            DependenceRelation &relation =
                append(DependenceKind::ScalarFlow);
            relation.sourceStatement = flow.producer;
            relation.sinkStatement = flow.consumer;
            relation.ordering =
                DependenceOrdering::IdentityBefore;
            addEqualDimensions(relation,
                               flow.commonDimensions);
        }

        for (const RecurrenceUseRelation &use :
             model_.recurrenceUses()) {
            if (use.recurrence >=
                    model_.scalarRecurrences().size() ||
                use.consumer >= model_.statements().size())
                return reject("recurrence-use");
            const ScalarRecurrence &recurrence =
                model_.scalarRecurrences()[use.recurrence];
            DependenceRelation &relation = append(
                use.kind == RecurrenceValueKind::Iteration
                    ? DependenceKind::RecurrenceCarried
                    : DependenceKind::RecurrenceResult);
            relation.sourceStatement =
                recurrence.yieldStatement;
            relation.sinkStatement = use.consumer;
            relation.sourceRecurrence = recurrence.id;
            relation.ordering =
                use.kind == RecurrenceValueKind::Iteration
                    ? DependenceOrdering::NextIteration
                    : DependenceOrdering::LastIteration;
            addEqualDimensions(
                relation, use.commonDimensions,
                use.kind == RecurrenceValueKind::Iteration
                    ? std::optional<AffineVariable>(
                          recurrence.dimension)
                    : std::nullopt);
        }

        for (const ScalarRecurrence &recurrence :
             model_.scalarRecurrences()) {
            const ScalarValueSource &source =
                recurrence.initialSource;
            if (source.kind == ScalarSourceKind::External ||
                source.kind == ScalarSourceKind::Dimension)
                continue;
            DependenceRelation &relation =
                append(DependenceKind::
                           RecurrenceInitialization);
            relation.sinkRecurrence = recurrence.id;
            relation.ordering = DependenceOrdering::LoopEntry;
            addEqualDimensions(relation,
                               source.commonDimensions);
            if (source.kind == ScalarSourceKind::Statement) {
                relation.sourceStatement = source.source;
            } else {
                relation.sourceRecurrence = source.source;
            }
        }
        return true;
    }

    DependenceKind memoryKind(const AccessRelation &source,
                              const AccessRelation &sink) const {
        if (source.kind == MemoryAccessKind::Write &&
            sink.kind == MemoryAccessKind::Read)
            return DependenceKind::MemoryRAW;
        if (source.kind == MemoryAccessKind::Read &&
            sink.kind == MemoryAccessKind::Write)
            return DependenceKind::MemoryWAR;
        return DependenceKind::MemoryWAW;
    }

    bool buildMemory() {
        const auto &accesses = model_.accesses();
        for (AccessId sourceId = 0;
             sourceId < accesses.size(); ++sourceId) {
            const AccessRelation &source = accesses[sourceId];
            for (AccessId sinkId = 0;
                 sinkId < accesses.size(); ++sinkId) {
                const AccessRelation &sink = accesses[sinkId];
                if (source.kind == MemoryAccessKind::Read &&
                    sink.kind == MemoryAccessKind::Read)
                    continue;
                MemoryAliasKind alias =
                    model_.aliasRelation(source.object, sink.object);
                if (alias == MemoryAliasKind::NoAlias)
                    continue;

                DependenceRelation &relation =
                    append(memoryKind(source, sink));
                relation.sourceStatement = source.statement;
                relation.sinkStatement = sink.statement;
                relation.sourceAccess = sourceId;
                relation.sinkAccess = sinkId;
                relation.ordering =
                    DependenceOrdering::IdentityBefore;
                const PolyhedralStatement &sourceStatement =
                    model_.statements()[source.statement];
                const PolyhedralStatement &sinkStatement =
                    model_.statements()[sink.statement];
                addUnknownCommonDimensions(
                    relation, sourceStatement,
                    sinkStatement);
                if (model_.statements()[source.statement]
                            .domainPrecision !=
                        DomainPrecision::Exact ||
                    model_.statements()[sink.statement]
                            .domainPrecision !=
                        DomainPrecision::Exact)
                    relation.precision =
                        DependencePrecision::ConservativeDomain;

                if (alias == MemoryAliasKind::MayAlias) {
                    relation.precision =
                        DependencePrecision::ConservativeAlias;
                    continue;
                }
                if (source.subscripts.size() !=
                        sink.subscripts.size() ||
                    source.linearizedExtent !=
                        sink.linearizedExtent) {
                    relation.precision =
                        DependencePrecision::ConservativeShape;
                    continue;
                }
                for (std::size_t index = 0;
                     index < source.subscripts.size(); ++index)
                    relation.accessEqualities.push_back(
                        {source.subscripts[index],
                         sink.subscripts[index]});
                refineDirections(relation);
            }
        }
        return true;
    }

    const PolyhedralModel &model_;
    std::unique_ptr<DependenceSet> result_;
    DependenceBuildResult failure_;
};

namespace {

void printEndpoint(std::ostringstream &out,
                   const DependenceRelation &relation) {
    if (relation.sourceStatement)
        out << "S" << *relation.sourceStatement;
    else if (relation.sourceRecurrence)
        out << "R" << *relation.sourceRecurrence;
    else
        out << "<entry>";
    out << " -> ";
    if (relation.sinkStatement)
        out << "S" << *relation.sinkStatement;
    else if (relation.sinkRecurrence)
        out << "R" << *relation.sinkRecurrence;
    else
        out << "<exit>";
}

} // namespace

const char *dependenceBuildErrorName(DependenceBuildError error) {
    switch (error) {
    case DependenceBuildError::None:
        return "none";
    case DependenceBuildError::InvalidModelReference:
        return "invalid-model-reference";
    }
    return "unknown";
}

DependenceBuildResult
buildDependenceRelations(const PolyhedralModel &model) {
    return DependenceBuilder(model).run();
}

std::string
printDependenceRelations(const DependenceSet &dependences) {
    std::ostringstream out;
    out << "polyhedral.dependences {\n";
    for (const DependenceRelation &relation :
         dependences.relations()) {
        out << "  D" << relation.id << " "
            << kindName(relation.kind) << " ";
        printEndpoint(out, relation);
        out << " ordering=" << orderingName(relation.ordering)
            << " precision=" << precisionName(relation.precision);
        if (!relation.dimensionDistances.empty()) {
            out << " distance=[";
            for (std::size_t index = 0;
                 index < relation.dimensionDistances.size();
                 ++index) {
                if (index)
                    out << ", ";
                const DimensionDistance &distance =
                    relation.dimensionDistances[index];
                out << variableName(distance.source) << "->"
                    << variableName(distance.sink) << ":"
                    << distance.distance;
            }
            out << "]";
        }
        if (!relation.directionVector.empty()) {
            out << " direction=[";
            for (std::size_t index = 0;
                 index < relation.directionVector.size();
                 ++index) {
                if (index)
                    out << ", ";
                const DirectionComponent &component =
                    relation.directionVector[index];
                out << variableName(component.dimension)
                    << ":" << directionName(
                           component.direction);
                if (component.distance)
                    out << "(" << *component.distance << ")";
            }
            out << "]";
        }
        if (!relation.accessEqualities.empty()) {
            out << " equal=[";
            for (std::size_t index = 0;
                 index < relation.accessEqualities.size(); ++index) {
                if (index)
                    out << ", ";
                out << expressionName(
                           relation.accessEqualities[index].source,
                           "src.d")
                    << " == "
                    << expressionName(
                           relation.accessEqualities[index].sink,
                           "dst.d");
            }
            out << "]";
        }
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
