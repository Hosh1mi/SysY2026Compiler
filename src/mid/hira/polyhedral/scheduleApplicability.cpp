#include "../../../include/mid/hira/polyhedral/scheduleApplicability.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <sstream>

namespace hira::polyhedral {
namespace {

bool sameExpression(const AffineExpr &left,
                    const AffineExpr &right) {
    return left.valid() == right.valid() &&
           left.constantTerm() == right.constantTerm() &&
           left.coefficients() == right.coefficients();
}

bool sameConstraint(const AffineConstraint &left,
                    const AffineConstraint &right) {
    return left.relation == right.relation &&
           sameExpression(left.expression, right.expression);
}

const IterationDomain *findDomain(
    const PolyhedralModel &model,
    AffineVariable dimension) {
    for (const IterationDomain &domain : model.domains())
        if (domain.dimension == dimension)
            return &domain;
    return nullptr;
}

bool isIntegerValue(const HiraValue *value,
                    std::int64_t expected) {
    return value &&
           value->kind() == ValueKind::IntegerConstant &&
           value->integerValue() == expected;
}

bool isUnitStep(const HiraLoop &loop) {
    return isIntegerValue(loop.step(), 1);
}

bool hasCoupledBounds(const IterationDomain &outer,
                      const IterationDomain &inner) {
    if (inner.constraints.size() !=
        outer.constraints.size() + 2)
        return true;
    for (std::size_t index = 0;
         index < outer.constraints.size(); ++index)
        if (!sameConstraint(outer.constraints[index],
                            inner.constraints[index]))
            return true;
    for (std::size_t index = outer.constraints.size();
         index < inner.constraints.size(); ++index) {
        auto coefficient =
            inner.constraints[index]
                .expression.coefficients()
                .find(outer.dimension);
        if (coefficient !=
                inner.constraints[index]
                    .expression.coefficients()
                    .end() &&
            coefficient->second)
            return true;
    }
    return false;
}

ScheduleApplicability unsupported(
    ScheduleCandidateId schedule,
    ScheduleApplicabilityReason reason) {
    return {schedule, ScheduleApplicabilityKind::Unsupported,
            reason};
}

ScheduleApplicability analyzeInterchange(
    const PolyhedralModel &model,
    const ScheduleCandidate &candidate) {
    const IterationDomain *outer =
        findDomain(model, candidate.outerDimension);
    const IterationDomain *inner =
        findDomain(model, candidate.innerDimension);
    if (!outer || !inner || !outer->loop || !inner->loop)
        return unsupported(
            candidate.id,
            ScheduleApplicabilityReason::MissingLoopDomain);
    if (!isUnitStep(*outer->loop) ||
        !isUnitStep(*inner->loop))
        return unsupported(
            candidate.id,
            ScheduleApplicabilityReason::NonUnitStep);
    if (!outer->loop->carriedValues().empty() ||
        !inner->loop->carriedValues().empty())
        return unsupported(
            candidate.id,
            ScheduleApplicabilityReason::LoopCarriedState);
    if (!isPerfectLoopNest(*outer->loop, *inner->loop))
        return unsupported(
            candidate.id,
            ScheduleApplicabilityReason::ImperfectNest);
    if (!analyzeCanonicalLoopControl(*outer->loop) ||
        !analyzeCanonicalLoopControl(*inner->loop))
        return unsupported(
            candidate.id,
            ScheduleApplicabilityReason::NonCanonicalLatch);
    if (hasCoupledBounds(*outer, *inner))
        return unsupported(
            candidate.id,
            ScheduleApplicabilityReason::CoupledBounds);
    return {candidate.id,
            ScheduleApplicabilityKind::Realizable,
            ScheduleApplicabilityReason::None};
}

const char *kindName(ScheduleApplicabilityKind kind) {
    switch (kind) {
    case ScheduleApplicabilityKind::Realizable:
        return "realizable";
    case ScheduleApplicabilityKind::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

const char *reasonName(ScheduleApplicabilityReason reason) {
    switch (reason) {
    case ScheduleApplicabilityReason::None:
        return "none";
    case ScheduleApplicabilityReason::UnsupportedCandidate:
        return "unsupported-candidate";
    case ScheduleApplicabilityReason::MissingLoopDomain:
        return "missing-loop-domain";
    case ScheduleApplicabilityReason::NonUnitStep:
        return "non-unit-step";
    case ScheduleApplicabilityReason::LoopCarriedState:
        return "loop-carried-state";
    case ScheduleApplicabilityReason::ImperfectNest:
        return "imperfect-nest";
    case ScheduleApplicabilityReason::NonCanonicalLatch:
        return "non-canonical-latch";
    case ScheduleApplicabilityReason::CoupledBounds:
        return "coupled-bounds";
    }
    return "unknown";
}

} // namespace

ScheduleApplicabilityResult analyzeScheduleApplicability(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules) {
    ScheduleApplicabilityResult result;
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        if (candidate.kind ==
            ScheduleCandidateKind::Identity) {
            result.schedules_.push_back(
                {candidate.id,
                 ScheduleApplicabilityKind::Realizable,
                 ScheduleApplicabilityReason::None});
        } else if (candidate.kind ==
                   ScheduleCandidateKind::Interchange) {
            result.schedules_.push_back(
                analyzeInterchange(model, candidate));
        } else {
            result.schedules_.push_back(unsupported(
                candidate.id,
                ScheduleApplicabilityReason::
                    UnsupportedCandidate));
        }
    }
    return result;
}

bool verifyScheduleApplicability(
    const ScheduleCandidateSet &schedules,
    const ScheduleApplicabilityResult &result,
    std::string &detail) {
    if (result.schedules().size() !=
        schedules.candidates().size()) {
        detail = "incomplete-schedule-applicability";
        return false;
    }
    for (std::size_t index = 0;
         index < result.schedules().size(); ++index) {
        const ScheduleApplicability &applicability =
            result.schedules()[index];
        if (applicability.schedule != index ||
            (applicability.kind ==
                 ScheduleApplicabilityKind::Realizable) !=
                (applicability.reason ==
                 ScheduleApplicabilityReason::None) ||
            (index == 0 &&
             applicability.kind !=
                 ScheduleApplicabilityKind::Realizable)) {
            detail = "invalid-schedule-applicability";
            return false;
        }
    }
    return true;
}

std::string printScheduleApplicability(
    const ScheduleApplicabilityResult &result) {
    std::ostringstream out;
    out << "polyhedral.schedule_applicability {\n";
    for (const ScheduleApplicability &applicability :
         result.schedules()) {
        out << "  C" << applicability.schedule << " = "
            << kindName(applicability.kind);
        if (applicability.reason !=
            ScheduleApplicabilityReason::None)
            out << " reason="
                << reasonName(applicability.reason);
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
