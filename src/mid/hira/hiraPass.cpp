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
#include "../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../include/mid/hira/polyhedral/polyhedralVerifier.hpp"
#include "../../include/mid/hira/polyhedral/scheduleAnalysis.hpp"
#include "../../include/mid/hira/polyhedral/scheduleApplicability.hpp"
#include "../../include/mid/hira/polyhedral/scheduleLegality.hpp"
#include "../../include/mid/hira/polyhedral/scheduleProfitability.hpp"
#include "../../include/mid/hira/polyhedral/scheduleSelection.hpp"
#include "../../include/mid/hira/polyhedral/scheduleVerifier.hpp"
#include "../../include/mid/hira/transform/loopInvariantCodeMotion.hpp"
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
        polyhedral::DependenceVerificationResult
            dependenceVerification;
        polyhedral::DependenceFeasibilityResult
            dependenceFeasibility;
        std::string dependenceFeasibilityDetail;
        bool dependenceFeasibilityValid = false;
        polyhedral::ScheduleCandidateSet schedules;
        std::string scheduleDetail;
        bool schedulesValid = false;
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
        polyhedral::ScheduleSelectionResult scheduleSelection;
        std::string scheduleSelectionDetail;
        bool scheduleSelectionValid = false;
        if (polyhedralModel.succeeded() &&
            polyhedralVerification.succeeded()) {
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
                        schedules =
                            polyhedral::buildScheduleCandidates(
                                *polyhedralModel.model);
                        schedulesValid =
                            polyhedral::verifyScheduleCandidates(
                                *polyhedralModel.model, schedules,
                                scheduleDetail);
                    }
                    if (schedulesValid) {
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
                    }
                    if (scheduleApplicabilityValid &&
                        scheduleLegalityValid &&
                        scheduleProfitabilityValid) {
                        scheduleSelection =
                            polyhedral::selectSchedule(
                                schedules, scheduleLegality,
                                scheduleApplicability,
                                scheduleProfitability);
                        scheduleSelectionValid =
                            polyhedral::verifyScheduleSelection(
                                schedules, scheduleLegality,
                                scheduleApplicability,
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
                        if (schedulesValid) {
                            std::cerr
                                << polyhedral::
                                       printScheduleCandidates(
                                           schedules);
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
        if (!forceRoundtrip && !imported.region->modified())
            return false;

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
    for (Function *function : module->function_list_) {
        if (function->is_declaration())
            continue;
        LoopInfo &loopInfo = analysisManager.getLoopInfo(function);
        for (Loop *loop : loopInfo.topLevelLoops()) {
            if (selectRegions(*function, *loop, loopInfo, debug,
                              forceRoundtrip, dumpHira,
                              dumpPolyhedral, aliasAnalysis)) {
                changed = true;
                break;
            }
        }
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
