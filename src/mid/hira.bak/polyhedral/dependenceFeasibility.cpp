#include "../../../include/mid/hira/polyhedral/dependenceFeasibility.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>

namespace hira::polyhedral {
namespace {

using WideInt = __int128;
using WideUInt = unsigned __int128;

WideUInt magnitude(WideInt value) {
    return value < 0 ? static_cast<WideUInt>(-value)
                     : static_cast<WideUInt>(value);
}

WideUInt gcd(WideUInt left, WideUInt right) {
    while (right) {
        WideUInt remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

bool equalityHasGcdConflict(const AffineEquality &equality) {
    // An integer solution can exist only if the GCD of all variable
    // coefficients divides the constant difference.
    WideUInt divisor = 0;
    std::map<std::uint32_t, WideInt> symbols;
    for (const auto &[variable, coefficient] :
         equality.source.coefficients()) {
        if (variable.kind == AffineVariableKind::Dimension)
            divisor = gcd(divisor,
                          magnitude(coefficient));
        else
            symbols[variable.position] += coefficient;
    }
    for (const auto &[variable, coefficient] :
         equality.sink.coefficients()) {
        if (variable.kind == AffineVariableKind::Dimension)
            divisor = gcd(divisor,
                          magnitude(coefficient));
        else
            symbols[variable.position] -= coefficient;
    }
    for (const auto &[position, coefficient] : symbols) {
        (void)position;
        divisor = gcd(divisor, magnitude(coefficient));
    }

    WideInt constant =
        static_cast<WideInt>(equality.source.constantTerm()) -
        static_cast<WideInt>(equality.sink.constantTerm());
    if (!divisor)
        return constant != 0;
    return magnitude(constant) % divisor != 0;
}

std::optional<AffineVariable>
directEqualDimension(const AffineEquality &equality) {
    if (equality.source.constantTerm() !=
        equality.sink.constantTerm())
        return std::nullopt;

    std::map<AffineVariable, std::int64_t> sourceSymbols;
    std::map<AffineVariable, std::int64_t> sinkSymbols;
    std::vector<std::pair<AffineVariable, std::int64_t>>
        sourceDimensions;
    std::vector<std::pair<AffineVariable, std::int64_t>>
        sinkDimensions;
    for (const auto &term : equality.source.coefficients())
        if (term.first.kind == AffineVariableKind::Dimension)
            sourceDimensions.push_back(term);
        else
            sourceSymbols.insert(term);
    for (const auto &term : equality.sink.coefficients())
        if (term.first.kind == AffineVariableKind::Dimension)
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
    return sourceDimensions.front().first;
}

struct IntegerBounds {
    std::optional<WideInt> lower;
    std::optional<WideInt> upper;
};

WideInt floorDivide(WideInt numerator, WideInt denominator) {
    if (numerator >= 0)
        return numerator / denominator;
    return -((-numerator + denominator - 1) / denominator);
}

WideInt ceilDivide(WideInt numerator, WideInt denominator) {
    return -floorDivide(-numerator, denominator);
}

void updateLower(IntegerBounds &bounds, WideInt value) {
    if (!bounds.lower || value > *bounds.lower)
        bounds.lower = value;
}

void updateUpper(IntegerBounds &bounds, WideInt value) {
    if (!bounds.upper || value < *bounds.upper)
        bounds.upper = value;
}

bool addConstraint(
    const AffineConstraint &constraint,
    std::map<std::uint32_t, IntegerBounds> &bounds) {
    std::optional<AffineVariable> dimension;
    WideInt coefficient = 0;
    for (const auto &[variable, value] :
         constraint.expression.coefficients()) {
        // Symbolic and coupled constraints need a general integer solver.
        // Ignoring them cannot turn a feasible relation into a proof of
        // emptiness.
        if (variable.kind == AffineVariableKind::Symbol)
            return true;
        if (dimension)
            return true;
        dimension = variable;
        coefficient = value;
    }

    WideInt constant =
        constraint.expression.constantTerm();
    if (!dimension) {
        return constraint.relation == AffineRelation::EqualZero
                   ? constant == 0
                   : constant >= 0;
    }

    IntegerBounds &current = bounds[dimension->position];
    if (constraint.relation == AffineRelation::EqualZero) {
        WideInt target = -constant;
        if (target % coefficient != 0)
            return false;
        WideInt value = target / coefficient;
        updateLower(current, value);
        updateUpper(current, value);
    } else if (coefficient > 0) {
        updateLower(current,
                    ceilDivide(-constant, coefficient));
    } else {
        updateUpper(current,
                    floorDivide(constant, -coefficient));
    }
    return !current.lower || !current.upper ||
           *current.lower <= *current.upper;
}

bool domainsAreDisjoint(const PolyhedralStatement &source,
                        const PolyhedralStatement &sink) {
    std::map<std::uint32_t, IntegerBounds> bounds;
    for (const AffineConstraint &constraint : source.constraints)
        if (!addConstraint(constraint, bounds))
            return true;
    for (const AffineConstraint &constraint : sink.constraints)
        if (!addConstraint(constraint, bounds))
            return true;
    for (const auto &[dimension, range] : bounds) {
        (void)dimension;
        if (range.lower && range.upper &&
            *range.lower > *range.upper)
            return true;
    }
    return false;
}

enum class StaticOrder {
    Before,
    NotBefore,
    Unknown,
};

StaticOrder compareSchedules(
    const std::vector<ScheduleComponent> &source,
    const std::vector<ScheduleComponent> &sink) {
    std::size_t common =
        std::min(source.size(), sink.size());
    for (std::size_t index = 0; index < common; ++index) {
        const ScheduleComponent &left = source[index];
        const ScheduleComponent &right = sink[index];
        if (left.kind != right.kind)
            return StaticOrder::Unknown;
        if (left.kind == ScheduleComponentKind::Iteration) {
            if (!(left.dimension == right.dimension))
                return StaticOrder::Unknown;
            continue;
        }
        if (left.position < right.position)
            return StaticOrder::Before;
        if (left.position > right.position)
            return StaticOrder::NotBefore;
    }
    if (source.size() == sink.size())
        return StaticOrder::NotBefore;
    return StaticOrder::Unknown;
}

bool allDimensionsEqual(
    const PolyhedralStatement &source,
    const PolyhedralStatement &sink,
    const std::vector<AffineEquality> &equalities) {
    if (source.dimensions != sink.dimensions)
        return false;
    std::map<AffineVariable, bool> equal;
    for (const AffineEquality &equality : equalities)
        if (auto dimension = directEqualDimension(equality))
            equal[*dimension] = true;
    for (AffineVariable dimension : source.dimensions)
        if (!equal[dimension])
            return false;
    return true;
}

const char *kindName(DependenceFeasibilityKind kind) {
    switch (kind) {
    case DependenceFeasibilityKind::Required:
        return "required";
    case DependenceFeasibilityKind::MayExist:
        return "may-exist";
    case DependenceFeasibilityKind::ProvenEmpty:
        return "empty";
    }
    return "unknown";
}

const char *reasonName(DependenceEmptyReason reason) {
    switch (reason) {
    case DependenceEmptyReason::None:
        return "none";
    case DependenceEmptyReason::AffineEqualityConflict:
        return "affine-conflict";
    case DependenceEmptyReason::DisjointDomains:
        return "disjoint-domains";
    case DependenceEmptyReason::IdentityOrderConflict:
        return "identity-order";
    }
    return "unknown";
}

bool isMemory(DependenceKind kind) {
    return kind == DependenceKind::MemoryRAW ||
           kind == DependenceKind::MemoryWAR ||
           kind == DependenceKind::MemoryWAW;
}

} // namespace

DependenceFeasibilityResult
analyzeDependenceFeasibility(
    const PolyhedralModel &model,
    const DependenceSet &dependences) {
    DependenceFeasibilityResult result;
    for (const DependenceRelation &relation :
         dependences.relations()) {
        DependenceFeasibility feasibility;
        feasibility.dependence = relation.id;
        if (!isMemory(relation.kind)) {
            feasibility.kind =
                DependenceFeasibilityKind::Required;
            result.relations_.push_back(feasibility);
            continue;
        }

        bool conflict = false;
        for (const AffineEquality &equality :
             relation.accessEqualities)
            if (equalityHasGcdConflict(equality)) {
                conflict = true;
                break;
            }
        if (conflict) {
            feasibility.kind =
                DependenceFeasibilityKind::ProvenEmpty;
            feasibility.reason =
                DependenceEmptyReason::AffineEqualityConflict;
            result.relations_.push_back(feasibility);
            continue;
        }

        const PolyhedralStatement &source =
            model.statements()[*relation.sourceStatement];
        const PolyhedralStatement &sink =
            model.statements()[*relation.sinkStatement];
        if (relation.precision == DependencePrecision::Exact &&
            allDimensionsEqual(source, sink,
                               relation.accessEqualities)) {
            // Domain intersection and static statement order are comparable
            // only after the access equalities force the same iteration.
            if (domainsAreDisjoint(source, sink)) {
                feasibility.kind =
                    DependenceFeasibilityKind::ProvenEmpty;
                feasibility.reason =
                    DependenceEmptyReason::DisjointDomains;
            } else if (compareSchedules(
                           source.identitySchedule,
                           sink.identitySchedule) !=
                       StaticOrder::Before) {
                feasibility.kind =
                    DependenceFeasibilityKind::ProvenEmpty;
                feasibility.reason =
                    DependenceEmptyReason::
                        IdentityOrderConflict;
            }
        }
        result.relations_.push_back(feasibility);
    }
    return result;
}

bool verifyDependenceFeasibility(
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &result,
    std::string &detail) {
    if (dependences.relations().size() !=
        result.relations().size()) {
        detail = "incomplete-feasibility-results";
        return false;
    }
    for (std::size_t index = 0;
         index < result.relations().size(); ++index) {
        const DependenceFeasibility &relation =
            result.relations()[index];
        if (relation.dependence != index ||
            (relation.kind ==
                 DependenceFeasibilityKind::ProvenEmpty) !=
                (relation.reason != DependenceEmptyReason::None) ||
            (!isMemory(dependences.relations()[index].kind) &&
             relation.kind !=
                 DependenceFeasibilityKind::Required)) {
            detail = "invalid-feasibility-result";
            return false;
        }
    }
    return true;
}

std::string printDependenceFeasibility(
    const DependenceFeasibilityResult &result) {
    std::ostringstream out;
    out << "polyhedral.feasibility {\n";
    for (const DependenceFeasibility &relation :
         result.relations()) {
        out << "  D" << relation.dependence << " = "
            << kindName(relation.kind);
        if (relation.reason != DependenceEmptyReason::None)
            out << " reason=" << reasonName(relation.reason);
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
