#include "../../include/mid/opt/loopIRCE.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <vector>

namespace {

bool isLoopInvariant(Value *value, const Loop &loop) {
    if (dynamic_cast<Constant *>(value) || dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && inst->parent_ && !loop.blocks.count(inst->parent_);
}

bool isZeroConst(Value *value) {
    auto *ci = dynamic_cast<ConstantInt *>(value);
    return ci && ci->value_ == 0;
}

bool isOneConst(Value *value) {
    auto *ci = dynamic_cast<ConstantInt *>(value);
    return ci && ci->value_ == 1;
}

Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

bool isOnlyIVUpdateAndLatchCmp(BasicBlock *latch, Instruction *ivNext,
                               ICmpInst *latchCmp) {
    std::vector<Instruction *> body;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator())
            break;
        body.push_back(inst);
    }
    return body.size() == 2 && body[0] == ivNext && body[1] == latchCmp;
}

bool isPureContinueBlock(BasicBlock *block) {
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    if (!term || term->num_ops_ != 1)
        return false;

    for (auto *inst : block->instr_list_) {
        if (inst == term)
            break;
        if (inst->is_store() || inst->is_call() || inst->is_load() ||
            inst->is_alloca())
            return false;
        for (auto &use : inst->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ != block)
                return false;
        }
    }
    return true;
}

} // namespace

void LoopIRCE::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses LoopIRCE::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LoopIRCE::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty())
        return false;

    LoopInfo LI;
    LI.analyze(func);

    std::vector<Loop *> loops;
    for (auto &loop : LI.allLoops())
        loops.push_back(loop.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    bool changed = false;
    for (auto *loop : loops)
        changed |= tryTightenLoop(*loop, func->parent_);

    if (changed)
        func->set_instr_name();
    return changed;
}

bool LoopIRCE::tryTightenLoop(Loop &loop, Module *module) {
    BasicBlock *preheader = loop.preheader;
    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    if (!preheader || !header || !latch)
        return false;

    auto *preTerm = dynamic_cast<BranchInst *>(preheader->get_terminator());
    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!preTerm || !headerTerm || !latchTerm)
        return false;
    if (preTerm->num_ops_ != 3 || headerTerm->num_ops_ != 3 || latchTerm->num_ops_ != 3)
        return false;

    auto *preTrue = dynamic_cast<BasicBlock *>(preTerm->get_operand(1));
    auto *preFalse = dynamic_cast<BasicBlock *>(preTerm->get_operand(2));
    if (preTrue != header || !preFalse || loop.isInLoop(preFalse))
        return false;

    auto *latchTrue = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
    if (latchTrue != header || !latchFalse || loop.isInLoop(latchFalse))
        return false;

    auto *cont = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *work = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!cont || !work || cont == work || !loop.isInLoop(cont) ||
        !loop.isInLoop(work))
        return false;

    auto *contTerm = dynamic_cast<BranchInst *>(cont->get_terminator());
    auto *workTerm = dynamic_cast<BranchInst *>(work->get_terminator());
    if (!contTerm || contTerm->num_ops_ != 1 || contTerm->get_operand(0) != latch)
        return false;
    if (!workTerm || workTerm->num_ops_ != 1 || workTerm->get_operand(0) != latch)
        return false;

    PhiInst *ivPhi = nullptr;
    Instruction *ivNext = nullptr;
    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID || phi->num_ops_ != 4)
            continue;

        Value *fromPre = incomingFrom(phi, preheader);
        Value *fromLatch = incomingFrom(phi, latch);
        auto *add = dynamic_cast<BinaryInst *>(fromLatch);
        if (!fromPre || !add || !add->is_add())
            continue;

        Value *op0 = add->get_operand(0);
        Value *op1 = add->get_operand(1);
        if (!isZeroConst(fromPre))
            continue;
        if (!((op0 == phi && isOneConst(op1)) || (op1 == phi && isOneConst(op0))))
            continue;

        ivPhi = phi;
        ivNext = add;
        break;
    }
    if (!ivPhi || !ivNext)
        return false;

    auto *latchCmp = dynamic_cast<ICmpInst *>(latchTerm->get_operand(0));
    if (!latchCmp || latchCmp->icmp_op_ != ICmpInst::ICMP_SLT)
        return false;
    if (latchCmp->get_operand(0) != ivNext)
        return false;
    Value *bound = latchCmp->get_operand(1);
    if (!bound || bound->type_->tid_ != Type::IntegerTyID || !isLoopInvariant(bound, loop))
        return false;

    auto *preCmp = dynamic_cast<ICmpInst *>(preTerm->get_operand(0));
    if (!preCmp)
        return false;
    bool preCheckMatches =
        ((preCmp->icmp_op_ == ICmpInst::ICMP_SGT && preCmp->get_operand(0) == bound &&
          isZeroConst(preCmp->get_operand(1))) ||
         (preCmp->icmp_op_ == ICmpInst::ICMP_SLT && isZeroConst(preCmp->get_operand(0)) &&
          preCmp->get_operand(1) == bound));
    if (!preCheckMatches)
        return false;

    auto *guardCmp = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    if (!guardCmp || guardCmp->parent_ != header)
        return false;

    Value *outer = nullptr;
    bool strict = false;
    if (guardCmp->icmp_op_ == ICmpInst::ICMP_SLT &&
        guardCmp->get_operand(1) == ivPhi) {
        outer = guardCmp->get_operand(0);
        strict = true;
    } else if (guardCmp->icmp_op_ == ICmpInst::ICMP_SLE &&
               guardCmp->get_operand(1) == ivPhi) {
        outer = guardCmp->get_operand(0);
        strict = false;
    } else if (guardCmp->icmp_op_ == ICmpInst::ICMP_SGT &&
               guardCmp->get_operand(0) == ivPhi) {
        outer = guardCmp->get_operand(1);
        strict = true;
    } else if (guardCmp->icmp_op_ == ICmpInst::ICMP_SGE &&
               guardCmp->get_operand(0) == ivPhi) {
        outer = guardCmp->get_operand(1);
        strict = false;
    } else {
        return false;
    }
    if (!outer || outer->type_->tid_ != Type::IntegerTyID || !isLoopInvariant(outer, loop))
        return false;

    if (!isPureContinueBlock(cont))
        return false;
    if (!isOnlyIVUpdateAndLatchCmp(latch, ivNext, latchCmp))
        return false;

    Instruction *insertBefore = preheader->get_terminator();
    Value *limit = outer;
    if (strict) {
        auto *plusOne = new BinaryInst(module->int32_ty_, Instruction::Add, outer,
                                       new ConstantInt(module->int32_ty_, 1),
                                       preheader, true);
        if (!preheader->add_instruction_before_inst(plusOne, insertBefore))
            return false;
        limit = plusOne;
    }

    auto *minCmp = new ICmpInst(ICmpInst::ICMP_SLT, limit, bound, preheader, true);
    if (!preheader->add_instruction_before_inst(minCmp, insertBefore))
        return false;

    auto *newBound = new SelectInst(minCmp, limit, bound, module->int32_ty_);
    if (!preheader->add_instruction_before_inst(newBound, insertBefore))
        return false;

    auto *positiveCmp = new ICmpInst(ICmpInst::ICMP_SGT, newBound,
                                     new ConstantInt(module->int32_ty_, 0),
                                     preheader, true);
    if (!preheader->add_instruction_before_inst(positiveCmp, insertBefore))
        return false;

    preTerm->set_operand(0, positiveCmp);
    latchCmp->set_operand(1, newBound);
    return true;
}
