#include "../../../include/mid/hira/polyhedral/scheduleApplicability.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"

#include <algorithm>
#include <sstream>

namespace hira::polyhedral {
namespace {

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

ScheduleApplicability unsupported(
    ScheduleCandidateId schedule,
    ScheduleApplicabilityReason reason) {
    return {schedule, ScheduleApplicabilityKind::Unsupported,
            reason};
}

ScheduleApplicability analyzePermutation(
    const PolyhedralModel &model,
    const ScheduleCandidate &candidate) {
    std::vector<const IterationDomain *> domains;
    for (AffineVariable dimension :
         candidate.originalDimensions) {
        const IterationDomain *domain =
            findDomain(model, dimension);
        if (!domain || !domain->loop)
            return unsupported(
                candidate.id,
                ScheduleApplicabilityReason::
                    MissingLoopDomain);
        domains.push_back(domain);
    }
    for (const IterationDomain *domain : domains) {
        if (!isUnitStep(*domain->loop))
            return unsupported(
                candidate.id,
                ScheduleApplicabilityReason::NonUnitStep);
        if (!domain->loop->carriedValues().empty())
            return unsupported(
                candidate.id,
                ScheduleApplicabilityReason::
                    LoopCarriedState);
        if (!analyzeCanonicalLoopControl(*domain->loop))
            return unsupported(
                candidate.id,
                ScheduleApplicabilityReason::
                    NonCanonicalLatch);
    }

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
        } else if (
            candidate.kind ==
                ScheduleCandidateKind::Interchange ||
            candidate.kind ==
                ScheduleCandidateKind::Permutation) {
            result.schedules_.push_back(
                analyzePermutation(model, candidate));
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
