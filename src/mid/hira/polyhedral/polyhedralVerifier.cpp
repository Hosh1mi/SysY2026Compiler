#include "../../../include/mid/hira/polyhedral/polyhedralVerifier.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <set>
#include <string>
#include <utility>

namespace hira::polyhedral {
namespace {

PolyhedralVerificationResult
fail(PolyhedralVerifyError error, std::string detail) {
    PolyhedralVerificationResult result;
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

PolyhedralVerificationResult
verifyConstraint(const PolyhedralModel &model,
                 const std::vector<AffineVariable> &dimensions,
                 const AffineConstraint &constraint) {
    if (!constraint.expression.valid())
        return fail(PolyhedralVerifyError::InvalidConstraint,
                    "invalid-expression");

    for (const auto &[variable, coefficient] :
         constraint.expression.coefficients()) {
        if (!coefficient || !model.space().source(variable))
            return fail(PolyhedralVerifyError::InvalidConstraint,
                        "invalid-variable");
        if (variable.kind == AffineVariableKind::Dimension &&
            !contains(dimensions, variable))
            return fail(PolyhedralVerifyError::InvalidConstraint,
                        "out-of-domain-dimension");
    }
    return {};
}

bool validStatementKind(const PolyhedralStatement &statement) {
    switch (statement.kind) {
    case StatementKind::Compute:
        return dynamic_cast<const HiraComputeOp *>(statement.node);
    case StatementKind::Load:
        return dynamic_cast<const HiraLoad *>(statement.node);
    case StatementKind::Store:
        return dynamic_cast<const HiraStore *>(statement.node);
    case StatementKind::Yield:
        return dynamic_cast<const HiraYield *>(statement.node);
    }
    return false;
}

} // namespace

const char *polyhedralVerifyErrorName(PolyhedralVerifyError error) {
    switch (error) {
    case PolyhedralVerifyError::None:
        return "none";
    case PolyhedralVerifyError::InvalidSpace:
        return "invalid-space";
    case PolyhedralVerifyError::InvalidDomain:
        return "invalid-domain";
    case PolyhedralVerifyError::InvalidConstraint:
        return "invalid-constraint";
    case PolyhedralVerifyError::InvalidStatement:
        return "invalid-statement";
    case PolyhedralVerifyError::InvalidMemoryObject:
        return "invalid-memory-object";
    case PolyhedralVerifyError::InvalidAccess:
        return "invalid-access";
    case PolyhedralVerifyError::MissingProofObligation:
        return "missing-proof-obligation";
    }
    return "unknown";
}

PolyhedralVerificationResult
verifyPolyhedralModel(const PolyhedralModel &model) {
    std::set<const HiraValue *> sources;
    for (const HiraValue *dimension : model.space().dimensions())
        if (!dimension || !sources.insert(dimension).second)
            return fail(PolyhedralVerifyError::InvalidSpace,
                        "invalid-dimension-source");
    for (const HiraValue *symbol : model.space().symbols())
        if (!symbol || !sources.insert(symbol).second)
            return fail(PolyhedralVerifyError::InvalidSpace,
                        "invalid-symbol-source");

    std::set<AffineVariable> domainDimensions;
    for (const IterationDomain &domain : model.domains()) {
        if (!domain.loop ||
            domain.dimension.kind !=
                AffineVariableKind::Dimension ||
            model.space().source(domain.dimension) !=
                domain.loop->induction() ||
            domain.dimensions.empty() ||
            !(domain.dimensions.back() == domain.dimension) ||
            !domainDimensions.insert(domain.dimension).second)
            return fail(PolyhedralVerifyError::InvalidDomain,
                        "invalid-loop-dimension");

        std::set<AffineVariable> activeDimensions;
        for (AffineVariable dimension : domain.dimensions)
            if (dimension.kind !=
                    AffineVariableKind::Dimension ||
                !model.space().source(dimension) ||
                !activeDimensions.insert(dimension).second)
                return fail(PolyhedralVerifyError::InvalidDomain,
                            "invalid-domain-space");

        if (domain.constraints.size() <
            domain.dimensions.size() * 2)
            return fail(PolyhedralVerifyError::InvalidDomain,
                        "incomplete-loop-constraints");
        for (const AffineConstraint &constraint :
             domain.constraints) {
            PolyhedralVerificationResult result =
                verifyConstraint(model, domain.dimensions,
                                 constraint);
            if (!result.succeeded())
                return result;
        }

        bool hasNoWrapObligation = false;
        for (const ProofObligation &obligation :
             model.proofObligations())
            if (obligation.kind ==
                    ProofObligationKind::NoSignedWrap &&
                obligation.loop == domain.loop &&
                obligation.dimension == domain.dimension) {
                hasNoWrapObligation = true;
                break;
            }
        if (!hasNoWrapObligation)
            return fail(
                PolyhedralVerifyError::MissingProofObligation,
                "missing-no-signed-wrap");
    }

    if (domainDimensions.size() !=
        model.space().dimensions().size())
        return fail(PolyhedralVerifyError::InvalidSpace,
                    "dimension-without-domain");

    std::vector<std::size_t> accessCounts(
        model.statements().size(), 0);
    std::set<const HiraValue *> memoryBases;
    for (std::size_t index = 0;
         index < model.memoryObjects().size(); ++index) {
        const MemoryObject &object = model.memoryObjects()[index];
        if (object.id != index || !object.base ||
            !dynamic_cast<PointerType *>(object.base->type()) ||
            !memoryBases.insert(object.base).second)
            return fail(PolyhedralVerifyError::InvalidMemoryObject,
                        "invalid-memory-base");
    }

    for (std::size_t index = 0;
         index < model.statements().size(); ++index) {
        const PolyhedralStatement &statement =
            model.statements()[index];
        if (statement.id != index || !statement.node ||
            !validStatementKind(statement))
            return fail(PolyhedralVerifyError::InvalidStatement,
                        "invalid-statement-node");

        std::set<AffineVariable> activeDimensions;
        for (AffineVariable dimension : statement.dimensions)
            if (dimension.kind !=
                    AffineVariableKind::Dimension ||
                !model.space().source(dimension) ||
                !activeDimensions.insert(dimension).second)
                return fail(
                    PolyhedralVerifyError::InvalidStatement,
                    "invalid-statement-space");
        if (statement.constraints.size() <
            statement.dimensions.size() * 2)
            return fail(PolyhedralVerifyError::InvalidStatement,
                        "incomplete-statement-domain");
        for (const AffineConstraint &constraint :
             statement.constraints) {
            PolyhedralVerificationResult result =
                verifyConstraint(model, statement.dimensions,
                                 constraint);
            if (!result.succeeded())
                return result;
        }
    }

    for (const AccessRelation &access : model.accesses()) {
        if (access.statement >= model.statements().size() ||
            access.object >= model.memoryObjects().size())
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "invalid-access-reference");
        const PolyhedralStatement &statement =
            model.statements()[access.statement];
        if ((access.kind == MemoryAccessKind::Read &&
             statement.kind != StatementKind::Load) ||
            (access.kind == MemoryAccessKind::Write &&
             statement.kind != StatementKind::Store))
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "access-kind-mismatch");
        for (const AffineExpr &subscript : access.subscripts) {
            AffineConstraint constraint{
                subscript, AffineRelation::EqualZero};
            PolyhedralVerificationResult result =
                verifyConstraint(model, statement.dimensions,
                                 constraint);
            if (!result.succeeded())
                return fail(PolyhedralVerifyError::InvalidAccess,
                            result.detail);
        }
        ++accessCounts[access.statement];
    }

    for (std::size_t index = 0;
         index < model.statements().size(); ++index) {
        StatementKind kind = model.statements()[index].kind;
        std::size_t expected =
            kind == StatementKind::Load ||
                    kind == StatementKind::Store
                ? 1
                : 0;
        if (accessCounts[index] != expected)
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "incomplete-statement-access");
    }
    return {};
}

} // namespace hira::polyhedral
