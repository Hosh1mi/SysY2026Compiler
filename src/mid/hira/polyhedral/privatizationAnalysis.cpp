#include "../../../include/mid/hira/polyhedral/privatizationAnalysis.hpp"

#include <algorithm>
#include <sstream>

namespace hira::polyhedral {
namespace {

bool contains(
    const std::vector<AffineVariable> &dimensions,
    AffineVariable dimension) {
    return std::find(dimensions.begin(), dimensions.end(),
                     dimension) != dimensions.end();
}

bool isExactReduction(
    ScalarRecurrenceId recurrence,
    const ReductionAnalysisResult &reductions) {
    for (const ScalarReduction &reduction :
         reductions.scalarReductions())
        if (reduction.recurrence == recurrence)
            return reduction.parallelSemantics ==
                   ReductionParallelSemantics::Exact;
    return false;
}

bool same(const PrivatizationAnalysisResult &left,
          const PrivatizationAnalysisResult &right) {
    if (left.schedules().size() !=
        right.schedules().size())
        return false;
    for (std::size_t index = 0;
         index < left.schedules().size(); ++index) {
        const SchedulePrivatization &a =
            left.schedules()[index];
        const SchedulePrivatization &b =
            right.schedules()[index];
        if (a.schedule != b.schedule ||
            a.recurrences.size() != b.recurrences.size())
            return false;
        for (std::size_t recurrence = 0;
             recurrence < a.recurrences.size();
             ++recurrence)
            if (a.recurrences[recurrence].recurrence !=
                    b.recurrences[recurrence].recurrence ||
                a.recurrences[recurrence].kind !=
                    b.recurrences[recurrence].kind)
                return false;
    }
    return true;
}

const char *kindName(PrivatizationKind kind) {
    switch (kind) {
    case PrivatizationKind::TaskPrivateScalar:
        return "task-private-scalar";
    case PrivatizationKind::WorkerReduction:
        return "worker-reduction";
    case PrivatizationKind::Sequential:
        return "sequential";
    }
    return "unknown";
}

} // namespace

PrivatizationAnalysisResult analyzePrivatization(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &parallelism,
    const ReductionAnalysisResult &reductions) {
    PrivatizationAnalysisResult result;
    for (std::size_t scheduleIndex = 0;
         scheduleIndex < schedules.candidates().size();
         ++scheduleIndex) {
        SchedulePrivatization schedule;
        schedule.schedule =
            schedules.candidates()[scheduleIndex].id;
        const ScheduleParallelism &parallel =
            parallelism.schedules()[scheduleIndex];
        for (const ScalarRecurrence &recurrence :
             model.scalarRecurrences()) {
            RecurrencePrivatization entry;
            entry.recurrence = recurrence.id;
            if (parallel.outerParallel &&
                parallel.outerDimension &&
                contains(recurrence.dimensions,
                         *parallel.outerDimension)) {
                if (!(recurrence.dimension ==
                      *parallel.outerDimension))
                    entry.kind =
                        PrivatizationKind::
                            TaskPrivateScalar;
                else if (isExactReduction(
                             recurrence.id, reductions))
                    entry.kind =
                        PrivatizationKind::
                            WorkerReduction;
            }
            schedule.recurrences.push_back(entry);
        }
        result.schedules_.push_back(
            std::move(schedule));
    }
    return result;
}

bool verifyPrivatizationAnalysis(
    const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleParallelismResult &parallelism,
    const ReductionAnalysisResult &reductions,
    const PrivatizationAnalysisResult &result,
    std::string &detail) {
    PrivatizationAnalysisResult expected =
        analyzePrivatization(
            model, schedules, parallelism, reductions);
    if (!same(result, expected)) {
        detail = "invalid-privatization-analysis";
        return false;
    }
    return true;
}

std::string printPrivatizationAnalysis(
    const PrivatizationAnalysisResult &result) {
    std::ostringstream out;
    out << "polyhedral.privatization {\n";
    for (const SchedulePrivatization &schedule :
         result.schedules()) {
        out << "  C" << schedule.schedule << " [";
        for (std::size_t index = 0;
             index < schedule.recurrences.size(); ++index) {
            if (index)
                out << ", ";
            out << "R"
                << schedule.recurrences[index].recurrence
                << ":"
                << kindName(
                       schedule.recurrences[index].kind);
        }
        out << "]\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
