/**
 * @file loopSimplify.cpp
 * @brief 循环 CFG 规范化：提供 LoopSimplify Pass 入口，对模块内函数调用通用循环 CFG 规范化。
 * @details Pass 本身只负责遍历函数和传播 preserved analyses，具体 CFG 修改由 canonicalizeLoopForm 完成。
 */

#include "../../../include/mid/opt/loopSimplify.hpp"

#include "../../../include/mid/ir/function.hpp"
#include "../../../include/mid/ir/module.hpp"
#include "../../../include/mid/transform/loopCanonicalization.hpp"

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopSimplify::execute(Module *module) {
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            canonicalizeLoopForm(function);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param analysisManager 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
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
