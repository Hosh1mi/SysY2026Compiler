#include "../../include/mid/opt/codeSink.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
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
    if (!phi || (argNo % 2) != 0 || argNo + 1 >= phi->num_ops())
        return nullptr;
    return dynamic_cast<BasicBlock *>(phi->get_operand(argNo + 1));
}

BasicBlock *useBlock(const Use &use) {
    auto *user = use.user_;
    if (!user)
        return nullptr;
    if (auto *phi = dynamic_cast<PhiInst *>(user))
        return phiIncomingBlock(phi, use.operand_index_);
    return user->parent_;
}

int loopDepth(LoopInfo &LI, BasicBlock *bb) {
    Loop *loop = LI.getLoopFor(bb);
    return loop ? loop->depth + 1 : 0;
}

bool operandsDominateTarget(Instruction *inst, const DominatorTreeAnalysis &DT,
                            BasicBlock *target) {
    for (unsigned i = 0; i < inst->num_ops(); ++i) {
        auto *opInst = dynamic_cast<Instruction *>(inst->get_operand(i));
        if (!opInst)
            continue;
        if (!opInst->parent_)
            return false;
        if (!DT.dominates(opInst->parent_, target))
            return false;
    }
    return true;
}

bool userUsesValue(Instruction *user, Value *value) {
    if (!user)
        return false;
    for (unsigned i = 0; i < user->num_ops(); ++i) {
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

bool trySinkInstruction(Instruction *inst, Function *func, LoopInfo &LI,
                        const DominatorTreeAnalysis &DT) {
    if (!isSinkableInstruction(inst))
        return false;

    BasicBlock *source = inst->parent_;
    if (!source)
        return false;

    BasicBlock *target = nullptr;
    for (const auto &use : inst->use_list_) {
        BasicBlock *block = useBlock(use);
        if (!block)
            return false;
        target = target ? DT.findNearestCommonDominator(target, block) : block;
        if (!target)
            return false;
    }

    if (!target || target == source)
        return false;
    if (!DT.dominates(source, target))
        return false;
    if (loopDepth(LI, target) > loopDepth(LI, source))
        return false;
    if (!operandsDominateTarget(inst, DT, target))
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
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses CodeSink::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

bool CodeSink::runOnFunction(Function *func, AnalysisManager &AM) {
    bool changed = false;

    // Sinking the accepted instructions changes neither CFG nor loop
    // membership, so LoopInfo and dominators remain valid for the whole
    // sweep.  Reverse order visits users before their operands: after a user
    // is sunk, an operand considered later sees the user's final block and
    // can be placed directly at its own final destination.  Restarting from
    // scratch after every successful move only turns the pass quadratic.
    LoopInfo &LI = AM.getLoopInfo(func);
    DominatorTreeAnalysis &DT = AM.getDominatorTree(func);

    std::vector<Instruction *> worklist;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_)
            worklist.push_back(inst);
    }

    for (auto it = worklist.rbegin(); it != worklist.rend(); ++it) {
        auto *inst = *it;
        if (!inst->parent_)
            continue;
        changed |= trySinkInstruction(inst, func, LI, DT);
    }

    if (changed)
        func->set_instr_name();
    return changed;
}
