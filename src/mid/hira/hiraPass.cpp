#include "../../include/mid/hira/hiraPass.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/hira/analysis/candidateAnalysis.hpp"
#include "../../include/mid/hira/conversion/exporter.hpp"
#include "../../include/mid/hira/conversion/importer.hpp"
#include "../../include/mid/hira/ir/hiraPrinter.hpp"
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
                   bool forceRoundtrip) {
    CandidateResult result = analyzeHiraCandidate(loop, loopInfo);
    if (!result.accepted()) {
        if (debug)
            dumpResult(function, loop, result);
        for (Loop *child : loop.children) {
            if (selectRegions(function, *child, loopInfo, debug,
                              forceRoundtrip))
                return true;
        }
        return false;
    }

    ImportResult imported = importHiraRegion(loop, loopInfo);
    if (imported.succeeded()) {
        if (debug) {
            std::cerr << "[Hira] region function=" << function.name_
                      << " header=" << blockName(loop.header) << "\n";
            std::cerr << printHiraRegion(*imported.region, function.name_);
        }
        bool optimized = hoistLoopInvariants(*imported.region);
        if (debug && optimized) {
            std::cerr << "[Hira] transformed function=" << function.name_
                      << " header=" << blockName(loop.header)
                      << " pass=loop-invariant-code-motion\n";
            std::cerr << printHiraRegion(*imported.region, function.name_);
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
                          forceRoundtrip))
            return true;
    }
    return false;
}

bool run(Module *module, AnalysisManager &analysisManager,
         bool forceRoundtrip) {
    const bool debug = std::getenv("DEBUG_HIRA") != nullptr;
    bool changed = false;
    for (Function *function : module->function_list_) {
        if (function->is_declaration())
            continue;
        LoopInfo &loopInfo = analysisManager.getLoopInfo(function);
        for (Loop *loop : loopInfo.topLevelLoops()) {
            if (selectRegions(*function, *loop, loopInfo, debug,
                              forceRoundtrip)) {
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
    run(module, analysisManager, forceRoundtrip_);
}

PreservedAnalyses HiraPass::execute(Module *module,
                                    AnalysisManager &analysisManager) {
    return run(module, analysisManager, forceRoundtrip_)
               ? PreservedAnalyses::none()
               : PreservedAnalyses::all();
}

} // namespace hira
