#include "../../include/mid/opt/codeSink.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <set>
#include <vector>

namespace {

bool isSinkableInstruction(Instruction *inst) {
    if (!inst || inst->is_phi() || inst->isTerminator() || inst->use_list_.empty())
        return false;

    switch (inst->op_id_) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FNeg:
        case Instruction::ZExt:
        case Instruction::FPtoSI:
        case Instruction::SItoFP:
        case Instruction::BitCast:
        case Instruction::ICmp:
        case Instruction::FCmp:
        case Instruction::Select:
        case Instruction::GetElementPtr:
            return true;
        default:
            return false;
    }
}

BasicBlock *phiIncomingBlock(PhiInst *phi, unsigned argNo) {
    if (!phi || (argNo % 2) != 0 || argNo + 1 >= phi->num_ops_)
        return nullptr;
    return dynamic_cast<BasicBlock *>(phi->get_operand(argNo + 1));
}

BasicBlock *useBlock(const Use &use) {
    auto *user = dynamic_cast<Instruction *>(use.val_);
    if (!user)
        return nullptr;
    if (auto *phi = dynamic_cast<PhiInst *>(user))
        return phiIncomingBlock(phi, use.arg_no_);
    return user->parent_;
}

BasicBlock *nearestCommonDominator(const DominatorInfo &domInfo,
                                   BasicBlock *a,
                                   BasicBlock *b) {
    if (!a || !b)
        return nullptr;

    std::set<BasicBlock *> ancestors;
    for (auto *cur = a; cur;) {
        ancestors.insert(cur);
        auto it = domInfo.idom.find(cur);
        cur = (it == domInfo.idom.end()) ? nullptr : it->second;
    }

    for (auto *cur = b; cur;) {
        if (ancestors.count(cur))
            return cur;
        auto it = domInfo.idom.find(cur);
        cur = (it == domInfo.idom.end()) ? nullptr : it->second;
    }
    return nullptr;
}

int loopDepth(LoopInfo &LI, BasicBlock *bb) {
    Loop *loop = LI.getLoopFor(bb);
    return loop ? loop->depth + 1 : 0;
}

bool operandsDominateTarget(Instruction *inst, Function *func, BasicBlock *target) {
    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        auto *opInst = dynamic_cast<Instruction *>(inst->get_operand(i));
        if (!opInst)
            continue;
        if (!opInst->parent_)
            return false;
        if (!func->dominates(opInst->parent_, target))
            return false;
    }
    return true;
}

bool userUsesValue(Instruction *user, Value *value) {
    if (!user)
        return false;
    for (unsigned i = 0; i < user->num_ops_; ++i) {
        if (user->get_operand(i) == value)
            return true;
    }
    return false;
}

Instruction *findInsertionPoint(Instruction *inst, BasicBlock *target) {
    for (auto *cur : target->instr_list_) {
        if (cur->is_phi())
            continue;
        if (userUsesValue(cur, inst))
            return cur;
    }
    return target->get_terminator();
}

bool trySinkInstruction(Instruction *inst, Function *func, LoopInfo &LI) {
    if (!isSinkableInstruction(inst))
        return false;

    BasicBlock *source = inst->parent_;
    if (!source)
        return false;

    DominatorInfo &domInfo = func->getDominatorInfo();
    BasicBlock *target = nullptr;
    for (const auto &use : inst->use_list_) {
        BasicBlock *block = useBlock(use);
        if (!block)
            return false;
        target = target ? nearestCommonDominator(domInfo, target, block) : block;
        if (!target)
            return false;
    }

    if (!target || target == source)
        return false;
    if (!func->dominates(source, target))
        return false;
    if (loopDepth(LI, target) > loopDepth(LI, source))
        return false;
    if (!operandsDominateTarget(inst, func, target))
        return false;

    Instruction *insertBefore = findInsertionPoint(inst, target);
    if (!insertBefore)
        return false;

    if (!source->remove_instr(inst))
        return false;
    if (!target->add_instruction_before_inst(inst, insertBefore)) {
        source->add_instruction_before_terminator(inst);
        return false;
    }
    return true;
}

} // namespace

void CodeSink::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

PreservedAnalyses CodeSink::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool CodeSink::runOnFunction(Function *func) {
    bool changed = false;
    bool localChanged = true;

    while (localChanged) {
        localChanged = false;

        LoopInfo LI;
        LI.analyze(func);

        std::vector<Instruction *> worklist;
        for (auto *bb : func->basic_blocks_) {
            for (auto *inst : bb->instr_list_)
                worklist.push_back(inst);
        }

        for (auto it = worklist.rbegin(); it != worklist.rend(); ++it) {
            auto *inst = *it;
            if (!inst->parent_)
                continue;
            if (trySinkInstruction(inst, func, LI)) {
                changed = true;
                localChanged = true;
                break;
            }
        }
    }

    if (changed)
        func->set_instr_name();
    return changed;
}
