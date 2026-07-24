#include "../../include/mid/hira/hiraPass.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/hira/analysis/candidateAnalysis.hpp"
#include "../../include/mid/hira/conversion/importer.hpp"
#include "../../include/mid/hira/ir/hiraPrinter.hpp"
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

void selectRegions(const Function &function, Loop &loop,
                   const LoopInfo &loopInfo, bool debug) {
    CandidateResult result = analyzeHiraCandidate(loop, loopInfo);
    if (!result.accepted()) {
        if (debug)
            dumpResult(function, loop, result);
        for (Loop *child : loop.children)
            selectRegions(function, *child, loopInfo, debug);
        return;
    }

    ImportResult imported = importHiraRegion(loop, loopInfo);
    if (imported.succeeded()) {
        if (debug) {
            std::cerr << "[Hira] region function=" << function.name_
                      << " header=" << blockName(loop.header) << "\n";
            std::cerr << printHiraRegion(*imported.region, function.name_);
        }
        return;
    }

    if (debug) {
        std::cerr << "[Hira] conversion-reject function=" << function.name_
                  << " header=" << blockName(loop.header)
                  << " reason=" << importRejectReasonName(imported.reason);
        if (!imported.detail.empty())
            std::cerr << " detail=" << imported.detail;
        std::cerr << "\n";
    }
    for (Loop *child : loop.children)
        selectRegions(function, *child, loopInfo, debug);
}

void run(Module *module, AnalysisManager &analysisManager) {
    const bool debug = std::getenv("DEBUG_HIRA") != nullptr;
    for (Function *function : module->function_list_) {
        if (function->is_declaration())
            continue;
        LoopInfo &loopInfo = analysisManager.getLoopInfo(function);
        for (Loop *loop : loopInfo.topLevelLoops())
            selectRegions(*function, *loop, loopInfo, debug);
    }
}

} // namespace

void HiraPass::execute(Module *module) {
    AnalysisManager analysisManager;
    run(module, analysisManager);
}

PreservedAnalyses HiraPass::execute(Module *module,
                                    AnalysisManager &analysisManager) {
    run(module, analysisManager);
    return PreservedAnalyses::all();
}

} // namespace hira
