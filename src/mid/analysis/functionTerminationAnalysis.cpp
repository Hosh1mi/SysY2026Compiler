// FunctionTerminationAnalysis 证明函数在定义行为下是否必然返回。它检查所有可达路径，并
// 只在循环具有可证明的单调控制变量和有限边界时接受循环。

#include "../../include/mid/analysis/functionTerminationAnalysis.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <cstdlib>

namespace {

Function *calledFunction(CallInst *call) {
    if (!call || call->num_ops() == 0)
        return nullptr;
    return dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
}

bool isUnitStep(const InductionDescriptor &control, long long expected) {
    return control.constantStep && *control.constantStep == expected;
}

} // namespace

bool FunctionTerminationAnalysis::mustReturn(Function *function) {
    if (!module_ || !function || function->parent_ != module_)
        return false;
    return analyzeFunction(function);
}

bool FunctionTerminationAnalysis::loopIsFinite(const Loop &loop) const {
    if (!loop.preheader || !loop.singleLatch() || !loop.singleExit() ||
        loop.exiting.size() != 1)
        return false;

    InductionDescriptor equalityControl;
    const InductionDescriptor *control = loop.getInductionDescriptor();
    if (!control &&
        describeEqualityControlInduction(loop, equalityControl))
        control = &equalityControl;
    if (!control || !control->constantStep)
        return false;

    BasicBlock *guard =
        control->guardPosition == InductionGuardPosition::Header
            ? loop.header
            : loop.singleLatch();
    if (loop.exiting.front() != guard)
        return false;

    // A unit-step inequality visits every i32 value in the recurrence ring.
    // Consequently the equality point is reached after finitely many steps.
    if (control->predicate == ICmpInst::ICMP_NE)
        return isUnitStep(*control, 1) || isUnitStep(*control, -1);

    // These header-tested monotone forms either execute zero times or stop at
    // the bound without overflowing the induction value.
    if (control->guardPosition != InductionGuardPosition::Header ||
        control->comparesUpdate)
        return false;
    if (control->predicate == ICmpInst::ICMP_SLT)
        return isUnitStep(*control, 1);
    if (control->predicate == ICmpInst::ICMP_SGT)
        return isUnitStep(*control, -1);
    return false;
}

// analyzeFunction：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool FunctionTerminationAnalysis::analyzeFunction(Function *function) {
    State &state = states_[function];
    if (state == State::Returns)
        return true;
    if (state == State::MayNotReturn || state == State::Visiting)
        return false;
    if (function->is_declaration()) {
        state = State::MayNotReturn;
        return false;
    }

    state = State::Visiting;

    LoopInfo loopInfo;
    loopInfo.analyze(function);
    for (const auto &loop : loopInfo.allLoops()) {
        if (!loopIsFinite(*loop)) {
            state = State::MayNotReturn;
            return false;
        }
    }

    bool hasReturn = false;
    for (auto *block : function->basic_blocks_) {
        Instruction *terminator = block->get_terminator();
        if (!terminator) {
            state = State::MayNotReturn;
            return false;
        }
        if (terminator->is_ret())
            hasReturn = true;

        for (auto *instruction : block->instr_list_) {
            auto *call = dynamic_cast<CallInst *>(instruction);
            if (!call)
                continue;
            Function *callee = calledFunction(call);
            if (!callee || !analyzeFunction(callee)) {
                state = State::MayNotReturn;
                return false;
            }
        }
    }

    state = hasReturn ? State::Returns : State::MayNotReturn;
    return state == State::Returns;
}
