#include "../../../include/mid/opt/indVarSimplify.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <vector>

namespace {

struct Recurrence {
    PhiInst *phi = nullptr;
    BinaryInst *update = nullptr;
    Value *start = nullptr;
    Value *step = nullptr;
    bool subtractStep = false;
    std::vector<Use> outsideUses;
};

bool isI32(Value *value) {
    auto *type = value ? dynamic_cast<IntegerType *>(value->type_) : nullptr;
    return type && type->num_bits_ == 32;
}

bool isZero(Value *value) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    return constant && constant->value_ == 0;
}

bool isLoopInvariant(Value *value, const Loop &loop) {
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && !loop.blocks.count(inst->parent_);
}

bool getIncomingValues(PhiInst *phi, const Loop &loop, Value *&start,
                       Value *&latchValue) {
    start = nullptr;
    latchValue = nullptr;
    BasicBlock *latch = loop.singleLatch();
    if (!phi || phi->num_ops_ != 4 || !loop.preheader || !latch)
        return false;

    for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
        auto *source = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (source == loop.preheader)
            start = phi->get_operand(i);
        else if (source == latch)
            latchValue = phi->get_operand(i);
        else
            return false;
    }
    return start && latchValue;
}

bool matchUpdate(Value *latchValue, PhiInst *phi, const Loop &loop,
                 BinaryInst *&update, Value *&step, bool &subtractStep) {
    update = dynamic_cast<BinaryInst *>(latchValue);
    if (!update || update->parent_ != loop.singleLatch() ||
        !isI32(update))
        return false;

    Value *lhs = update->get_operand(0);
    Value *rhs = update->get_operand(1);
    if (update->is_add()) {
        if (lhs == phi)
            step = rhs;
        else if (rhs == phi)
            step = lhs;
        else
            return false;
    } else if (update->is_sub() && lhs == phi) {
        step = rhs;
        subtractStep = true;
    } else {
        return false;
    }
    return isLoopInvariant(step, loop);
}

bool onlyUsedBy(Value *value, Instruction *expectedUser) {
    if (!value || !expectedUser || value->use_list_.empty())
        return false;
    for (const Use &use : value->use_list_) {
        if (use.val_ != expectedUser)
            return false;
    }
    return true;
}

bool matchDeadLiveOutRecurrence(PhiInst *phi, const Loop &loop,
                                Recurrence &recurrence) {
    if (!phi || phi == loop.canonicalIV ||
        phi == loop.controlInduction.phi || !isI32(phi))
        return false;

    Value *start = nullptr;
    Value *latchValue = nullptr;
    if (!getIncomingValues(phi, loop, start, latchValue) ||
        !isLoopInvariant(start, loop))
        return false;

    BinaryInst *update = nullptr;
    Value *step = nullptr;
    bool subtractStep = false;
    if (!matchUpdate(latchValue, phi, loop, update, step, subtractStep))
        return false;

    std::vector<Use> outsideUses;
    for (const Use &use : phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !user->parent_)
            return false;
        if (loop.blocks.count(user->parent_)) {
            if (user != update)
                return false;
        } else {
            outsideUses.push_back(use);
        }
    }
    if (outsideUses.empty() || !onlyUsedBy(update, phi))
        return false;

    recurrence.phi = phi;
    recurrence.update = update;
    recurrence.start = start;
    recurrence.step = step;
    recurrence.subtractStep = subtractStep;
    recurrence.outsideUses = std::move(outsideUses);
    return true;
}

ConstantInt *exactCanonicalTripCount(const Loop &loop) {
    const InductionDescriptor *control = loop.getInductionDescriptor();
    if (!control || !loop.hasCanonicalIV() ||
        control->phi != loop.canonicalIV || !control->isUnitStride() ||
        control->guardPosition != InductionGuardPosition::Header ||
        control->comparesUpdate ||
        control->predicate != ICmpInst::ICMP_SLT ||
        loop.exiting.size() != 1 || loop.exiting.front() != loop.header ||
        loop.exits.size() != 1)
        return nullptr;

    auto *start = dynamic_cast<ConstantInt *>(control->start);
    auto *bound = dynamic_cast<ConstantInt *>(control->bound);
    if (!start || start->value_ != 0 || !bound || bound->value_ < 0)
        return nullptr;
    return bound;
}

Value *materializeExitValue(const Recurrence &recurrence,
                            ConstantInt *tripCount, const Loop &loop,
                            Module *module) {
    if (!tripCount || !loop.preheader || !module)
        return nullptr;

    long long trips = tripCount->value_;
    if (trips == 0)
        return recurrence.start;

    Instruction *terminator = loop.preheader->get_terminator();
    Value *scaledStep = recurrence.step;
    if (trips != 1) {
        auto *scale = new ConstantInt(module->int32_ty_,
                                      static_cast<int>(trips));
        auto *multiply = new BinaryInst(module->int32_ty_,
                                        Instruction::Mul,
                                        recurrence.step, scale,
                                        loop.preheader, true);
        loop.preheader->add_instruction_before_inst(multiply, terminator);
        scaledStep = multiply;
    }

    if (!recurrence.subtractStep && isZero(recurrence.start))
        return scaledStep;

    Instruction::OpID operation = recurrence.subtractStep
                                      ? Instruction::Sub
                                      : Instruction::Add;
    auto *result = new BinaryInst(module->int32_ty_, operation,
                                  recurrence.start, scaledStep,
                                  loop.preheader, true);
    loop.preheader->add_instruction_before_inst(result, terminator);
    return result;
}

bool simplifyLoop(Loop &loop, Module *module) {
    ConstantInt *tripCount = exactCanonicalTripCount(loop);
    if (!tripCount || !loop.preheader || !loop.singleLatch())
        return false;

    std::vector<Recurrence> candidates;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        Recurrence recurrence;
        if (matchDeadLiveOutRecurrence(static_cast<PhiInst *>(inst), loop,
                                       recurrence))
            candidates.push_back(std::move(recurrence));
    }

    bool changed = false;
    for (const Recurrence &recurrence : candidates) {
        Value *exitValue =
            materializeExitValue(recurrence, tripCount, loop, module);
        if (!exitValue) continue;

        for (const Use &use : recurrence.outsideUses) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user)
                user->set_operand(use.arg_no_, exitValue);
        }

        if (onlyUsedBy(recurrence.phi, recurrence.update) &&
            onlyUsedBy(recurrence.update, recurrence.phi)) {
            recurrence.phi->parent_->delete_instr(recurrence.phi);
            recurrence.update->parent_->delete_instr(recurrence.update);
        }
        changed = true;
    }
    return changed;
}

} // namespace

void IndVarSimplify::execute(Module *module) {
    AnalysisManager analysisManager;
    execute(module, analysisManager);
}

PreservedAnalyses IndVarSimplify::execute(Module *module,
                                          AnalysisManager &analysisManager) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, analysisManager);
    }
    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveBasicAA();
    preserved.preserveLoopInfo();
    return preserved;
}

bool IndVarSimplify::runOnFunction(Function *func,
                                   AnalysisManager &analysisManager) {
    if (!func || func->basic_blocks_.empty())
        return false;

    LoopInfo &loopInfo = analysisManager.getLoopInfo(func);
    std::vector<Loop *> loops;
    for (const auto &loop : loopInfo.allLoops())
        loops.push_back(loop.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *lhs, Loop *rhs) {
                  return lhs->depth > rhs->depth;
              });

    bool changed = false;
    for (Loop *loop : loops)
        changed |= simplifyLoop(*loop, func->parent_);
    return changed;
}
