#include "../../../include/mid/hira/transform/loopParallelization.hpp"

#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace hira::polyhedral {
namespace {

LoopParallelizationResult reject(
    LoopParallelizationError error, std::string detail) {
    LoopParallelizationResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

const ScalarRecurrence *findRecurrence(
    const PolyhedralModel &model, ScalarRecurrenceId id) {
    for (const ScalarRecurrence &recurrence :
         model.scalarRecurrences())
        if (recurrence.id == id)
            return &recurrence;
    return nullptr;
}

const ScalarReduction *findReduction(
    const ReductionAnalysisResult &reductions,
    ScalarRecurrenceId id) {
    for (const ScalarReduction &reduction :
         reductions.scalarReductions())
        if (reduction.recurrence == id)
            return &reduction;
    return nullptr;
}

bool carriesOuterDimension(
    const PolyhedralModel &model, ScalarValueSource source,
    AffineVariable outerDimension) {
    if (source.kind != ScalarSourceKind::RecurrenceIteration &&
        source.kind != ScalarSourceKind::RecurrenceResult)
        return false;
    const ScalarRecurrence *parent =
        findRecurrence(model, source.source);
    return parent && parent->dimension == outerDimension;
}

// The two evaluation cores share a single 1 MB L2.  A band whose body only
// streams memory (loads/stores plus light index or guard arithmetic) is
// bounded by the shared L2 write/read bandwidth, so a second worker cannot
// double throughput and the dispatch overhead only regresses it.  Such
// bands are left sequential.  A band qualifies when it contains a
// vectorized point loop (SIMD packs compute into each memory op) or a
// multi-cycle compute op (multiply/divide) that makes it core-bound.
bool isHeavyCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Mul:
    case ComputeKind::FMul:
    case ComputeKind::FDiv:
        return true;
    default:
        return false;
    }
}

void scanComputeIntensity(const HiraSequence &sequence,
                          bool &sawVectorized,
                          bool &sawHeavyCompute) {
    for (const auto &node : sequence.nodes()) {
        if (auto *loop = dynamic_cast<const HiraLoop *>(
                node.get())) {
            if (loop->role() == HiraLoop::Role::VectorMain ||
                loop->role() == HiraLoop::Role::ScalarRemainder)
                sawVectorized = true;
            scanComputeIntensity(loop->body(), sawVectorized,
                                 sawHeavyCompute);
            continue;
        }
        if (auto *condition =
                dynamic_cast<const HiraIf *>(node.get())) {
            scanComputeIntensity(condition->thenSequence(),
                                 sawVectorized, sawHeavyCompute);
            scanComputeIntensity(condition->elseSequence(),
                                 sawVectorized, sawHeavyCompute);
            continue;
        }
        auto *compute =
            dynamic_cast<const HiraComputeOp *>(node.get());
        if (compute && isHeavyCompute(compute->computeKind()))
            sawHeavyCompute = true;
    }
}

bool bandIsComputeBound(const HiraLoop &loop) {
    bool sawVectorized = false;
    bool sawHeavyCompute = false;
    scanComputeIntensity(loop.body(), sawVectorized,
                         sawHeavyCompute);
    return sawVectorized || sawHeavyCompute;
}

std::optional<ParallelReductionOp> parallelOp(
    ReductionOperator operation) {
    switch (operation) {
    case ReductionOperator::BitAnd:
        return ParallelReductionOp::BitAnd;
    case ReductionOperator::BitOr:
        return ParallelReductionOp::BitOr;
    case ReductionOperator::BitXor:
        return ParallelReductionOp::BitXor;
    default:
        return std::nullopt;
    }
}

// Static statement count executed by a sequence.  Empty optional means
// the count depends on a runtime bound, which the cost model treats as
// large enough for worker lowering.
std::optional<std::uint64_t> sequenceWork(
    const HiraSequence &sequence, std::uint64_t cap);

std::optional<std::uint64_t> nodeWork(
    const HiraNode &node, std::uint64_t cap) {
    if (auto *loop = dynamic_cast<const HiraLoop *>(&node)) {
        const HiraValue *lower = loop->lowerBound();
        const HiraValue *upper = loop->upperBound();
        const HiraValue *step = loop->step();
        if (!lower || !upper || !step ||
            lower->kind() != ValueKind::IntegerConstant ||
            upper->kind() != ValueKind::IntegerConstant ||
            step->kind() != ValueKind::IntegerConstant ||
            step->integerValue() <= 0)
            return std::nullopt;
        std::int64_t span =
            upper->integerValue() - lower->integerValue();
        std::uint64_t trip =
            span <= 0 ? 0
                      : static_cast<std::uint64_t>(
                            (span + step->integerValue() - 1) /
                            step->integerValue());
        auto body = sequenceWork(loop->body(), cap);
        if (!body)
            return std::nullopt;
        if (trip != 0 && *body > cap / trip)
            return cap;
        return std::min(cap, trip * *body);
    }
    if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
        auto thenWork = sequenceWork(condition->thenSequence(), cap);
        auto elseWork = sequenceWork(condition->elseSequence(), cap);
        if (!thenWork || !elseWork)
            return std::nullopt;
        std::uint64_t total = *thenWork + *elseWork + 1;
        return std::min(cap, total);
    }
    if (dynamic_cast<const HiraYield *>(&node))
        return std::uint64_t{0};
    return std::uint64_t{1};
}

std::optional<std::uint64_t> sequenceWork(
    const HiraSequence &sequence, std::uint64_t cap) {
    std::uint64_t total = 0;
    for (const auto &node : sequence.nodes()) {
        auto work = nodeWork(*node, cap);
        if (!work)
            return std::nullopt;
        total = std::min(cap, total + *work);
    }
    return total;
}

int nextBodyId(const Module *module) {
    const std::string prefix = "__sysy_par_body_";
    int next = 0;
    if (!module)
        return next;
    for (const Function *function : module->function_list_) {
        if (function->name_.rfind(prefix, 0) != 0)
            continue;
        const std::string suffix =
            function->name_.substr(prefix.size());
        if (suffix.empty() ||
            !std::all_of(suffix.begin(), suffix.end(),
                         [](char ch) {
                             return ch >= '0' && ch <= '9';
                         }))
            continue;
        next = std::max(next, std::stoi(suffix) + 1);
    }
    return next;
}

} // namespace

LoopParallelizationResult parallelizeOuterBand(
    HiraRegion &region, const PolyhedralModel &model,
    const ScheduleCandidateSet &schedules,
    const ScheduleSelectionResult &selection,
    const ScheduleParallelismResult &parallelism,
    const PrivatizationAnalysisResult &privatization,
    const ReductionAnalysisResult &reductions,
    const target::A53TargetModel &target) {
    if (region.parallelPlan())
        return reject(LoopParallelizationError::MissingLoop,
                      "parallel-plan-already-set");
    ScheduleCandidateId selected = selection.selected();
    if (selected >= parallelism.schedules().size() ||
        selected >= privatization.schedules().size() ||
        parallelism.schedules()[selected].schedule != selected ||
        privatization.schedules()[selected].schedule != selected)
        return reject(LoopParallelizationError::OuterSequential,
                      "missing-selected-schedule");

    const ScheduleParallelism &parallel =
        parallelism.schedules()[selected];
    if (!parallel.outerParallel || !parallel.outerDimension)
        return reject(LoopParallelizationError::OuterSequential,
                      "outer-band-sequential");

    const HiraValue *outerInduction =
        model.space().source(*parallel.outerDimension);
    if (!outerInduction)
        return reject(LoopParallelizationError::MissingLoop,
                      "missing-outer-dimension-source");

    HiraLoop *band = nullptr;
    for (const auto &node : region.rootSequence().nodes()) {
        auto *loop = dynamic_cast<HiraLoop *>(node.get());
        if (loop && loop->induction() == outerInduction) {
            band = loop;
            break;
        }
    }
    if (!band)
        return reject(LoopParallelizationError::MissingLoop,
                      "outer-band-not-root-sequence");
    if (band->role() != HiraLoop::Role::Ordinary)
        return reject(LoopParallelizationError::UnsupportedLoop,
                      "outer-band-already-transformed");

    // The runtime hands each worker a [lo, hi) chunk of the band, so the
    // band must advance one iteration per step to keep chunk boundaries
    // on the iteration grid.
    auto *indexType = dynamic_cast<IntegerType *>(
        band->induction()->type());
    if (!indexType || indexType->num_bits_ != 32 ||
        !band->step() ||
        band->step()->kind() != ValueKind::IntegerConstant ||
        band->step()->integerValue() != 1)
        return reject(LoopParallelizationError::UnsupportedLoop,
                      "non-canonical-parallel-band");

    // The body needs the original lower bound to place its reduction
    // partial; bounds defined inside the region are unavailable there.
    const HiraValue *lower = band->lowerBound();
    if (!lower ||
        (lower->kind() != ValueKind::Parameter &&
         lower->kind() != ValueKind::IntegerConstant))
        return reject(LoopParallelizationError::UnsupportedLoop,
                      "unsupported-band-lower-bound");

    HiraParallelPlan plan;
    plan.loop = band;
    const SchedulePrivatization &privacy =
        privatization.schedules()[selected];
    for (const RecurrencePrivatization &entry :
         privacy.recurrences) {
        const ScalarRecurrence *recurrence =
            findRecurrence(model, entry.recurrence);
        if (!recurrence)
            return reject(
                LoopParallelizationError::SequentialRecurrence,
                "missing-recurrence");
        if (entry.kind == PrivatizationKind::Sequential)
            return reject(
                LoopParallelizationError::SequentialRecurrence,
                "outer-band-carries-sequential-recurrence");
        if (entry.kind == PrivatizationKind::TaskPrivateScalar) {
            // A private scalar may only read values that stay local to
            // one band iteration; reading the running value of an outer
            // recurrence would observe a worker partial.
            if (carriesOuterDimension(
                    model, recurrence->initialSource,
                    *parallel.outerDimension))
                return reject(
                    LoopParallelizationError::
                        UnsafePrivateInitial,
                    "private-scalar-reads-outer-recurrence");
            continue;
        }

        // Worker reduction: the band carries an exact reduction.  Every
        // in-body use of the running value must belong to the reduction
        // update itself, otherwise privatizing the accumulator would
        // change observable values.
        const ScalarReduction *reduction =
            findReduction(reductions, entry.recurrence);
        auto operation =
            reduction
                ? parallelOp(reduction->operation)
                : std::nullopt;
        if (!reduction ||
            reduction->parallelSemantics !=
                ReductionParallelSemantics::Exact ||
            !operation || !reduction->integerIdentity)
            return reject(
                LoopParallelizationError::UnsupportedReduction,
                "non-exact-worker-reduction");
        const HiraNode *update =
            recurrence->yielded
                ? recurrence->yielded->definingNode()
                : nullptr;
        if (!update)
            return reject(
                LoopParallelizationError::UnsupportedReduction,
                "missing-reduction-update");
        for (const RecurrenceUseRelation &use :
             model.recurrenceUses()) {
            if (use.recurrence != entry.recurrence ||
                use.kind != RecurrenceValueKind::Iteration)
                continue;
            const PolyhedralStatement *consumer = nullptr;
            for (const PolyhedralStatement &statement :
                 model.statements())
                if (statement.id == use.consumer) {
                    consumer = &statement;
                    break;
                }
            if (!consumer || consumer->node != update)
                return reject(
                    LoopParallelizationError::
                        UnsupportedReduction,
                    "reduction-value-escapes-update");
        }

        std::size_t carried = 0;
        bool matched = false;
        for (std::size_t index = 0;
             index < band->carriedValues().size(); ++index) {
            if (band->carriedValues()[index].iteration ==
                recurrence->iteration) {
                carried = index;
                matched = true;
                break;
            }
        }
        if (!matched)
            return reject(
                LoopParallelizationError::UnsupportedReduction,
                "reduction-binding-not-on-band");
        const HiraValue *initial =
            band->carriedValues()[carried].initial;
        if (!initial ||
            (initial->kind() != ValueKind::Parameter &&
             initial->kind() != ValueKind::IntegerConstant))
            return reject(
                LoopParallelizationError::UnsupportedReduction,
                "unsupported-reduction-initial");
        plan.reductions.push_back(
            {carried, *operation,
             *reduction->integerIdentity});
    }

    // The band may carry exact reductions only; any other carried value
    // is a cross-iteration dependence the chunk split cannot honor.
    for (std::size_t index = 0;
         index < band->carriedValues().size(); ++index) {
        bool covered = false;
        for (const HiraParallelReduction &reduction :
             plan.reductions)
            covered |= reduction.carriedIndex == index;
        if (!covered)
            return reject(
                LoopParallelizationError::SequentialRecurrence,
                "unmodeled-carried-binding");
    }

    // Live-outs must be reduction results: worker partials are combined
    // after the join, everything else stays inside the body.
    for (const HiraValue *result : region.results()) {
        bool covered = false;
        for (const HiraParallelReduction &reduction :
             plan.reductions)
            covered |= band->carriedValues()[
                               reduction.carriedIndex]
                               .result == result;
        if (!covered)
            return reject(
                LoopParallelizationError::UnsupportedResult,
                "non-reduction-live-out");
    }

const std::uint64_t threshold =
        static_cast<std::uint64_t>(
            target.parallelDispatchStatements) *
        target.minimumParallelOverheadRatio;
    auto work = nodeWork(*band, threshold);
    if (work && *work < threshold)
        return reject(LoopParallelizationError::NotProfitable,
                      "static-work-below-dispatch-overhead");

    if (!bandIsComputeBound(*band))
        return reject(LoopParallelizationError::MemoryBound,
                      "band-memory-bound-no-l2-headroom");

    Loop *sourceLoop = region.sourceLoop();
    Module *module =
        sourceLoop && sourceLoop->header &&
                sourceLoop->header->parent_
            ? sourceLoop->header->parent_->parent_
            : nullptr;
    if (!module)
        return reject(LoopParallelizationError::MissingLoop,
                      "missing-source-module");
    plan.bodyId = nextBodyId(module);

    band->setRole(HiraLoop::Role::Parallel);
    region.setParallelPlan(std::move(plan));
    region.markModified();

    LoopParallelizationResult result;
    result.changed = true;
    return result;
}

const char *loopParallelizationErrorName(
    LoopParallelizationError error) {
    switch (error) {
    case LoopParallelizationError::None:
        return "none";
    case LoopParallelizationError::OuterSequential:
        return "outer-sequential";
    case LoopParallelizationError::MissingLoop:
        return "missing-loop";
    case LoopParallelizationError::UnsupportedLoop:
        return "unsupported-loop";
    case LoopParallelizationError::SequentialRecurrence:
        return "sequential-recurrence";
    case LoopParallelizationError::UnsafePrivateInitial:
        return "unsafe-private-initial";
    case LoopParallelizationError::UnsupportedReduction:
        return "unsupported-reduction";
    case LoopParallelizationError::UnsupportedResult:
        return "unsupported-result";
    case LoopParallelizationError::NotProfitable:
        return "not-profitable";
    case LoopParallelizationError::MemoryBound:
        return "memory-bound";
    }
    return "unknown";
}

} // namespace hira::polyhedral
