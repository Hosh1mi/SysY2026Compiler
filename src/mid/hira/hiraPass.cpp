#include "../../include/mid/hira/hiraPass.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/hira/analysis/candidateAnalysis.hpp"
#include "../../include/mid/hira/conversion/exporter.hpp"
#include "../../include/mid/hira/conversion/importer.hpp"
#include "../../include/mid/hira/ir/hiraPrinter.hpp"
#include "../../include/mid/hira/ir/hiraVerifier.hpp"
#include "../../include/mid/hira/polyhedral/dimensionSemanticAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/dependenceFeasibility.hpp"
#include "../../include/mid/hira/polyhedral/dependenceVerifier.hpp"
#include "../../include/mid/hira/polyhedral/cacheFootprintAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../include/mid/hira/polyhedral/polyhedralVerifier.hpp"
#include "../../include/mid/hira/polyhedral/reductionAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/privatizationAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/vectorizationAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/scheduleAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/scheduleApplicability.hpp"
#include "../../include/mid/hira/polyhedral/scheduleLegality.hpp"
#include "../../include/mid/hira/polyhedral/scheduleParallelism.hpp"
#include "../../include/mid/hira/polyhedral/scheduleProfitability.hpp"
#include "../../include/mid/hira/polyhedral/scheduleSelection.hpp"
#include "../../include/mid/hira/polyhedral/scheduleVerifier.hpp"
#include "../../include/mid/hira/polyhedral/statementDependenceGraph.hpp"
#include "../../include/mid/hira/polyhedral/statementPartitionAnalysis.hpp"
#include "../../include/mid/hira/transform/loopInvariantCodeMotion.hpp"
#include "../../include/mid/hira/transform/conditionalIfConversion.hpp"
#include "../../include/mid/hira/transform/affineDomainSimplification.hpp"
#include "../../include/mid/hira/transform/loopAddressRecurrence.hpp"
#include "../../include/mid/hira/transform/loopNativeUnroll.hpp"
#include "../../include/mid/hira/transform/loopRepetitionFolding.hpp"
#include "../../include/mid/hira/transform/reductionInterchangeBuffering.hpp"
#include "../../include/mid/hira/transform/statementPartitionRealization.hpp"
#include "../../include/mid/hira/transform/loopParallelization.hpp"
#include "../../include/mid/hira/transform/loopVectorization.hpp"
#include "../../include/mid/hira/transform/pointLoopExpansion.hpp"
#include "../../include/mid/hira/transform/scheduleRealization.hpp"
#include "../../include/mid/hira/transform/scheduleFusion.hpp"
#include "../../include/mid/hira/transform/loopTiling.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/module.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace hira {
namespace {

std::string blockName(const BasicBlock *block) {
    if (!block)
        return "<null>";
    return block->name_.empty() ? "<anon>" : block->name_;
}

bool hasTemporalReuse(
    const polyhedral::PolyhedralModel &model,
    polyhedral::AffineVariable dimension) {
    bool invariantAccess = false;
    bool varyingAccess = false;
    for (const polyhedral::AccessRelation &access :
         model.accesses()) {
        auto stride =
            polyhedral::analyzeLinearAccessStride(
                model, access, dimension);
        if (!stride)
            return false;
        invariantAccess |= *stride == 0;
        varyingAccess |= *stride > 0;
    }
    return invariantAccess && varyingAccess;
}

std::optional<std::size_t> findMatchingScheduleCandidate(
    const polyhedral::ScheduleCandidateSet &candidates,
    const polyhedral::ScheduleCandidate &selected) {
    for (std::size_t index = 0;
         index < candidates.candidates().size(); ++index) {
        const polyhedral::ScheduleCandidate &candidate =
            candidates.candidates()[index];
        if (candidate.kind == selected.kind &&
            candidate.originalDimensions ==
                selected.originalDimensions &&
            candidate.scheduledDimensions ==
                selected.scheduledDimensions)
            return index;
    }
    return std::nullopt;
}

std::optional<polyhedral::ScheduleVectorization>
refreshVectorizationAfterSchedule(
    const polyhedral::PolyhedralModel &transformModel,
    const polyhedral::ScheduleCandidate &selectedSchedule) {
    polyhedral::DependenceBuildResult dependences =
        polyhedral::buildDependenceRelations(transformModel);
    if (!dependences.succeeded())
        return std::nullopt;
    polyhedral::DependenceVerificationResult dependenceVerification =
        polyhedral::verifyDependenceRelations(
            transformModel, *dependences.dependences);
    if (!dependenceVerification.succeeded())
        return std::nullopt;
    polyhedral::DependenceFeasibilityResult feasibility =
        polyhedral::analyzeDependenceFeasibility(
            transformModel, *dependences.dependences);
    std::string feasibilityDetail;
    if (!polyhedral::verifyDependenceFeasibility(
            *dependences.dependences, feasibility,
            feasibilityDetail))
        return std::nullopt;
    polyhedral::ScheduleCandidateSet schedules =
        polyhedral::buildScheduleCandidates(transformModel);
    std::string scheduleDetail;
    if (!polyhedral::verifyScheduleCandidates(
            transformModel, schedules, scheduleDetail))
        return std::nullopt;
    const std::optional<std::size_t> candidateIndex =
        findMatchingScheduleCandidate(schedules, selectedSchedule);
    if (!candidateIndex)
        return std::nullopt;
    const target::A53TargetModel targetModel =
        target::cortexA53();
    polyhedral::VectorizationAnalysisResult vectorization =
        polyhedral::analyzeVectorization(
            transformModel, *dependences.dependences,
            feasibility, schedules, targetModel);
    std::string vectorizationDetail;
    if (!polyhedral::verifyVectorizationAnalysis(
            transformModel, *dependences.dependences,
            feasibility, schedules, targetModel,
            vectorization, vectorizationDetail))
        return std::nullopt;
    return vectorization.schedules()[*candidateIndex];
}

std::optional<polyhedral::AffineVariable> mapVectorizationDimension(
    const polyhedral::PolyhedralModel &fromModel,
    const polyhedral::PolyhedralModel &toModel,
    polyhedral::AffineVariable dimension) {
    const HiraValue *source = fromModel.space().source(dimension);
    return source ? toModel.space().variableFor(source)
                  : std::nullopt;
}

void dumpResult(const Function &function, const Loop &loop,
                const CandidateResult &result) {
    std::cerr << "[Hira] " << (result.accepted() ? "candidate" : "reject")
              << " function=" << function.name_
              << " header=" << blockName(loop.header);
    if (!result.accepted()) {
        std::cerr << " reason=" << candidateRejectReasonName(result.reason);
        if (!result.detail.empty())
            std::cerr << " detail=" << result.detail;
    }
    std::cerr << "\n";
}

bool selectRegions(Function &function, Loop &loop,
                   const LoopInfo &loopInfo, bool debug,
                   bool forceRoundtrip,
                   bool dumpHira, bool dumpPolyhedral,
                   const ArgumentAliasAnalysis &aliasAnalysis,
                   bool allowParallelization) {
    CandidateResult result = analyzeHiraCandidate(loop, loopInfo);
    if (!result.accepted()) {
        if (debug)
            dumpResult(function, loop, result);
        for (Loop *child : loop.children) {
            if (selectRegions(function, *child, loopInfo, debug,
                              forceRoundtrip, dumpHira,
                              dumpPolyhedral, aliasAnalysis,
                              allowParallelization))
                return true;
        }
        return false;
    }

    ImportResult imported = importHiraRegion(loop, loopInfo);
    if (imported.succeeded()) {
        auto verifyRegion = [&](const char *stage) {
            HiraVerificationResult verification =
                verifyHiraRegion(*imported.region);
            if (!verification.succeeded() && debug) {
                std::cerr << "[Hira] verify-reject function="
                          << function.name_
                          << " header=" << blockName(loop.header)
                          << " stage=" << stage
                          << " reason="
                          << hiraVerifyErrorName(verification.error);
                if (!verification.detail.empty())
                    std::cerr << " detail=" << verification.detail;
                std::cerr << "\n";
            }
            return verification.succeeded();
        };

        if (!verifyRegion("import"))
            return false;
        if (debug)
            std::cerr << "[Hira] region function=" << function.name_
                      << " header=" << blockName(loop.header) << "\n";
        if (dumpHira) {
            std::cerr << "// hira.dump stage=import function="
                      << function.name_
                      << " header=" << blockName(loop.header) << "\n";
            std::cerr << printHiraRegion(*imported.region, function.name_);
        }
        bool interiorDomainsExtracted =
            extractAffineInteriorDomains(*imported.region);
        bool conditionalsConverted =
            convertPureConditionals(*imported.region);
        bool invariantsHoisted = hoistLoopInvariants(
            *imported.region, &aliasAnalysis);
        bool repetitionsFolded =
            foldRepeatedAdditiveLoops(*imported.region);
        bool optimized =
            conditionalsConverted ||
            interiorDomainsExtracted ||
            invariantsHoisted || repetitionsFolded;
        if (!verifyRegion("transform"))
            return false;
        if (debug && optimized) {
            std::cerr << "[Hira] transformed function="
                      << function.name_
                      << " header=" << blockName(loop.header)
                      << " passes=";
            bool separator = false;
            auto printPass = [&](const char *name, bool applied) {
                if (!applied)
                    return;
                if (separator)
                    std::cerr << ",";
                std::cerr << name;
                separator = true;
            };
            printPass("loop-invariant-code-motion",
                      invariantsHoisted);
            printPass("conditional-if-conversion",
                      conditionalsConverted);
            printPass("affine-interior-domain",
                      interiorDomainsExtracted);
            printPass("loop-repetition-folding",
                      repetitionsFolded);
            std::cerr << "\n";
        }
        if (dumpHira && optimized) {
            std::cerr << "// hira.dump stage=transform function="
                      << function.name_
                      << " header=" << blockName(loop.header) << "\n";
            std::cerr << printHiraRegion(*imported.region,
                                        function.name_);
        }
        bool reductionBuffered = false;
        polyhedral::PolyhedralBuildResult polyhedralModel =
            polyhedral::buildPolyhedralModel(*imported.region,
                                             &aliasAnalysis);
        polyhedral::PolyhedralVerificationResult polyhedralVerification;
        if (polyhedralModel.succeeded())
            polyhedralVerification =
                polyhedral::verifyPolyhedralModel(
                    *polyhedralModel.model);
        if (polyhedralModel.succeeded() &&
            polyhedralVerification.succeeded()) {
            polyhedral::ReductionInterchangeBufferingResult buffering =
                polyhedral::bufferReductionInterchange(
                    *imported.region, *polyhedralModel.model);
            if (buffering.changed) {
                reductionBuffered = true;
                // Buffering changes which values are invariant in the new
                // compute inner loop.  Hoisting a GEP can expose its load only
                // on the following round, so converge before rebuilding the
                // model rather than leaving per-point invariant loads behind.
                while (hoistLoopInvariants(
                    *imported.region, &aliasAnalysis)) {
                }
                if (!verifyRegion(
                        "reduction-interchange-buffering"))
                    return false;
                polyhedralModel =
                    polyhedral::buildPolyhedralModel(
                        *imported.region, &aliasAnalysis);
                polyhedralVerification = {};
                if (polyhedralModel.succeeded())
                    polyhedralVerification =
                        polyhedral::verifyPolyhedralModel(
                            *polyhedralModel.model);
                if (!polyhedralModel.succeeded() ||
                    !polyhedralVerification.succeeded())
                    return false;
                optimized = true;
                if (debug || dumpPolyhedral)
                    std::cerr
                        << "// polyhedral."
                           "reduction_interchange_buffering"
                        << " = realized\n";
                if (dumpHira)
                    std::cerr << printHiraRegion(
                        *imported.region, function.name_);
            } else if ((debug || dumpPolyhedral) &&
                       buffering.error !=
                           polyhedral::
                               ReductionInterchangeBufferingError::
                                   NoCandidate) {
                std::cerr
                    << "// polyhedral."
                       "reduction_interchange_buffering"
                    << " = rejected reason="
                    << polyhedral::
                           reductionInterchangeBufferingErrorName(
                               buffering.error);
                if (!buffering.detail.empty())
                    std::cerr << " detail=" << buffering.detail;
                std::cerr << "\n";
            }
        }
        if (polyhedralModel.succeeded() &&
            polyhedralVerification.succeeded()) {
            std::size_t fusedBands = 0;
            while (true) {
                polyhedral::ScheduleFusionResult fusion =
                    polyhedral::
                        fuseProvablyDisjointAdjacentBands(
                            *imported.region,
                            *polyhedralModel.model);
                if (!fusion.changed)
                    break;
                fusedBands += fusion.fusedBands;
                if (!verifyRegion("schedule-fusion"))
                    return false;
                polyhedralModel =
                    polyhedral::buildPolyhedralModel(
                        *imported.region, &aliasAnalysis);
                polyhedralVerification = {};
                if (polyhedralModel.succeeded())
                    polyhedralVerification =
                        polyhedral::verifyPolyhedralModel(
                            *polyhedralModel.model);
                if (!polyhedralModel.succeeded() ||
                    !polyhedralVerification.succeeded())
                    return false;
            }
            if (fusedBands && (debug || dumpPolyhedral))
                std::cerr
                    << "// polyhedral.schedule_fusion"
                    << " bands=" << fusedBands
                    << " = realized\n";
            if (fusedBands && dumpHira)
                std::cerr << printHiraRegion(
                    *imported.region, function.name_);
        }
        polyhedral::DependenceBuildResult dependences;
        polyhedral::ReductionAnalysisResult reductions;
        std::string reductionDetail;
        bool reductionsValid = false;
        polyhedral::DependenceVerificationResult
            dependenceVerification;
        polyhedral::DependenceFeasibilityResult
            dependenceFeasibility;
        std::string dependenceFeasibilityDetail;
        bool dependenceFeasibilityValid = false;
        polyhedral::StatementDependenceGraph
            statementDependenceGraph;
        std::string statementDependenceGraphDetail;
        bool statementDependenceGraphValid = false;
        polyhedral::StatementPartitionResult
            statementPartitions;
        std::string statementPartitionDetail;
        bool statementPartitionsValid = false;
        polyhedral::ScheduleCandidateSet schedules;
        std::string scheduleDetail;
        bool schedulesValid = false;
        polyhedral::CacheFootprintResult cacheFootprints;
        std::string cacheFootprintDetail;
        bool cacheFootprintsValid = false;
        polyhedral::ScheduleApplicabilityResult
            scheduleApplicability;
        std::string scheduleApplicabilityDetail;
        bool scheduleApplicabilityValid = false;
        polyhedral::ScheduleLegalityResult scheduleLegality;
        std::string scheduleLegalityDetail;
        bool scheduleLegalityValid = false;
        polyhedral::ScheduleProfitabilityResult
            scheduleProfitability;
        std::string scheduleProfitabilityDetail;
        bool scheduleProfitabilityValid = false;
        polyhedral::ScheduleParallelismResult
            scheduleParallelism;
        std::string scheduleParallelismDetail;
        bool scheduleParallelismValid = false;
        polyhedral::PrivatizationAnalysisResult
            privatization;
        std::string privatizationDetail;
        bool privatizationValid = false;
        polyhedral::VectorizationAnalysisResult
            vectorization;
        std::string vectorizationDetail;
        bool vectorizationValid = false;
        polyhedral::ScheduleSelectionResult scheduleSelection;
        std::string scheduleSelectionDetail;
        bool scheduleSelectionValid = false;
        if (polyhedralModel.succeeded() &&
            polyhedralVerification.succeeded()) {
            reductions =
                polyhedral::analyzeReductions(
                    *polyhedralModel.model);
            reductionsValid =
                polyhedral::verifyReductionAnalysis(
                    *polyhedralModel.model, reductions,
                    reductionDetail);
            dependences = polyhedral::buildDependenceRelations(
                *polyhedralModel.model);
            if (dependences.succeeded()) {
                dependenceVerification =
                    polyhedral::verifyDependenceRelations(
                        *polyhedralModel.model,
                        *dependences.dependences);
                if (dependenceVerification.succeeded()) {
                    dependenceFeasibility =
                        polyhedral::analyzeDependenceFeasibility(
                            *polyhedralModel.model,
                            *dependences.dependences);
                    dependenceFeasibilityValid =
                        polyhedral::verifyDependenceFeasibility(
                            *dependences.dependences,
                            dependenceFeasibility,
                            dependenceFeasibilityDetail);
                    if (dependenceFeasibilityValid) {
                        statementDependenceGraph =
                            polyhedral::
                                buildStatementDependenceGraph(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility);
                        statementDependenceGraphValid =
                            polyhedral::
                                verifyStatementDependenceGraph(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    statementDependenceGraph,
                                    statementDependenceGraphDetail);
                    }
                    if (statementDependenceGraphValid) {
                        statementPartitions =
                            polyhedral::
                                analyzeStatementPartitions(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    statementDependenceGraph);
                        statementPartitionsValid =
                            polyhedral::
                                verifyStatementPartitions(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    statementDependenceGraph,
                                    statementPartitions,
                                    statementPartitionDetail);
                    }
                    if (statementPartitionsValid) {
                        schedules =
                            polyhedral::buildScheduleCandidates(
                                *polyhedralModel.model);
                        schedulesValid =
                            polyhedral::verifyScheduleCandidates(
                                *polyhedralModel.model, schedules,
                                scheduleDetail);
                    }
                    if (schedulesValid) {
                        const target::A53TargetModel targetModel =
                            target::cortexA53();
                        cacheFootprints =
                            polyhedral::analyzeCacheFootprints(
                                *polyhedralModel.model,
                                schedules, targetModel);
                        cacheFootprintsValid =
                            polyhedral::verifyCacheFootprints(
                                *polyhedralModel.model,
                                schedules, targetModel,
                                cacheFootprints,
                                cacheFootprintDetail);
                        scheduleApplicability =
                            polyhedral::
                                analyzeScheduleApplicability(
                                    *polyhedralModel.model,
                                    schedules);
                        scheduleApplicabilityValid =
                            polyhedral::
                                verifyScheduleApplicability(
                                    schedules,
                                    scheduleApplicability,
                                    scheduleApplicabilityDetail);
                        scheduleLegality =
                            polyhedral::analyzeScheduleLegality(
                                *polyhedralModel.model,
                                *dependences.dependences,
                                dependenceFeasibility, schedules);
                        scheduleLegalityValid =
                            polyhedral::verifyScheduleLegality(
                                *dependences.dependences, schedules,
                                scheduleLegality,
                                scheduleLegalityDetail);
                        scheduleProfitability =
                            polyhedral::
                                analyzeScheduleProfitability(
                                    *polyhedralModel.model,
                                    schedules);
                        scheduleProfitabilityValid =
                            polyhedral::
                                verifyScheduleProfitability(
                                    *polyhedralModel.model,
                                    schedules,
                                    scheduleProfitability,
                                    scheduleProfitabilityDetail);
                        scheduleParallelism =
                            polyhedral::
                                analyzeScheduleParallelism(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    reductions,
                                    schedules);
                        scheduleParallelismValid =
                            polyhedral::
                                verifyScheduleParallelism(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    reductions,
                                    schedules,
                                    scheduleParallelism,
                                    scheduleParallelismDetail);
                        if (scheduleParallelismValid &&
                            reductionsValid) {
                            privatization =
                                polyhedral::
                                    analyzePrivatization(
                                        *polyhedralModel.model,
                                        schedules,
                                        scheduleParallelism,
                                        reductions);
                            privatizationValid =
                                polyhedral::
                                    verifyPrivatizationAnalysis(
                                        *polyhedralModel.model,
                                        schedules,
                                        scheduleParallelism,
                                        reductions,
                                        privatization,
                                        privatizationDetail);
                        }
                        vectorization =
                            polyhedral::
                                analyzeVectorization(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    schedules, targetModel);
                        vectorizationValid =
                            polyhedral::
                                verifyVectorizationAnalysis(
                                    *polyhedralModel.model,
                                    *dependences.dependences,
                                    dependenceFeasibility,
                                    schedules, targetModel,
                                    vectorization,
                                    vectorizationDetail);
                    }
                    if (scheduleApplicabilityValid &&
                        scheduleLegalityValid &&
                        scheduleParallelismValid &&
                        scheduleProfitabilityValid &&
                        cacheFootprintsValid &&
                        vectorizationValid) {
                        scheduleSelection =
                            polyhedral::selectSchedule(
                                schedules, scheduleLegality,
                                scheduleApplicability,
                                scheduleParallelism,
                                scheduleProfitability,
                                cacheFootprints,
                                vectorization);
                        scheduleSelectionValid =
                            polyhedral::verifyScheduleSelection(
                                schedules, scheduleLegality,
                                scheduleApplicability,
                                scheduleParallelism,
                                scheduleProfitability,
                                cacheFootprints,
                                vectorization,
                                scheduleSelection,
                                scheduleSelectionDetail);
                    }
                }
            }
        }
        if (dumpPolyhedral) {
            if (polyhedralModel.succeeded() &&
                polyhedralVerification.succeeded()) {
                std::cerr
                    << "// polyhedral.dump function="
                    << function.name_
                    << " header=" << blockName(loop.header) << "\n";
                std::cerr << polyhedral::printPolyhedralModel(
                    *polyhedralModel.model);
                std::cerr << polyhedral::printDimensionSemantics(
                    *polyhedralModel.model);
                if (reductionsValid)
                    std::cerr
                        << polyhedral::printReductionAnalysis(
                               reductions);
                else {
                    std::cerr
                        << "// polyhedral.reductions rejected";
                    if (!reductionDetail.empty())
                        std::cerr
                            << " detail="
                            << reductionDetail;
                    std::cerr << "\n";
                }
                if (dependences.succeeded() &&
                    dependenceVerification.succeeded()) {
                    std::cerr
                        << polyhedral::printDependenceRelations(
                               *dependences.dependences);
                    if (dependenceFeasibilityValid) {
                        std::cerr
                            << polyhedral::
                                   printDependenceFeasibility(
                                       dependenceFeasibility);
                        if (statementDependenceGraphValid)
                            std::cerr
                                << polyhedral::
                                       printStatementDependenceGraph(
                                           statementDependenceGraph);
                        else {
                            std::cerr
                                << "// polyhedral.statement_graph "
                                   "rejected";
                            if (!statementDependenceGraphDetail
                                     .empty())
                                std::cerr
                                    << " detail="
                                    << statementDependenceGraphDetail;
                            std::cerr << "\n";
                        }
                        if (statementPartitionsValid)
                            std::cerr
                                << polyhedral::
                                       printStatementPartitions(
                                           statementPartitions);
                        else {
                            std::cerr
                                << "// polyhedral."
                                   "statement_partitions rejected";
                            if (!statementPartitionDetail.empty())
                                std::cerr
                                    << " detail="
                                    << statementPartitionDetail;
                            std::cerr << "\n";
                        }
                        if (schedulesValid) {
                            std::cerr
                                << polyhedral::
                                       printScheduleCandidates(
                                           schedules);
                            if (cacheFootprintsValid)
                                std::cerr
                                    << polyhedral::
                                           printCacheFootprints(
                                               cacheFootprints);
                            else {
                                std::cerr
                                    << "// polyhedral."
                                       "cache_footprints rejected";
                                if (!cacheFootprintDetail.empty())
                                    std::cerr
                                        << " detail="
                                        << cacheFootprintDetail;
                                std::cerr << "\n";
                            }
                            if (scheduleApplicabilityValid) {
                                std::cerr
                                    << polyhedral::
                                           printScheduleApplicability(
                                               scheduleApplicability);
                            } else {
                                std::cerr
                                    << "// polyhedral."
                                       "schedule_applicability "
                                       "rejected";
                                if (!scheduleApplicabilityDetail
                                         .empty())
                                    std::cerr
                                        << " detail="
                                        << scheduleApplicabilityDetail;
                                std::cerr << "\n";
                            }
                            if (scheduleLegalityValid) {
                                std::cerr
                                    << polyhedral::
                                           printScheduleLegality(
                                               scheduleLegality);
                            } else {
                                std::cerr
                                    << "// polyhedral."
                                       "schedule_legality rejected";
                                if (!scheduleLegalityDetail.empty())
                                    std::cerr
                                        << " detail="
                                        << scheduleLegalityDetail;
                                std::cerr << "\n";
                            }
                            if (scheduleProfitabilityValid) {
                                std::cerr
                                    << polyhedral::
                                           printScheduleProfitability(
                                               scheduleProfitability);
                            } else {
                                std::cerr
                                    << "// polyhedral."
                                       "schedule_profitability "
                                       "rejected";
                                if (!scheduleProfitabilityDetail
                                         .empty())
                                    std::cerr
                                        << " detail="
                                        << scheduleProfitabilityDetail;
                                std::cerr << "\n";
                            }
                            if (vectorizationValid)
                                std::cerr
                                    << polyhedral::
                                           printVectorizationAnalysis(
                                               vectorization);
                            else {
                                std::cerr
                                    << "// polyhedral."
                                       "vectorization rejected";
                                if (!vectorizationDetail.empty())
                                    std::cerr
                                        << " detail="
                                        << vectorizationDetail;
                                std::cerr << "\n";
                            }
                            if (scheduleParallelismValid) {
                                std::cerr
                                    << polyhedral::
                                           printScheduleParallelism(
                                           scheduleParallelism);
                                if (privatizationValid)
                                    std::cerr
                                        << polyhedral::
                                               printPrivatizationAnalysis(
                                                   privatization);
                                else {
                                    std::cerr
                                        << "// polyhedral."
                                           "privatization rejected";
                                    if (!privatizationDetail.empty())
                                        std::cerr
                                            << " detail="
                                            << privatizationDetail;
                                    std::cerr << "\n";
                                }
                            } else {
                                std::cerr
                                    << "// polyhedral."
                                       "schedule_parallelism "
                                       "rejected";
                                if (!scheduleParallelismDetail
                                         .empty())
                                    std::cerr
                                        << " detail="
                                        << scheduleParallelismDetail;
                                std::cerr << "\n";
                            }
                            if (scheduleSelectionValid) {
                                std::cerr
                                    << polyhedral::
                                           printScheduleSelection(
                                               scheduleSelection);
                            } else {
                                std::cerr
                                    << "// polyhedral."
                                       "schedule_selection rejected";
                                if (!scheduleSelectionDetail.empty())
                                    std::cerr
                                        << " detail="
                                        << scheduleSelectionDetail;
                                std::cerr << "\n";
                            }
                        } else {
                            std::cerr
                                << "// polyhedral.schedules rejected";
                            if (!scheduleDetail.empty())
                                std::cerr << " detail="
                                          << scheduleDetail;
                            std::cerr << "\n";
                        }
                    } else {
                        std::cerr
                            << "// polyhedral.feasibility rejected";
                        if (!dependenceFeasibilityDetail.empty())
                            std::cerr
                                << " detail="
                                << dependenceFeasibilityDetail;
                        std::cerr << "\n";
                    }
                } else if (dependences.succeeded()) {
                    std::cerr
                        << "// polyhedral.dependences rejected reason="
                        << polyhedral::dependenceVerifyErrorName(
                               dependenceVerification.error);
                    if (!dependenceVerification.detail.empty())
                        std::cerr << " detail="
                                  << dependenceVerification.detail;
                    std::cerr << "\n";
                } else {
                    std::cerr
                        << "// polyhedral.dependences rejected reason="
                        << polyhedral::dependenceBuildErrorName(
                               dependences.error);
                    if (!dependences.detail.empty())
                        std::cerr << " detail="
                                  << dependences.detail;
                    std::cerr << "\n";
                }
            } else if (polyhedralModel.succeeded()) {
                std::cerr
                          << "// polyhedral.dump rejected function="
                          << function.name_
                          << " header=" << blockName(loop.header)
                          << " reason="
                          << polyhedral::polyhedralVerifyErrorName(
                                 polyhedralVerification.error);
                if (!polyhedralVerification.detail.empty())
                    std::cerr << " detail="
                              << polyhedralVerification.detail;
                std::cerr << "\n";
            } else {
                std::cerr
                          << "// polyhedral.dump rejected function="
                          << function.name_
                          << " header=" << blockName(loop.header)
                          << " reason="
                          << polyhedral::polyhedralBuildErrorName(
                                 polyhedralModel.error);
                if (!polyhedralModel.detail.empty())
                    std::cerr << " detail=" << polyhedralModel.detail;
                std::cerr << "\n";
            }
        }
        polyhedral::PolyhedralBuildResult
            realizedScheduleModel;
        const polyhedral::PolyhedralModel *
            transformModel = polyhedralModel.model.get();
        bool distributedRegion = false;
        bool scheduleTransformed = false;
        std::optional<polyhedral::ScheduleVectorization>
            transformedVectorization;
        if (scheduleSelectionValid &&
            scheduleSelection.selected() != 0) {
            const polyhedral::ScheduleCandidate &selectedSchedule =
                schedules.candidates()[scheduleSelection.selected()];
            polyhedral::ScheduleRealizationResult realization =
                polyhedral::realizeSelectedSchedule(
                    *imported.region, *polyhedralModel.model,
                    schedules, scheduleSelection);
            if (!realization.succeeded()) {
                if (debug || dumpPolyhedral) {
                    std::cerr
                        << "// polyhedral.schedule_realization C"
                        << scheduleSelection.selected()
                        << " = rejected reason="
                        << polyhedral::
                               scheduleRealizationErrorName(
                                   realization.error);
                    if (!realization.detail.empty())
                        std::cerr << " detail="
                                  << realization.detail;
                    std::cerr << "\n";
                }
                return false;
            }
            if (!verifyRegion("schedule-realization"))
                return false;

            realizedScheduleModel =
                polyhedral::buildPolyhedralModel(
                    *imported.region, &aliasAnalysis);
            polyhedral::PolyhedralVerificationResult
                realizedVerification;
            if (realizedScheduleModel.succeeded())
                realizedVerification =
                    polyhedral::verifyPolyhedralModel(
                        *realizedScheduleModel.model);
            std::string realizationDetail;
            bool realizationVerified =
                realizedScheduleModel.succeeded() &&
                realizedVerification.succeeded() &&
                polyhedral::verifyScheduleRealization(
                    *polyhedralModel.model, selectedSchedule,
                    *realizedScheduleModel.model,
                    realizationDetail);
            if (!realizationVerified) {
                if (realizationDetail.empty()) {
                    if (!realizedScheduleModel.succeeded())
                        realizationDetail =
                            realizedScheduleModel.detail.empty()
                                ? polyhedral::
                                      polyhedralBuildErrorName(
                                          realizedScheduleModel.error)
                                : realizedScheduleModel.detail;
                    else
                        realizationDetail =
                            realizedVerification.detail.empty()
                                ? polyhedral::
                                      polyhedralVerifyErrorName(
                                          realizedVerification.error)
                                : realizedVerification.detail;
                }
                if (debug || dumpPolyhedral)
                    std::cerr
                        << "// polyhedral.schedule_realization C"
                        << scheduleSelection.selected()
                        << " = rejected detail="
                        << realizationDetail << "\n";
                return false;
            }
            transformModel =
                realizedScheduleModel.model.get();
            scheduleTransformed = true;
            transformedVectorization =
                refreshVectorizationAfterSchedule(
                    *transformModel, selectedSchedule);

            if (debug || dumpPolyhedral)
                std::cerr
                    << "// polyhedral.schedule_realization C"
                    << scheduleSelection.selected()
                    << " = realized\n";
            if (debug)
                std::cerr
                    << "[Hira] transformed function="
                    << function.name_
                    << " header=" << blockName(loop.header)
                    << " pass=polyhedral-schedule-realization"
                    << " schedule=C"
                    << scheduleSelection.selected() << "\n";
            if (dumpHira) {
                std::cerr
                    << "// hira.dump stage=schedule-realization"
                    << " function=" << function.name_
                    << " header=" << blockName(loop.header)
                    << "\n";
                std::cerr << printHiraRegion(
                    *imported.region, function.name_);
            }
        }

        if (statementPartitionsValid &&
            statementPartitions.kind() ==
                polyhedral::
                    StatementPartitionKind::Distributable &&
            !reductionBuffered) {
            polyhedral::StatementPartitionRealizationResult
                partitionRealization =
                    polyhedral::realizeStatementPartitions(
                    *imported.region, *polyhedralModel.model,
                    statementPartitions);
            if (partitionRealization.succeeded() &&
                partitionRealization.changed) {
                distributedRegion = true;
                if (!verifyRegion(
                        "statement-partition-realization"))
                    return false;
                if (debug || dumpPolyhedral)
                    std::cerr
                        << "// polyhedral."
                           "statement_partition_realization"
                        << " = realized\n";
                if (dumpHira)
                    std::cerr << printHiraRegion(
                        *imported.region, function.name_);
            } else if (!partitionRealization.succeeded() &&
                       (debug || dumpPolyhedral)) {
                std::cerr
                    << "// polyhedral."
                       "statement_partition_realization"
                    << " = rejected reason="
                    << polyhedral::
                           statementPartitionRealizationErrorName(
                               partitionRealization.error);
                if (!partitionRealization.detail.empty())
                    std::cerr
                        << " detail="
                        << partitionRealization.detail;
                std::cerr << "\n";
            }
        }

        if (!distributedRegion && vectorizationValid &&
            scheduleSelectionValid && transformModel) {
            const polyhedral::ScheduleVectorization
                &selectedVectorization =
                    vectorization.schedules()[
                        scheduleSelection.selected()];
            const bool selectedDimensionIsParallelOuter =
                allowParallelization &&
                scheduleParallelismValid &&
                scheduleSelection.selected() <
                    scheduleParallelism.schedules().size() &&
                scheduleParallelism
                    .schedules()[scheduleSelection.selected()]
                    .outerParallel &&
                scheduleParallelism
                    .schedules()[scheduleSelection.selected()]
                    .outerDimension ==
                    selectedVectorization.dimension;
            if (selectedVectorization.kind ==
                    polyhedral::VectorizationKind::Scalar &&
                selectedVectorization.dimension &&
                !selectedDimensionIsParallelOuter) {
                const HiraValue *source =
                    polyhedralModel.model->space().source(
                        *selectedVectorization.dimension);
                auto transformedDimension =
                    source
                        ? transformModel->space()
                              .variableFor(source)
                        : std::nullopt;
                if (transformedDimension) {
                    polyhedral::LoopAddressRecurrenceResult
                        recurrences =
                            polyhedral::
                                introduceLoopAddressRecurrences(
                                    *imported.region,
                                    *transformModel,
                                    *transformedDimension);
                    if (recurrences.changed) {
                        if (!verifyRegion(
                                "loop-address-recurrence"))
                            return false;
                        if (debug || dumpPolyhedral)
                            std::cerr
                                << "// polyhedral."
                                   "loop_address_recurrence"
                                << " count="
                                << recurrences.recurrences
                                << " = realized\n";
                        if (dumpHira)
                            std::cerr << printHiraRegion(
                                *imported.region,
                                function.name_);
                    }
                }
            }
        }

        if (!distributedRegion && transformModel) {
            LoopNativeUnrollResult unrolled =
                unrollCountedLoops(*imported.region);
            if (unrolled.changed) {
                if (!verifyRegion("loop-native-unroll"))
                    return false;
                if (debug || dumpPolyhedral)
                    std::cerr
                        << "// polyhedral.loop_native_unroll"
                        << " loops=" << unrolled.loops
                        << " = realized\n";
                if (dumpHira)
                    std::cerr << printHiraRegion(
                        *imported.region, function.name_);
            }
        }

        if (!reductionBuffered &&
            cacheFootprintsValid &&
            scheduleSelectionValid &&
            transformModel) {
            const polyhedral::CacheFootprint &footprint =
                cacheFootprints.schedules()[
                    scheduleSelection.selected()];
            if (footprint.kind ==
                    polyhedral::CacheFootprintKind::Known &&
                footprint.dimensions.size() >= 2) {
                std::vector<polyhedral::AffineVariable>
                    transformedDimensions;
                bool mappingValid = true;
                bool hasReuse = false;
                std::size_t tiledDimensions = 0;
                std::size_t reorderableTiledDimensions = 0;
                for (std::size_t index = 0;
                     index < footprint.dimensions.size();
                     ++index) {
                    const HiraValue *source =
                        polyhedralModel.model->space().source(
                            footprint.dimensions[index]);
                    auto transformedDimension =
                        source
                            ? transformModel->space()
                                  .variableFor(source)
                            : std::nullopt;
                    if (!transformedDimension) {
                        mappingValid = false;
                        break;
                    }
                    transformedDimensions.push_back(
                        *transformedDimension);
                    hasReuse |= hasTemporalReuse(
                        *transformModel,
                        *transformedDimension);
                    if (footprint.tileSizes[index] > 1) {
                        ++tiledDimensions;
                        for (const polyhedral::IterationDomain
                                 &domain :
                             transformModel->domains())
                            if (domain.dimension ==
                                    *transformedDimension &&
                                domain.loop &&
                                domain.loop
                                    ->carriedValues()
                                    .empty()) {
                                ++reorderableTiledDimensions;
                                break;
                            }
                    }
                }
                if (mappingValid && hasReuse &&
                    tiledDimensions >= 2 &&
                    reorderableTiledDimensions >= 2) {
                    polyhedral::LoopTilingResult tiling =
                        polyhedral::tileLoopBand(
                            *imported.region, *transformModel,
                            transformedDimensions,
                            footprint.tileSizes);
                    if (!tiling.succeeded()) {
                        if (debug || dumpPolyhedral) {
                            std::cerr
                                << "// polyhedral.loop_tiling"
                                << " = rejected reason="
                                << polyhedral::
                                       loopTilingErrorName(
                                           tiling.error);
                            if (!tiling.detail.empty())
                                std::cerr
                                    << " detail="
                                    << tiling.detail;
                            std::cerr << "\n";
                        }
                    } else if (!verifyRegion("loop-tiling")) {
                        return false;
                    } else if (debug || dumpPolyhedral) {
                        std::cerr
                            << "// polyhedral.loop_tiling"
                            << " dimensions="
                            << transformedDimensions.size()
                            << " footprint="
                            << footprint.l1FootprintBytes
                            << "B"
                            << " = realized\n";
                    }
                    if (tiling.succeeded() && dumpHira)
                        std::cerr << printHiraRegion(
                            *imported.region,
                            function.name_);
                } else if ((debug || dumpPolyhedral) &&
                           mappingValid && hasReuse &&
                           tiledDimensions >= 2 &&
                           reorderableTiledDimensions < 2) {
                    std::cerr
                        << "// polyhedral.loop_tiling"
                        << " = rejected reason="
                        << "insufficient-spatial-tile-rank\n";
                }
            }
        }

        bool pointExpanded = false;
        if (!distributedRegion &&
            vectorizationValid &&
            scheduleSelectionValid &&
            transformModel) {
            const polyhedral::ScheduleVectorization
                &selectedVectorization =
                    transformedVectorization &&
                            transformedVectorization->kind ==
                                polyhedral::
                                    VectorizationKind::Vectorizable
                        ? *transformedVectorization
                        : vectorization.schedules()[
                              scheduleSelection.selected()];
            if (selectedVectorization.kind ==
                    polyhedral::
                        VectorizationKind::Vectorizable &&
                selectedVectorization.dimension) {
                const polyhedral::ScheduleCandidate
                    &selectedSchedule =
                        schedules.candidates()[
                            scheduleSelection.selected()];
                const polyhedral::PolyhedralModel
                    &vectorizationModel =
                        scheduleTransformed ? *transformModel
                                            : *polyhedralModel.model;
                std::vector<polyhedral::AffineVariable> dimensions =
                    polyhedral::vectorizationCandidateDimensions(
                        vectorizationModel, selectedSchedule);
                if (!scheduleTransformed &&
                    selectedVectorization.dimension &&
                    std::find(dimensions.begin(), dimensions.end(),
                              *selectedVectorization.dimension) ==
                        dimensions.end())
                    dimensions.insert(
                        dimensions.begin(),
                        *selectedVectorization.dimension);
                else if (
                    transformedVectorization &&
                    transformedVectorization->kind ==
                        polyhedral::
                            VectorizationKind::Vectorizable &&
                    transformedVectorization->dimension) {
                    auto position = std::find(
                        dimensions.begin(), dimensions.end(),
                        *transformedVectorization->dimension);
                    if (position != dimensions.end())
                        std::rotate(
                            dimensions.begin(), position,
                            position + 1);
                    else
                        dimensions.insert(
                            dimensions.begin(),
                            *transformedVectorization->dimension);
                }

                const bool selectedDimensionIsParallelOuter =
                    allowParallelization &&
                    scheduleParallelismValid &&
                    scheduleSelection.selected() <
                        scheduleParallelism.schedules().size() &&
                    scheduleParallelism
                        .schedules()[scheduleSelection.selected()]
                        .outerParallel &&
                    scheduleParallelism
                        .schedules()[scheduleSelection.selected()]
                        .outerDimension ==
                        selectedVectorization.dimension;
                bool profitableParallelBand = false;
                if (selectedDimensionIsParallelOuter) {
                    auto parallelDimension =
                        scheduleTransformed
                            ? selectedVectorization.dimension
                            : mapVectorizationDimension(
                                  *polyhedralModel.model,
                                  *transformModel,
                                  *selectedVectorization.dimension);
                    if (parallelDimension) {
                        const HiraValue *transformedSource =
                            transformModel->space().source(
                                *parallelDimension);
                        for (const auto &node :
                             imported.region->rootSequence()
                                 .nodes()) {
                            auto *candidate =
                                dynamic_cast<HiraLoop *>(node.get());
                            if (candidate &&
                                candidate->induction() ==
                                    transformedSource) {
                                profitableParallelBand =
                                    polyhedral::
                                        isParallelBandProfitable(
                                            *candidate,
                                            target::cortexA53());
                                break;
                            }
                        }
                    }
                }
                if (selectedDimensionIsParallelOuter &&
                    profitableParallelBand)
                    dimensions.clear();

                polyhedral::LoopVectorizationResult vectorized;
                std::optional<polyhedral::AffineVariable>
                    vectorizedDimension;
                for (polyhedral::AffineVariable dimension :
                     dimensions) {
                    auto transformedDimension =
                        scheduleTransformed
                            ? std::optional<
                                  polyhedral::AffineVariable>(
                                  dimension)
                            : mapVectorizationDimension(
                                  *polyhedralModel.model,
                                  *transformModel, dimension);
                    if (!transformedDimension)
                        continue;
                    vectorized = polyhedral::vectorizeLoop(
                        *imported.region, *transformModel,
                        *transformedDimension,
                        selectedVectorization.lanes);
                    if (vectorized.succeeded()) {
                        vectorizedDimension =
                            transformedDimension;
                        break;
                    }
                    if (debug || dumpPolyhedral) {
                        std::cerr
                            << "// polyhedral."
                               "loop_vectorization"
                            << " = rejected reason="
                            << polyhedral::
                                   loopVectorizationErrorName(
                                       vectorized.error);
                        if (!vectorized.detail.empty())
                            std::cerr << " detail="
                                      << vectorized.detail;
                        std::cerr << "\n";
                    }
                }
                if (vectorizedDimension) {
                    if (!verifyRegion("loop-vectorization"))
                        return false;
                    if (debug || dumpPolyhedral)
                        std::cerr
                            << "// polyhedral."
                               "loop_vectorization"
                            << " dimension=d"
                            << vectorizedDimension->position
                            << " lanes="
                            << selectedVectorization.lanes
                            << " = realized\n";
                    if (dumpHira)
                        std::cerr << printHiraRegion(
                            *imported.region,
                            function.name_);
                }
            }
        }

        if (!distributedRegion &&
            vectorizationValid &&
            scheduleSelectionValid &&
            transformModel) {
            const polyhedral::ScheduleVectorization
                &selectedVectorization =
                    vectorization.schedules()[
                        scheduleSelection.selected()];
            if (selectedVectorization.kind ==
                    polyhedral::VectorizationKind::Scalar &&
                selectedVectorization.reason ==
                    polyhedral::VectorizationReason::
                        LoopCarriedDependence &&
                selectedVectorization.dimension) {
                const HiraValue *source =
                    polyhedralModel.model->space().source(
                        *selectedVectorization.dimension);
                auto transformedDimension =
                    source
                        ? transformModel->space()
                              .variableFor(source)
                        : std::nullopt;
                if (transformedDimension) {
                    // Preserve the exact scalar dependence order while
                    // amortizing point-loop control and address work across
                    // the four 32-bit lanes available on Cortex-A53.
                    polyhedral::PointLoopExpansionResult
                        expanded =
                            polyhedral::expandPointLoop(
                                *imported.region,
                                *transformModel,
                                *transformedDimension, 4);
                    if (!expanded.succeeded()) {
                        if (debug || dumpPolyhedral) {
                            std::cerr
                                << "// polyhedral."
                                   "point_loop_expansion"
                                << " = rejected reason="
                                << polyhedral::
                                       pointLoopExpansionErrorName(
                                           expanded.error);
                            if (!expanded.detail.empty())
                                std::cerr
                                    << " detail="
                                    << expanded.detail;
                            std::cerr << "\n";
                        }
                    } else {
                        pointExpanded = true;
                        if (debug || dumpPolyhedral)
                            std::cerr
                                << "// polyhedral."
                                   "point_loop_expansion"
                                << " factor=4"
                                << " = realized\n";
                        if (dumpHira)
                            std::cerr << printHiraRegion(
                                *imported.region,
                                function.name_);
                    }
                }
            }
        }

        if (!distributedRegion &&
            allowParallelization &&
            scheduleParallelismValid &&
            privatizationValid &&
            reductionsValid &&
            scheduleSelectionValid &&
            transformModel) {
            const target::A53TargetModel targetModel =
                target::cortexA53();
            polyhedral::LoopParallelizationResult
                parallelized =
                    polyhedral::parallelizeOuterBand(
                        *imported.region, *polyhedralModel.model,
                        schedules, scheduleSelection,
                        scheduleParallelism, privatization,
                        reductions, targetModel);
            if (!parallelized.succeeded()) {
                if ((debug || dumpPolyhedral) &&
                    parallelized.error !=
                        polyhedral::LoopParallelizationError::
                            OuterSequential) {
                    std::cerr
                        << "// polyhedral."
                           "loop_parallelization"
                        << " = rejected reason="
                        << polyhedral::
                               loopParallelizationErrorName(
                                   parallelized.error);
                    if (!parallelized.detail.empty())
                        std::cerr
                            << " detail="
                            << parallelized.detail;
                    std::cerr << "\n";
                }
            } else if (!verifyRegion(
                           "loop-parallelization")) {
                return false;
            } else {
                if (debug || dumpPolyhedral)
                    std::cerr
                        << "// polyhedral."
                           "loop_parallelization"
                        << " workers="
                        << targetModel.evaluationWorkers
                        << " = realized\n";
                if (dumpHira)
                    std::cerr << printHiraRegion(
                        *imported.region,
                        function.name_);
            }
        }

        if (!forceRoundtrip && !imported.region->modified()) {
            for (Loop *child : loop.children)
                if (selectRegions(
                        function, *child, loopInfo, debug,
                        forceRoundtrip, dumpHira, dumpPolyhedral,
                        aliasAnalysis, allowParallelization))
                    return true;
            return false;
        }

        ExportResult exported = exportHiraRegion(*imported.region);
        if (debug) {
            std::cerr << "[Hira] "
                      << (exported.changed ? "roundtrip" : "export-reject")
                      << " function=" << function.name_
                      << " header=" << blockName(loop.header);
            if (!exported.changed) {
                std::cerr << " reason="
                          << exportRejectReasonName(exported.reason);
                if (!exported.detail.empty())
                    std::cerr << " detail=" << exported.detail;
            }
            std::cerr << "\n";
        }
        return exported.changed;
    }

    if (debug) {
        std::cerr << "[Hira] conversion-reject function=" << function.name_
                  << " header=" << blockName(loop.header)
                  << " reason=" << importRejectReasonName(imported.reason);
        if (!imported.detail.empty())
            std::cerr << " detail=" << imported.detail;
        std::cerr << "\n";
    }
    for (Loop *child : loop.children) {
        if (selectRegions(function, *child, loopInfo, debug,
                          forceRoundtrip, dumpHira,
                          dumpPolyhedral, aliasAnalysis,
                          allowParallelization))
            return true;
    }
    return false;
}

bool run(Module *module, AnalysisManager &analysisManager,
         bool forceRoundtrip, bool dumpHira,
         bool dumpPolyhedral) {
    const bool debug = std::getenv("DEBUG_HIRA") != nullptr;
    dumpHira |= debug;
    dumpPolyhedral |= debug;
    ArgumentAliasAnalysis aliasAnalysis;
    aliasAnalysis.analyze(module);
    bool changed = false;
    // Worker body functions are appended to the module while regions
    // are exported; iterate over a snapshot so the export cannot
    // invalidate the traversal or revisit freshly lowered bodies.
    std::vector<Function *> functions;
    for (Function *function : module->function_list_)
        functions.push_back(function);
    for (Function *function : functions) {
        if (function->is_declaration())
            continue;
        bool functionChanged = false;
        do {
            functionChanged = false;
            LoopInfo &loopInfo =
                analysisManager.getLoopInfo(function);
            for (Loop *loop : loopInfo.topLevelLoops()) {
                if (selectRegions(
                        *function, *loop, loopInfo, debug,
                        forceRoundtrip, dumpHira,
                        dumpPolyhedral, aliasAnalysis, true)) {
                    changed = true;
                    functionChanged = true;
                    break;
                }
            }
            if (functionChanged)
                analysisManager.clear(function);
            // Forced round-trip is a conversion diagnostic, not an
            // optimization fixed-point mode.
        } while (functionChanged && !forceRoundtrip);
    }

    // Parallel bodies are deliberately revisited only after all source
    // regions have been exported.  Their semantic marker, rather than
    // their generated name, selects this phase.  Parallelization is
    // disabled here so a worker can acquire a SIMD main loop and scalar
    // tail over its exact [lo, hi) chunk without recursively dispatching.
    std::vector<Function *> workers;
    for (Function *function : module->function_list_)
        if (function->isPendingHiraParallelWorker())
            workers.push_back(function);
    if (!workers.empty())
        aliasAnalysis.analyze(module);
    for (Function *function : workers) {
        bool functionChanged = false;
        do {
            functionChanged = false;
            LoopInfo &loopInfo =
                analysisManager.getLoopInfo(function);
            for (Loop *loop : loopInfo.topLevelLoops()) {
                if (selectRegions(
                        *function, *loop, loopInfo, debug,
                        forceRoundtrip, dumpHira,
                        dumpPolyhedral, aliasAnalysis, false)) {
                    changed = true;
                    functionChanged = true;
                    break;
                }
            }
            if (functionChanged)
                analysisManager.clear(function);
        } while (functionChanged && !forceRoundtrip);
        function->markHiraParallelWorkerOptimized();
    }
    return changed;
}

} // namespace

void HiraPass::execute(Module *module) {
    AnalysisManager analysisManager;
    run(module, analysisManager, forceRoundtrip_, dumpHira_,
        dumpPolyhedral_);
}

PreservedAnalyses HiraPass::execute(Module *module,
                                    AnalysisManager &analysisManager) {
    return run(module, analysisManager, forceRoundtrip_, dumpHira_,
               dumpPolyhedral_)
               ? PreservedAnalyses::none()
               : PreservedAnalyses::all();
}

} // namespace hira
