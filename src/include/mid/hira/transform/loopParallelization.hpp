#pragma once

#include "../polyhedral/privatizationAnalysis.hpp"
#include "../polyhedral/reductionAnalysis.hpp"
#include "../polyhedral/scheduleParallelism.hpp"
#include "../polyhedral/scheduleSelection.hpp"
#include "../target/a53TargetModel.hpp"

#include <string>

namespace hira {

class HiraRegion;

namespace polyhedral {

class PolyhedralModel;

enum class LoopParallelizationError {
    None,
    OuterSequential,
    MissingLoop,
    UnsupportedLoop,
    SequentialRecurrence,
    UnsafePrivateInitial,
    UnsupportedReduction,
    UnsupportedResult,
    NotProfitable,
    MemoryBound,
};

struct LoopParallelizationResult {
    bool changed = false;
    LoopParallelizationError error =
        LoopParallelizationError::None;
    std::string detail;

    bool succeeded() const {
        return error == LoopParallelizationError::None;
    }
};

// Marks the proven-parallel outer band of the selected schedule for
// worker lowering.  The band must be the region's outermost loop, carry
// only exact reductions (each becomes a worker-private partial combined
// in band order after the join) and contain enough static work to
// amortize a dual-core dispatch on the target.
LoopParallelizationResult parallelizeOuterBand(
    HiraRegion &region, const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleSelectionResult &selection,
    const ScheduleParallelismResult &parallelism,
    const PrivatizationAnalysisResult &privatization,
    const ReductionAnalysisResult &reductions,
    const target::A53TargetModel &target);
const char *loopParallelizationErrorName(
    LoopParallelizationError error);

} // namespace polyhedral
} // namespace hira
