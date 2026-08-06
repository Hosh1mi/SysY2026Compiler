#include "../../include/mid/opt/canonicalCleanup.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/opt/deadCodeEliminate.hpp"
#include "../../include/mid/opt/deadStoreEliminate.hpp"
#include "../../include/mid/opt/linearBlockMerge.hpp"
#include "../../include/mid/opt/sccp.hpp"
#include "../../include/mid/opt/instCombine.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

bool dumpNestedPass(const std::string &name) {
    const char *filter = std::getenv("DUMP_IR_PASS");
    if (!filter || !*filter)
        return false;
    if (std::string(filter) == "*")
        return true;
    std::stringstream ss(filter);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item == name)
            return true;
    }
    return false;
}

void dumpNestedSnapshot(Module *module, const std::string &name,
                        const char *when) {
    if (!dumpNestedPass(name))
        return;
    std::cerr << "; === IR " << when << " " << name << " ===\n"
              << module->print() << "\n";
}

} // namespace

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
        const std::string passName = pass->name();
        if (std::getenv("TRACE_PASS_PIPELINE"))
            std::cerr << "[PipelinePass] " << passName << "\n";
        dumpNestedSnapshot(module, passName, "Before");
        PreservedAnalyses preserved = pass->execute(module, AM);
        dumpNestedSnapshot(module, passName, "After");
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
