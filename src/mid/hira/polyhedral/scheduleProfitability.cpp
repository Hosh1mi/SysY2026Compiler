#include "../../../include/mid/hira/polyhedral/scheduleProfitability.hpp"

#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>

namespace hira::polyhedral {
namespace {

const StatementSchedule *statementSchedule(
    const ScheduleCandidate &candidate,
    StatementId statement) {
    if (statement >= candidate.statements.size() ||
        candidate.statements[statement].statement != statement)
        return nullptr;
    return &candidate.statements[statement];
}

std::optional<AffineVariable> innermostDimension(
    const ScheduleCandidate &candidate,
    StatementId statement) {
    const StatementSchedule *schedule =
        statementSchedule(candidate, statement);
    if (!schedule)
        return std::nullopt;
    for (auto component = schedule->components.rbegin();
         component != schedule->components.rend(); ++component)
        if (component->kind ==
            ScheduleComponentKind::Iteration)
            return component->dimension;
    return std::nullopt;
}

std::size_t iterationDepth(
    const ScheduleCandidate &candidate,
    StatementId statement) {
    const StatementSchedule *schedule =
        statementSchedule(candidate, statement);
    if (!schedule)
        return 0;
    std::size_t depth = 0;
    for (const ScheduleComponent &component :
         schedule->components)
        depth += component.kind ==
                 ScheduleComponentKind::Iteration;
    return depth;
}

ScheduleProfitability unknown(
    ScheduleCandidateId schedule,
    ScheduleProfitabilityReason reason) {
    return {schedule, ScheduleProfitabilityKind::Unknown,
            reason, 0, {}};
}

const char *kindName(ScheduleProfitabilityKind kind) {
    switch (kind) {
    case ScheduleProfitabilityKind::Baseline:
        return "baseline";
    case ScheduleProfitabilityKind::ProvenBeneficial:
        return "beneficial";
    case ScheduleProfitabilityKind::Neutral:
        return "neutral";
    case ScheduleProfitabilityKind::Regressing:
        return "regressing";
    case ScheduleProfitabilityKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char *reasonName(ScheduleProfitabilityReason reason) {
    switch (reason) {
    case ScheduleProfitabilityReason::None:
        return "none";
    case ScheduleProfitabilityReason::NoMemoryAccess:
        return "no-memory-access";
    case ScheduleProfitabilityReason::UnknownStride:
        return "unknown-stride";
    case ScheduleProfitabilityReason::NoStrictImprovement:
        return "no-strict-improvement";
    case ScheduleProfitabilityReason::AccessStrideRegression:
        return "access-stride-regression";
    }
    return "unknown";
}

} // namespace

ScheduleProfitabilityResult analyzeScheduleProfitability(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules) {
    ScheduleProfitabilityResult result;
    if (schedules.candidates().empty())
        return result;

    const ScheduleCandidate &identity =
        schedules.candidates().front();
    std::size_t maximumAccessDepth = 0;
    for (const AccessRelation &access : model.accesses())
        maximumAccessDepth = std::max(
            maximumAccessDepth,
            iterationDepth(identity, access.statement));
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        ScheduleProfitability profitability;
        profitability.schedule = candidate.id;
        profitability.kind =
            ScheduleProfitabilityKind::Neutral;
        if (model.accesses().empty()) {
            profitability.kind =
                candidate.id == identity.id
                    ? ScheduleProfitabilityKind::Baseline
                    : ScheduleProfitabilityKind::Neutral;
            profitability.reason =
                ScheduleProfitabilityReason::NoMemoryAccess;
            result.schedules_.push_back(
                std::move(profitability));
            continue;
        }

        bool regression = false;
        bool improvement = false;
        std::int64_t totalReduction = 0;
        bool reductionOverflow = false;
        for (std::uint32_t accessId = 0;
             accessId < model.accesses().size(); ++accessId) {
            const AccessRelation &access =
                model.accesses()[accessId];
            auto baselineDimension = innermostDimension(
                identity, access.statement);
            auto candidateDimension = innermostDimension(
                candidate, access.statement);
            if (!baselineDimension || !candidateDimension) {
                profitability = unknown(
                    candidate.id,
                    ScheduleProfitabilityReason::UnknownStride);
                break;
            }
            auto baseline = analyzeLinearAccessStride(
                model, access, *baselineDimension);
            auto current = analyzeLinearAccessStride(
                model, access, *candidateDimension);
            if (!baseline || !current) {
                profitability = unknown(
                    candidate.id,
                    ScheduleProfitabilityReason::UnknownStride);
                break;
            }
            profitability.accesses.push_back(
                {accessId, *baseline, *current});
            // Compare the highest-complexity memory operations first. A
            // stride change in an O(n^k) statement asymptotically dominates
            // any number of changes in statements nested fewer than k
            // iteration dimensions. Lower-order accesses are still reported,
            // but they must not veto a schedule that preserves the hot tier.
            // Only innermost read regressions are treated as blockers: a
            // strided store is acceptable when the innermost load becomes
            // contiguous after interchange.
            const bool highestOrder =
                iterationDepth(identity, access.statement) ==
                maximumAccessDepth;
            if (highestOrder && *current > *baseline &&
                access.kind == MemoryAccessKind::Read)
                regression = true;
            if (highestOrder && *current < *baseline) {
                improvement = true;
                std::int64_t reduction =
                    *baseline - *current;
                if (totalReduction >
                    std::numeric_limits<std::int64_t>::max() -
                        reduction) {
                    reductionOverflow = true;
                    break;
                }
                totalReduction += reduction;
            }
        }
        if (reductionOverflow)
            profitability = unknown(
                candidate.id,
                ScheduleProfitabilityReason::UnknownStride);
        if (profitability.kind ==
            ScheduleProfitabilityKind::Unknown) {
            result.schedules_.push_back(
                std::move(profitability));
            continue;
        }
        if (candidate.id == identity.id) {
            profitability.kind =
                ScheduleProfitabilityKind::Baseline;
        } else if (regression) {
            profitability.kind =
                ScheduleProfitabilityKind::Regressing;
            profitability.reason =
                ScheduleProfitabilityReason::
                    AccessStrideRegression;
        } else if (!improvement) {
            profitability.kind =
                ScheduleProfitabilityKind::Neutral;
            profitability.reason =
                ScheduleProfitabilityReason::
                    NoStrictImprovement;
        } else {
            profitability.kind =
                ScheduleProfitabilityKind::ProvenBeneficial;
            profitability.totalStrideReduction =
                totalReduction;
        }
        result.schedules_.push_back(std::move(profitability));
    }
    return result;
}

bool verifyScheduleProfitability(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleProfitabilityResult &result,
    std::string &detail) {
    if (result.schedules().size() !=
        schedules.candidates().size()) {
        detail = "incomplete-schedule-profitability";
        return false;
    }
    for (std::size_t index = 0;
         index < result.schedules().size(); ++index) {
        const ScheduleProfitability &profitability =
            result.schedules()[index];
        if (profitability.schedule != index ||
            (index == 0 &&
             profitability.kind !=
                 ScheduleProfitabilityKind::Baseline) ||
            (profitability.kind ==
                 ScheduleProfitabilityKind::ProvenBeneficial &&
             (profitability.reason !=
                  ScheduleProfitabilityReason::None ||
              profitability.totalStrideReduction <= 0)) ||
            (profitability.kind ==
                 ScheduleProfitabilityKind::Regressing &&
             profitability.reason !=
                 ScheduleProfitabilityReason::
                     AccessStrideRegression) ||
            (profitability.accesses.size() !=
                 model.accesses().size() &&
             profitability.kind !=
                 ScheduleProfitabilityKind::Unknown)) {
            detail = "invalid-schedule-profitability";
            return false;
        }
        for (std::size_t accessIndex = 0;
             accessIndex < profitability.accesses.size();
             ++accessIndex)
            if (profitability.accesses[accessIndex].access !=
                accessIndex) {
                detail = "invalid-access-stride";
                return false;
            }
    }
    return true;
}

std::string printScheduleProfitability(
    const ScheduleProfitabilityResult &result) {
    std::ostringstream out;
    out << "polyhedral.schedule_profitability {\n";
    for (const ScheduleProfitability &profitability :
         result.schedules()) {
        out << "  C" << profitability.schedule << " = "
            << kindName(profitability.kind);
        if (profitability.reason !=
            ScheduleProfitabilityReason::None)
            out << " reason="
                << reasonName(profitability.reason);
        if (profitability.totalStrideReduction > 0)
            out << " reduction="
                << profitability.totalStrideReduction << "B";
        if (!profitability.accesses.empty()) {
            out << " strides=[";
            for (std::size_t index = 0;
                 index < profitability.accesses.size(); ++index) {
                if (index)
                    out << ", ";
                const AccessStrideChange &access =
                    profitability.accesses[index];
                out << "A" << access.access << ":"
                    << access.baselineBytes << "B->"
                    << access.candidateBytes << "B";
            }
            out << "]";
        }
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
