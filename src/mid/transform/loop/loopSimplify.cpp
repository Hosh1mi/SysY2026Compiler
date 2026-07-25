#include "../../../include/mid/opt/loopSimplify.hpp"

#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/transform/loopCanonicalization.hpp"

void LoopSimplify::execute(Module *module) {
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            canonicalizeLoopForm(function);
}

PreservedAnalyses LoopSimplify::execute(
    Module *module, AnalysisManager &analysisManager) {
    (void)analysisManager;
    bool changed = false;
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            changed |= canonicalizeLoopForm(function);
    return changed ? PreservedAnalyses::none()
                   : PreservedAnalyses::all();
}
