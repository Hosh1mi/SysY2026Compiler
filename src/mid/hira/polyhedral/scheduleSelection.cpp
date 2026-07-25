#include "../../../include/mid/hira/polyhedral/scheduleSelection.hpp"

#include <sstream>

namespace hira::polyhedral {
namespace {

bool qualifies(
    std::size_t index,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleProfitabilityResult &profitability) {
    return legality.schedules()[index].kind ==
               ScheduleLegalityKind::Legal &&
           applicability.schedules()[index].kind ==
               ScheduleApplicabilityKind::Realizable &&
           profitability.schedules()[index].kind ==
               ScheduleProfitabilityKind::ProvenBeneficial;
}

ScheduleSelectionDecision decisionFor(
    std::size_t index, ScheduleCandidateId selected,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleProfitabilityResult &profitability) {
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
    if (profitability.schedules()[index].kind !=
        ScheduleProfitabilityKind::ProvenBeneficial)
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
    const ScheduleProfitabilityResult &profitability) {
    ScheduleSelectionResult result;
    std::int64_t bestReduction = 0;
    for (std::size_t index = 1;
         index < schedules.candidates().size(); ++index) {
        if (!qualifies(index, legality, applicability,
                       profitability))
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
                         applicability, profitability)});
    return result;
}

bool verifyScheduleSelection(
    const ScheduleCandidateSet &schedules,
    const ScheduleLegalityResult &legality,
    const ScheduleApplicabilityResult &applicability,
    const ScheduleProfitabilityResult &profitability,
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
    std::int64_t bestReduction = 0;
    for (std::size_t index = 1;
         index < schedules.candidates().size(); ++index) {
        if (!qualifies(index, legality, applicability,
                       profitability))
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
                            profitability)) {
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
