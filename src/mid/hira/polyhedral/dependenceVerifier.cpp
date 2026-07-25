#include "../../../include/mid/hira/polyhedral/dependenceVerifier.hpp"

#include "../../../include/mid/hira/polyhedral/dependenceAnalysis.hpp"

#include <string>
#include <utility>
#include <vector>

namespace hira::polyhedral {
namespace {

DependenceVerificationResult
fail(DependenceVerifyError error, std::string detail) {
    DependenceVerificationResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

bool contains(const std::vector<AffineVariable> &variables,
              AffineVariable variable) {
    for (AffineVariable candidate : variables)
        if (candidate == variable)
            return true;
    return false;
}

const std::vector<AffineVariable> *sourceDimensions(
    const PolyhedralModel &model,
    const DependenceRelation &relation) {
    if (relation.sourceStatement)
        return &model.statements()[*relation.sourceStatement]
                    .dimensions;
    if (relation.sourceRecurrence)
        return &model.scalarRecurrences()[
                    *relation.sourceRecurrence]
                    .dimensions;
    return nullptr;
}

const std::vector<AffineVariable> *sinkDimensions(
    const PolyhedralModel &model,
    const DependenceRelation &relation) {
    if (relation.sinkStatement)
        return &model.statements()[*relation.sinkStatement]
                    .dimensions;
    if (relation.sinkRecurrence)
        return &model.scalarRecurrences()[
                    *relation.sinkRecurrence]
                    .dimensions;
    return nullptr;
}

DependenceKind memoryKind(const AccessRelation &source,
                          const AccessRelation &sink) {
    if (source.kind == MemoryAccessKind::Write &&
        sink.kind == MemoryAccessKind::Read)
        return DependenceKind::MemoryRAW;
    if (source.kind == MemoryAccessKind::Read &&
        sink.kind == MemoryAccessKind::Write)
        return DependenceKind::MemoryWAR;
    return DependenceKind::MemoryWAW;
}

bool isMemory(DependenceKind kind) {
    return kind == DependenceKind::MemoryRAW ||
           kind == DependenceKind::MemoryWAR ||
           kind == DependenceKind::MemoryWAW;
}

bool verifyExpression(
    const PolyhedralModel &model, const AffineExpr &expression,
    const std::vector<AffineVariable> &dimensions) {
    if (!expression.valid())
        return false;
    for (const auto &[variable, coefficient] :
         expression.coefficients()) {
        if (!coefficient || !model.space().source(variable))
            return false;
        if (variable.kind == AffineVariableKind::Dimension &&
            !contains(dimensions, variable))
            return false;
    }
    return true;
}

bool sameExpression(const AffineExpr &first,
                    const AffineExpr &second) {
    return first.valid() == second.valid() &&
           first.constantTerm() == second.constantTerm() &&
           first.coefficients() == second.coefficients();
}

} // namespace

const char *dependenceVerifyErrorName(DependenceVerifyError error) {
    switch (error) {
    case DependenceVerifyError::None:
        return "none";
    case DependenceVerifyError::InvalidRelation:
        return "invalid-relation";
    case DependenceVerifyError::InvalidEndpoint:
        return "invalid-endpoint";
    case DependenceVerifyError::InvalidConstraint:
        return "invalid-constraint";
    }
    return "unknown";
}

DependenceVerificationResult
verifyDependenceRelations(const PolyhedralModel &model,
                          const DependenceSet &dependences) {
    std::size_t expectedRelations =
        model.scalarFlows().size() +
        model.recurrenceUses().size();
    for (const ScalarRecurrence &recurrence :
         model.scalarRecurrences())
        if (recurrence.initialSource.kind ==
                ScalarSourceKind::Statement ||
            recurrence.initialSource.kind ==
                ScalarSourceKind::RecurrenceIteration ||
            recurrence.initialSource.kind ==
                ScalarSourceKind::RecurrenceResult)
            ++expectedRelations;
    for (const AccessRelation &source : model.accesses())
        for (const AccessRelation &sink : model.accesses())
            if (!(source.kind == MemoryAccessKind::Read &&
                  sink.kind == MemoryAccessKind::Read) &&
                model.aliasRelation(source.object, sink.object) !=
                    MemoryAliasKind::NoAlias)
                ++expectedRelations;
    if (dependences.relations().size() != expectedRelations)
        return fail(DependenceVerifyError::InvalidRelation,
                    "incomplete-dependence-set");

    for (std::size_t index = 0;
         index < dependences.relations().size(); ++index) {
        const DependenceRelation &relation =
            dependences.relations()[index];
        if (relation.id != index)
            return fail(DependenceVerifyError::InvalidRelation,
                        "invalid-dependence-id");
        if ((relation.sourceStatement &&
             *relation.sourceStatement >=
                 model.statements().size()) ||
            (relation.sinkStatement &&
             *relation.sinkStatement >=
                 model.statements().size()) ||
            (relation.sourceRecurrence &&
             *relation.sourceRecurrence >=
                 model.scalarRecurrences().size()) ||
            (relation.sinkRecurrence &&
             *relation.sinkRecurrence >=
                 model.scalarRecurrences().size()))
            return fail(DependenceVerifyError::InvalidEndpoint,
                        "out-of-range-endpoint");

        const auto *sourceSpace =
            sourceDimensions(model, relation);
        const auto *sinkSpace =
            sinkDimensions(model, relation);
        for (const DimensionDistance &distance :
             relation.dimensionDistances)
            if (!sourceSpace || !sinkSpace ||
                distance.source.kind !=
                    AffineVariableKind::Dimension ||
                distance.sink.kind !=
                    AffineVariableKind::Dimension ||
                !contains(*sourceSpace, distance.source) ||
                !contains(*sinkSpace, distance.sink))
                return fail(
                    DependenceVerifyError::InvalidConstraint,
                    "invalid-dimension-distance");

        if (isMemory(relation.kind)) {
            if (!relation.sourceAccess ||
                !relation.sinkAccess ||
                *relation.sourceAccess >= model.accesses().size() ||
                *relation.sinkAccess >= model.accesses().size() ||
                !relation.sourceStatement ||
                !relation.sinkStatement ||
                relation.ordering !=
                    DependenceOrdering::IdentityBefore)
                return fail(DependenceVerifyError::InvalidEndpoint,
                            "invalid-memory-endpoint");
            const AccessRelation &source =
                model.accesses()[*relation.sourceAccess];
            const AccessRelation &sink =
                model.accesses()[*relation.sinkAccess];
            MemoryAliasKind alias =
                model.aliasRelation(source.object, sink.object);
            if (source.statement !=
                    *relation.sourceStatement ||
                sink.statement != *relation.sinkStatement ||
                relation.kind != memoryKind(source, sink) ||
                alias == MemoryAliasKind::NoAlias)
                return fail(DependenceVerifyError::InvalidRelation,
                            "mismatched-memory-relation");

            DependencePrecision expectedPrecision =
                alias == MemoryAliasKind::MayAlias
                    ? DependencePrecision::ConservativeAlias
                    : source.subscripts.size() !=
                              sink.subscripts.size()
                          ? DependencePrecision::
                                ConservativeShape
                          : (model.statements()[source.statement]
                                         .domainPrecision !=
                                     DomainPrecision::Exact ||
                                 model.statements()[sink.statement]
                                         .domainPrecision !=
                                     DomainPrecision::Exact)
                                ? DependencePrecision::
                                      ConservativeDomain
                                : DependencePrecision::Exact;
            std::size_t expectedEqualities =
                expectedPrecision == DependencePrecision::Exact ||
                        expectedPrecision ==
                            DependencePrecision::
                                ConservativeDomain
                    ? source.subscripts.size()
                    : 0;
            if (relation.precision != expectedPrecision ||
                relation.accessEqualities.size() !=
                    expectedEqualities)
                return fail(DependenceVerifyError::InvalidConstraint,
                            "invalid-access-equalities");
            for (std::size_t equalityIndex = 0;
                 equalityIndex <
                 relation.accessEqualities.size();
                 ++equalityIndex) {
                const AffineEquality &equality =
                    relation.accessEqualities[equalityIndex];
                if (!verifyExpression(model, equality.source,
                                      *sourceSpace) ||
                    !verifyExpression(model, equality.sink,
                                      *sinkSpace) ||
                    !sameExpression(
                        equality.source,
                        source.subscripts[equalityIndex]) ||
                    !sameExpression(
                        equality.sink,
                        sink.subscripts[equalityIndex]))
                    return fail(
                        DependenceVerifyError::InvalidConstraint,
                        "invalid-access-expression");
            }
            continue;
        }

        if (relation.sourceAccess || relation.sinkAccess ||
            relation.precision != DependencePrecision::Exact)
            return fail(DependenceVerifyError::InvalidRelation,
                        "invalid-scalar-relation");
        switch (relation.kind) {
        case DependenceKind::ScalarFlow:
            if (!relation.sourceStatement ||
                !relation.sinkStatement ||
                relation.sourceRecurrence ||
                relation.sinkRecurrence ||
                relation.ordering !=
                    DependenceOrdering::IdentityBefore)
                return fail(
                    DependenceVerifyError::InvalidEndpoint,
                    "invalid-scalar-flow");
            break;
        case DependenceKind::RecurrenceCarried:
        case DependenceKind::RecurrenceResult:
            if (!relation.sourceStatement ||
                !relation.sinkStatement ||
                !relation.sourceRecurrence ||
                relation.sinkRecurrence ||
                relation.ordering !=
                    (relation.kind ==
                             DependenceKind::RecurrenceCarried
                         ? DependenceOrdering::NextIteration
                         : DependenceOrdering::LastIteration))
                return fail(
                    DependenceVerifyError::InvalidEndpoint,
                    "invalid-recurrence-use");
            break;
        case DependenceKind::RecurrenceInitialization:
            if (relation.sinkStatement ||
                !relation.sinkRecurrence ||
                (!relation.sourceStatement &&
                 !relation.sourceRecurrence) ||
                relation.ordering !=
                    DependenceOrdering::LoopEntry)
                return fail(
                    DependenceVerifyError::InvalidEndpoint,
                    "invalid-recurrence-init");
            break;
        default:
            return fail(DependenceVerifyError::InvalidRelation,
                        "unexpected-relation-kind");
        }
    }
    return {};
}

} // namespace hira::polyhedral
