#include "../../../include/mid/hira/polyhedral/polyhedralVerifier.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"

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
                 const IterationDomain &domain,
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
            !contains(domain.dimensions, variable))
            return fail(PolyhedralVerifyError::InvalidConstraint,
                        "out-of-domain-dimension");
    }
    return {};
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
                verifyConstraint(model, domain, constraint);
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
    return {};
}

} // namespace hira::polyhedral
