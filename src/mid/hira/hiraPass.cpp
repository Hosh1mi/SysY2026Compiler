#include "../../include/mid/hira/hiraPass.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/hira/analysis/candidateAnalysis.hpp"
#include "../../include/mid/hira/conversion/exporter.hpp"
#include "../../include/mid/hira/conversion/importer.hpp"
#include "../../include/mid/hira/ir/hiraPrinter.hpp"
#include "../../include/mid/hira/ir/hiraVerifier.hpp"
#include "../../include/mid/hira/polyhedral/dependenceAnalysis.hpp"
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
#include "../../include/mid/hira/transform/loopDistribution.hpp"
#include "../../include/mid/hira/transform/loopParallelization.hpp"
#include "../../include/mid/hira/transform/loopVectorization.hpp"
#include "../../include/mid/hira/transform/scheduleRealization.hpp"
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
                   const ArgumentAliasAnalysis &aliasAnalysis) {
    CandidateResult result = analyzeHiraCandidate(loop, loopInfo);
    if (!result.accepted()) {
        if (debug)
            dumpResult(function, loop, result);
        for (Loop *child : loop.children) {
            if (selectRegions(function, *child, loopInfo, debug,
                              forceRoundtrip, dumpHira,
                              dumpPolyhedral, aliasAnalysis))
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
        bool optimized = hoistLoopInvariants(*imported.region);
        if (!verifyRegion("transform"))
            return false;
        if (debug && optimized)
            std::cerr << "[Hira] transformed function="
                      << function.name_
                      << " header=" << blockName(loop.header)
                      << " pass=loop-invariant-code-motion\n";
        if (dumpHira && optimized) {
            std::cerr << "// hira.dump stage=transform function="
                      << function.name_
                      << " header=" << blockName(loop.header) << "\n";
            std::cerr << printHiraRegion(*imported.region,
                                        function.name_);
        }
        polyhedral::PolyhedralBuildResult polyhedralModel =
            polyhedral::buildPolyhedralModel(*imported.region,
                                             &aliasAnalysis);
        polyhedral::PolyhedralVerificationResult polyhedralVerification;
        if (polyhedralModel.succeeded())
            polyhedralVerification =
                polyhedral::verifyPolyhedralModel(
                    *polyhedralModel.model);
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
                        scheduleProfitabilityValid) {
                        scheduleSelection =
                            polyhedral::selectSchedule(
                                schedules, scheduleLegality,
                                scheduleApplicability,
                                scheduleParallelism,
                                scheduleProfitability);
                        scheduleSelectionValid =
                            polyhedral::verifyScheduleSelection(
                                schedules, scheduleLegality,
                                scheduleApplicability,
                                scheduleParallelism,
                                scheduleProfitability,
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
                    StatementPartitionKind::Distributable) {
            polyhedral::LoopDistributionResult distribution =
                polyhedral::distributeStatements(
                    *imported.region, *polyhedralModel.model,
                    statementPartitions);
            if (distribution.succeeded() &&
                distribution.changed) {
                distributedRegion = true;
                if (!verifyRegion("loop-distribution"))
                    return false;
                if (debug || dumpPolyhedral)
                    std::cerr
                        << "// polyhedral.loop_distribution"
                        << " = realized\n";
                if (dumpHira)
                    std::cerr << printHiraRegion(
                        *imported.region, function.name_);
            } else if (!distribution.succeeded() &&
                       (debug || dumpPolyhedral)) {
                std::cerr
                    << "// polyhedral.loop_distribution"
                    << " = rejected reason="
                    << polyhedral::
                           loopDistributionErrorName(
                               distribution.error);
                if (!distribution.detail.empty())
                    std::cerr
                        << " detail="
                        << distribution.detail;
                std::cerr << "\n";
            }
        }

        if (cacheFootprintsValid &&
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
                    tiledDimensions +=
                        footprint.tileSizes[index] > 1;
                }
                if (mappingValid && hasReuse &&
                    tiledDimensions >= 2) {
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
                    polyhedral::
                        VectorizationKind::Vectorizable &&
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
                    polyhedral::LoopVectorizationResult
                        vectorized =
                            polyhedral::vectorizeLoop(
                                *imported.region,
                                *transformModel,
                                *transformedDimension,
                                selectedVectorization.lanes);
                    if (!vectorized.succeeded()) {
                        if (debug || dumpPolyhedral) {
                            std::cerr
                                << "// polyhedral."
                                   "loop_vectorization"
                                << " = rejected reason="
                                << polyhedral::
                                       loopVectorizationErrorName(
                                           vectorized.error);
                            if (!vectorized.detail.empty())
                                std::cerr
                                    << " detail="
                                    << vectorized.detail;
                            std::cerr << "\n";
                        }
                    } else if (!verifyRegion(
                                   "loop-vectorization")) {
                        return false;
                    } else {
                        if (debug || dumpPolyhedral)
                            std::cerr
                                << "// polyhedral."
                                   "loop_vectorization"
                                << " dimension=d"
                                << transformedDimension->position
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
        }

        if (!distributedRegion &&
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
                        aliasAnalysis))
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
                          dumpPolyhedral, aliasAnalysis))
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
                        dumpPolyhedral, aliasAnalysis)) {
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
