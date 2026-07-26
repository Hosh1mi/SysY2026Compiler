#include "../../../include/mid/hira/polyhedral/scheduleSelection.hpp"

#include <sstream>

namespace hira::polyhedral {
namespace {

bool preservesBaselineCapabilities(
    std::size_t index,
    const ScheduleParallelismResult &parallelism,
    const CacheFootprintResult &cacheFootprints,
    const VectorizationAnalysisResult &vectorization) {
    const ScheduleParallelism &baselineParallel =
        parallelism.schedules().front();
    const ScheduleParallelism &candidateParallel =
        parallelism.schedules()[index];
    if (baselineParallel.outerParallel &&
        !candidateParallel.outerParallel)
        return false;

    const ScheduleVectorization &baselineVector =
        vectorization.schedules().front();
    const ScheduleVectorization &candidateVector =
        vectorization.schedules()[index];
    if (baselineVector.kind ==
            VectorizationKind::Vectorizable &&
        (candidateVector.kind !=
             VectorizationKind::Vectorizable ||
         candidateVector.lanes < baselineVector.lanes))
        return false;

    const CacheFootprint &baselineCache =
        cacheFootprints.schedules().front();
    const CacheFootprint &candidateCache =
        cacheFootprints.schedules()[index];
    return baselineCache.kind != CacheFootprintKind::Known ||
           candidateCache.kind == CacheFootprintKind::Known;
}

bool qualifies(
    std::size_t index,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleParallelismResult &parallelism,
    const ScheduleProfitabilityResult &profitability,
    const CacheFootprintResult &cacheFootprints,
    const VectorizationAnalysisResult &vectorization) {
    bool baselineParallel =
        parallelism.schedules().front().outerParallel;
    bool gainsParallelism =
        !baselineParallel &&
        parallelism.schedules()[index].outerParallel;
    ScheduleProfitabilityKind profitabilityKind =
        profitability.schedules()[index].kind;
    return legality.schedules()[index].kind ==
               ScheduleLegalityKind::Legal &&
           applicability.schedules()[index].kind ==
               ScheduleApplicabilityKind::Realizable &&
           preservesBaselineCapabilities(
               index, parallelism, cacheFootprints,
               vectorization) &&
           profitabilityKind !=
               ScheduleProfitabilityKind::Regressing &&
           profitabilityKind !=
               ScheduleProfitabilityKind::Unknown &&
           (profitabilityKind ==
                ScheduleProfitabilityKind::ProvenBeneficial ||
            gainsParallelism);
}

ScheduleSelectionDecision decisionFor(
    std::size_t index, ScheduleCandidateId selected,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleParallelismResult &parallelism,
    const ScheduleProfitabilityResult &profitability,
    const CacheFootprintResult &cacheFootprints,
    const VectorizationAnalysisResult &vectorization) {
    if (index == selected)
        return ScheduleSelectionDecision::Selected;
    if (index == 0)
        return ScheduleSelectionDecision::Baseline;
    if (legality.schedules()[index].kind !=
        ScheduleLegalityKind::Legal)
        return ScheduleSelectionDecision::RejectedLegality;
    if (applicability.schedules()[index].kind !=
        ScheduleApplicabilityKind::Realizable)
        return ScheduleSelectionDecision::
            RejectedApplicability;
    if (!preservesBaselineCapabilities(
            index, parallelism, cacheFootprints,
            vectorization))
        return ScheduleSelectionDecision::
            RejectedCapability;
    bool baselineParallel =
        parallelism.schedules().front().outerParallel;
    bool gainsParallelism =
        !baselineParallel &&
        parallelism.schedules()[index].outerParallel;
    if (profitability.schedules()[index].kind !=
            ScheduleProfitabilityKind::ProvenBeneficial &&
        !gainsParallelism)
        return parallelism.schedules()[index].outerParallel
                   ? ScheduleSelectionDecision::
                         RejectedProfitability
                   : ScheduleSelectionDecision::
                         RejectedParallelism;
    if (!qualifies(index, legality, applicability,
                   parallelism, profitability,
                   cacheFootprints, vectorization))
        return ScheduleSelectionDecision::
            RejectedProfitability;
    return ScheduleSelectionDecision::LowerBenefit;
}

const char *decisionName(ScheduleSelectionDecision decision) {
    switch (decision) {
    case ScheduleSelectionDecision::Selected:
        return "selected";
    case ScheduleSelectionDecision::Baseline:
        return "baseline";
    case ScheduleSelectionDecision::RejectedLegality:
        return "rejected-legality";
    case ScheduleSelectionDecision::RejectedApplicability:
        return "rejected-applicability";
    case ScheduleSelectionDecision::RejectedCapability:
        return "rejected-capability";
    case ScheduleSelectionDecision::RejectedParallelism:
        return "rejected-parallelism";
    case ScheduleSelectionDecision::RejectedProfitability:
        return "rejected-profitability";
    case ScheduleSelectionDecision::LowerBenefit:
        return "lower-benefit";
    }
    return "unknown";
}

} // namespace

ScheduleSelectionResult selectSchedule(
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleParallelismResult &parallelism,
    const ScheduleProfitabilityResult &profitability,
    const CacheFootprintResult &cacheFootprints,
    const VectorizationAnalysisResult &vectorization) {
    ScheduleSelectionResult result;
    std::int64_t bestReduction = -1;
    for (std::size_t index = 1;
         index < schedules.candidates().size(); ++index) {
        if (!qualifies(index, legality, applicability,
                       parallelism, profitability,
                       cacheFootprints, vectorization))
            continue;
        std::int64_t reduction =
            profitability.schedules()[index]
                .totalStrideReduction;
        if (reduction > bestReduction) {
            bestReduction = reduction;
            result.selected_ =
                schedules.candidates()[index].id;
        }
    }
    for (std::size_t index = 0;
         index < schedules.candidates().size(); ++index)
        result.entries_.push_back(
            {schedules.candidates()[index].id,
             decisionFor(index, result.selected_, legality,
                         applicability, parallelism,
                         profitability, cacheFootprints,
                         vectorization)});
    return result;
}

bool verifyScheduleSelection(
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleParallelismResult &parallelism,
    const ScheduleProfitabilityResult &profitability,
    const CacheFootprintResult &cacheFootprints,
    const VectorizationAnalysisResult &vectorization,
    const ScheduleSelectionResult &selection,
    std::string &detail) {
    if (selection.selected() >=
            schedules.candidates().size() ||
        selection.entries().size() !=
            schedules.candidates().size()) {
        detail = "invalid-schedule-selection";
        return false;
    }

    ScheduleCandidateId expectedSelected = 0;
    std::int64_t bestReduction = -1;
    for (std::size_t index = 1;
         index < schedules.candidates().size(); ++index) {
        if (!qualifies(index, legality, applicability,
                       parallelism, profitability,
                       cacheFootprints, vectorization))
            continue;
        std::int64_t reduction =
            profitability.schedules()[index]
                .totalStrideReduction;
        if (reduction > bestReduction) {
            bestReduction = reduction;
            expectedSelected =
                schedules.candidates()[index].id;
        }
    }
    if (selection.selected() != expectedSelected) {
        detail = "non-optimal-schedule-selection";
        return false;
    }

    for (std::size_t index = 0;
         index < selection.entries().size(); ++index) {
        const ScheduleSelectionEntry &entry =
            selection.entries()[index];
        if (entry.schedule != index ||
            entry.decision !=
                decisionFor(index, selection.selected(),
                            legality, applicability,
                            parallelism, profitability,
                            cacheFootprints,
                            vectorization)) {
            detail = "invalid-schedule-selection-entry";
            return false;
        }
    }
    return true;
}

std::string printScheduleSelection(
    const ScheduleSelectionResult &selection) {
    std::ostringstream out;
    out << "polyhedral.schedule_selection selected=C"
        << selection.selected() << " {\n";
    for (const ScheduleSelectionEntry &entry :
         selection.entries())
        out << "  C" << entry.schedule << " = "
            << decisionName(entry.decision) << "\n";
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
