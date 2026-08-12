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

Function *calledFunction(CallInst *call) {
    if (!call || call->num_ops() == 0)
        return nullptr;
    return dynamic_cast<Function *>(call->get_operand(call->num_ops() - 1));
}

bool isI32(Value *value) {
    auto *integer = value
                        ? dynamic_cast<IntegerType *>(value->type_)
                        : nullptr;
    return integer && integer->num_bits_ == 32;
}

int incomingIndex(PhiInst *phi, BasicBlock *source) {
    if (!phi || !source)
        return -1;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == source)
            return static_cast<int>(i);
    }
    return -1;
}

bool isInsideUse(const Use &use, const Loop &loop) {
    auto *user = use.user_;
    return user && user->parent_ && loop.isInLoop(user->parent_);
}

bool hasEscapingUse(Value *value, const Loop &loop) {
    for (const Use &use : value->use_list_) {
        auto *user = use.user_;
        if (user && user->parent_ && !loop.isInLoop(user->parent_))
            return true;
    }
    return false;
}

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

void LastIterationElimination::execute(Module *module) {
    BasicAliasAnalysis aliasAnalysis;
    aliasAnalysis.analyze(module);
    FunctionTerminationAnalysis terminationAnalysis(module);
    for (auto *function : module->function_list_) {
        if (!function->is_declaration())
            runOnFunction(function, aliasAnalysis, terminationAnalysis);
    }
}

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

bool LastIterationElimination::tryTransform(
    Loop &loop, Function *function, BasicAliasAnalysis &aliasAnalysis,
    FunctionTerminationAnalysis &terminationAnalysis) {
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

    // Avoid strengthening an explicitly non-wrapping recurrence.  The
    // equality proof below relies on the ordinary i32 recurrence ring.
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
