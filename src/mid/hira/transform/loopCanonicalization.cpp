#include "../../../include/mid/hira/transform/loopCanonicalization.hpp"

#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/transform/loopCanonicalization.hpp"

namespace hira {
namespace {

bool canonicalizeModuleLoops(Module *module) {
    bool changed = false;
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            changed |= canonicalizeLoopForm(function);
    return changed;
}

} // namespace

void LoopCanonicalizationPass::execute(Module *module) {
    canonicalizeModuleLoops(module);
}

PreservedAnalyses LoopCanonicalizationPass::execute(
    Module *module, AnalysisManager &analysisManager) {
    (void)analysisManager;
    return canonicalizeModuleLoops(module)
               ? PreservedAnalyses::none()
               : PreservedAnalyses::all();
}

} // namespace hira
