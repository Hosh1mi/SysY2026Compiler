/**
 * @file lastIterationElimination.cpp
 * @brief 最后一次迭代消除：证明早期迭代写入不可观察后，仅保留会向循环外泄露结果的最后一次迭代。
 * @details 必须证明早期迭代无可观察调用、内存副作用或逃逸值，才把循环重写为最后一次有效迭代。
 */

// LastIterationElimination retains only the dynamic loop iteration whose
// overwrite value can escape, after proving earlier iterations unobservable.

#include "../../../include/mid/opt/lastIterationElimination.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

/**
 * @brief 从 CallInst 的最后一个操作数中取得被调函数。
 * @param call 待解析的调用指令。
 * @return 操作数合法时返回被调函数，否则返回 nullptr。
 */
Function *calledFunction(CallInst *call) {
    if (!call || call->num_ops() == 0)
        return nullptr;
    return dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
}

/**
 * @brief 判断 isI32 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isI32(Value *value) {
    auto *integer = value
                        ? dynamic_cast<IntegerType *>(value->type_)
                        : nullptr;
    return integer && integer->num_bits_ == 32;
}

/**
 * @brief 实现 incomingIndex 对应的局部分析或变换辅助逻辑。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param source 参数 `source`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
int incomingIndex(PhiInst *phi, BasicBlock *source) {
    if (!phi || !source)
        return -1;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == source)
            return static_cast<int>(i);
    }
    return -1;
}

/**
 * @brief 判断 isInsideUse 所描述的结构、合法性或安全条件是否成立。
 * @param use 参数 `use`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isInsideUse(const Use &use, const Loop &loop) {
    auto *user = use.user_;
    return user && user->parent_ && loop.isInLoop(user->parent_);
}

/**
 * @brief 判断 hasEscapingUse 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasEscapingUse(Value *value, const Loop &loop) {
    for (const Use &use : value->use_list_) {
        auto *user = use.user_;
        if (user && user->parent_ && !loop.isInLoop(user->parent_))
            return true;
    }
    return false;
}

/**
 * @brief 判断 isAlreadyClamped 所描述的结构、合法性或安全条件是否成立。
 * @param start 参数 `start`，用于本函数的分析、匹配或 IR 构造。
 * @param lastValue 参数 `lastValue`，用于本函数的分析、匹配或 IR 构造。
 * @param boundValue 参数 `boundValue`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isAlreadyClamped(Value *start, int lastValue, int boundValue) {
    auto *select = dynamic_cast<SelectInst *>(start);
    auto *last = select
                     ? dynamic_cast<ConstantInt *>(select->get_operand(1))
                     : nullptr;
    auto *bound = select
                      ? dynamic_cast<ConstantInt *>(select->get_operand(2))
                      : nullptr;
    return last && bound && last->value_ == lastValue &&
           bound->value_ == boundValue;
}

} // namespace

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LastIterationElimination::execute(Module *module) {
    BasicAliasAnalysis aliasAnalysis;
    aliasAnalysis.analyze(module);
    FunctionTerminationAnalysis terminationAnalysis(module);
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            runOnFunction(function, aliasAnalysis, terminationAnalysis);
    }
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses
LastIterationElimination::execute(Module *module, AnalysisManager &AM) {
    BasicAliasAnalysis &aliasAnalysis = AM.getBasicAA(module);
    FunctionTerminationAnalysis terminationAnalysis(module);
    bool changed = false;
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            changed |= runOnFunction(function, aliasAnalysis,
                                     terminationAnalysis);
    }
    return changed ? PreservedAnalyses::none()
                   : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param function 待分析或改写的函数。
 * @param aliasAnalysis 参数 `aliasAnalysis`，用于本函数的分析、匹配或 IR 构造。
 * @param terminationAnalysis 参数 `terminationAnalysis`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LastIterationElimination::runOnFunction(
    Function *function, BasicAliasAnalysis &aliasAnalysis,
    FunctionTerminationAnalysis &terminationAnalysis) {
    LoopInfo loopInfo;
    loopInfo.analyze(function);

    std::vector<Loop *> loops;
    for (const auto &loop : loopInfo.allLoops())
        loops.push_back(loop.get());
    std::sort(loops.begin(), loops.end(), [](Loop *lhs, Loop *rhs) {
        return lhs->depth < rhs->depth;
    });

    bool changed = false;
    for (Loop *loop : loops) {
        changed |= tryTransform(*loop, function, aliasAnalysis,
                                terminationAnalysis);
    }
    if (changed)
        function->set_instr_name();
    return changed;
}

/**
 * @brief 尝试执行 Transform 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param function 待分析或改写的函数。
 * @param aliasAnalysis 参数 `aliasAnalysis`，用于本函数的分析、匹配或 IR 构造。
 * @param terminationAnalysis 参数 `terminationAnalysis`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LastIterationElimination::tryTransform(
    Loop &loop, Function *function, BasicAliasAnalysis &aliasAnalysis,
    FunctionTerminationAnalysis &terminationAnalysis) {
    // 变换成立需同时证明：计数循环末值可计算、早期迭代结果不逃逸、
    // 循环体没有可观察调用或别名内存副作用。所有证明完成后才改写起始值。
    if (!loop.preheader || !loop.singleLatch() || !loop.singleExit() ||
        !loop.children.empty() || loop.exiting.size() != 1 ||
        loop.exiting.front() != loop.header)
        return false;

    InductionDescriptor equalityControl;
    if (!describeEqualityControlInduction(loop, equalityControl))
        return false;
    const InductionDescriptor *control = &equalityControl;
    if (!control->constantStep ||
        control->guardPosition != InductionGuardPosition::Header ||
        control->comparesUpdate ||
        control->predicate != ICmpInst::ICMP_NE ||
        (*control->constantStep != 1 && *control->constantStep != -1) ||
        !isI32(control->phi))
        return false;

    auto *bound = dynamic_cast<ConstantInt *>(control->bound);
    if (!bound)
        return false;

    // 后续等式证明按普通 i32 环绕递推成立；若原更新显式带 NSW/NUW，
    // 改写会加强或改变其语义，因此该候选必须拒绝。
    if (control->update->hasSemFlag(SemFlag::NoSignedWrap) ||
        control->update->hasSemFlag(SemFlag::NoUnsignedWrap))
        return false;

    const int64_t lastWide =
        static_cast<int64_t>(bound->value_) - *control->constantStep;
    if (lastWide < std::numeric_limits<int32_t>::min() ||
        lastWide > std::numeric_limits<int32_t>::max())
        return false;
    const int lastValue = static_cast<int>(lastWide);
    if (isAlreadyClamped(control->start, lastValue, bound->value_))
        return false;

    bool hasOverwriteLiveOut = false;
    for (auto *instruction : loop.header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi)
            break;
        if (phi == control->phi)
            continue;
        for (const Use &use : phi->use_list_) {
            if (isInsideUse(use, loop))
                return false;
        }
        hasOverwriteLiveOut |= hasEscapingUse(phi, loop);
    }
    if (!hasOverwriteLiveOut)
        return false;

    for (auto *block : loop.blocksOrdered) {
        for (auto *instruction : block->instr_list_) {
            if (instruction->is_store() || instruction->is_alloca() ||
                instruction->is_ret())
                return false;

            if (auto *call = dynamic_cast<CallInst *>(instruction)) {
                Function *callee = calledFunction(call);
                if (!callee || aliasAnalysis.mayHaveSideEffect(callee) ||
                    !terminationAnalysis.mustReturn(callee))
                    return false;
            }

            if (instruction->is_phi())
                continue;
            for (const Use &use : instruction->use_list_) {
                auto *user = use.user_;
                if (user && user->parent_ &&
                    !loop.isInLoop(user->parent_))
                    return false;
            }
        }
    }

    int startIndex = incomingIndex(control->phi, loop.preheader);
    int updateIndex = incomingIndex(control->phi, loop.singleLatch());
    if (startIndex < 0 || updateIndex < 0 ||
        control->phi->get_operand(startIndex) != control->start ||
        control->phi->get_operand(updateIndex) != control->update)
        return false;

    Type *type = control->phi->type_;
    auto *last = new ConstantInt(type, lastValue);
    auto *exitValue = new ConstantInt(type, bound->value_);
    auto *enters = new ICmpInst(ICmpInst::ICMP_NE, control->start,
                                exitValue, loop.preheader, true);
    auto *clamped = new SelectInst(enters, last, exitValue, type);
    if (!loop.preheader->add_instruction_before_terminator(enters) ||
        !loop.preheader->add_instruction_before_terminator(clamped))
        return false;

    control->phi->set_operand(startIndex, clamped);

    if (std::getenv("DEBUG_LAST_ITERATION_ELIMINATION")) {
        std::cerr << "[LastIterationElimination] function="
                  << function->name_ << " header=" << loop.header->name_
                  << " last=" << lastValue << "\n";
    }
    return true;
}
