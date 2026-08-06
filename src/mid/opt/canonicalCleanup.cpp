#include "../../include/mid/opt/canonicalCleanup.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/opt/deadCodeEliminate.hpp"
#include "../../include/mid/opt/deadStoreEliminate.hpp"
#include "../../include/mid/opt/linearBlockMerge.hpp"
#include "../../include/mid/opt/sccp.hpp"
#include "../../include/mid/opt/instCombine.hpp"

#include <memory>

namespace {

constexpr unsigned kMaxCleanupIterations = 4;

bool runCleanupIteration(Module *module, AnalysisManager &AM) {
    std::unique_ptr<Pass> passes[] = {
        std::make_unique<DeadCodeEliminate>(),
        std::make_unique<LinearBlockMerge>(),
        std::make_unique<SCCP>(),
        std::make_unique<InstCombine>(),
        std::make_unique<DeadStoreEliminate>(),
        std::make_unique<DeadCodeEliminate>(),
        std::make_unique<LinearBlockMerge>(),
    };

    bool changed = false;
    for (auto &pass : passes) {
        PreservedAnalyses preserved = pass->execute(module, AM);
        changed |= !preserved.preservesAll();
        AM.invalidate(module, preserved);
    }
    return changed;
}

} // namespace

void CanonicalCleanup::execute(Module *module) {
    AnalysisManager AM;
    for (unsigned iteration = 0; iteration < kMaxCleanupIterations;
         ++iteration) {
        if (!runCleanupIteration(module, AM))
            break;
    }
}

PreservedAnalyses CanonicalCleanup::execute(Module *module,
                                            AnalysisManager &AM) {
    bool changed = false;
    for (unsigned iteration = 0; iteration < kMaxCleanupIterations;
         ++iteration) {
        bool iterationChanged = runCleanupIteration(module, AM);
        changed |= iterationChanged;
        if (!iterationChanged)
            break;
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
