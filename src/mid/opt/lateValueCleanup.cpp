#include "../../include/mid/opt/lateValueCleanup.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <vector>

namespace {

bool sameValue(Value *a, Value *b) {
    if (a == b)
        return true;
    auto *ai = dynamic_cast<ConstantInt *>(a);
    auto *bi = dynamic_cast<ConstantInt *>(b);
    if (ai && bi)
        return ai->value_ == bi->value_;
    auto *af = dynamic_cast<ConstantFloat *>(a);
    auto *bf = dynamic_cast<ConstantFloat *>(b);
    if (af && bf)
        return af->value_ == bf->value_;
    return dynamic_cast<ConstantZero *>(a) && dynamic_cast<ConstantZero *>(b);
}

bool isPureValueInst(Instruction *inst) {
    switch (inst->op_id_) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::SDiv:
        case Instruction::SRem:
        case Instruction::UDiv:
        case Instruction::URem:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
        case Instruction::ICmp:
        case Instruction::FCmp:
        case Instruction::ZExt:
        case Instruction::FPtoSI:
        case Instruction::SItoFP:
        case Instruction::BitCast:
        case Instruction::Clz:
        case Instruction::GetElementPtr:
            return true;
        default:
            return false;
    }
}

bool sameExpression(Instruction *a, Instruction *b) {
    if (!a || !b || a->op_id_ != b->op_id_ || a->num_ops_ != b->num_ops_)
        return false;
    if (auto *ac = dynamic_cast<ICmpInst *>(a)) {
        auto *bc = dynamic_cast<ICmpInst *>(b);
        if (!bc || ac->icmp_op_ != bc->icmp_op_)
            return false;
    }
    if (auto *ac = dynamic_cast<FCmpInst *>(a)) {
        auto *bc = dynamic_cast<FCmpInst *>(b);
        if (!bc || ac->fcmp_op_ != bc->fcmp_op_)
            return false;
    }
    for (unsigned i = 0; i < a->num_ops_; ++i) {
        if (!sameValue(a->get_operand(i), b->get_operand(i)))
            return false;
    }
    return true;
}

Instruction *findEquivalentInPred(Instruction *inst, BasicBlock *pred) {
    for (auto it = pred->instr_list_.rbegin(); it != pred->instr_list_.rend(); ++it) {
        auto *cand = *it;
        if (!isPureValueInst(cand))
            continue;
        if (sameExpression(inst, cand))
            return cand;
    }
    return nullptr;
}

bool cleanupFunction(Function *func) {
    bool changed = false;
    for (auto *bb : func->basic_blocks_) {
        if (bb->pre_bbs_.size() != 1)
            continue;
        BasicBlock *pred = bb->pre_bbs_.front();
        std::vector<Instruction *> worklist;
        for (auto *inst : bb->instr_list_) {
            if (isPureValueInst(inst))
                worklist.push_back(inst);
        }
        for (auto *inst : worklist) {
            if (inst->parent_ != bb || !inst->use_list_.empty() && inst->isTerminator())
                continue;
            Instruction *replacement = findEquivalentInPred(inst, pred);
            if (!replacement)
                continue;
            inst->replace_all_use_with(replacement);
            if (inst->use_list_.empty() && bb->delete_instr(inst))
                changed = true;
        }
    }
    return changed;
}

} // namespace

void LateValueCleanup::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            cleanupFunction(func);
    }
}

PreservedAnalyses LateValueCleanup::execute(Module *module, AnalysisManager &) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        changed |= cleanupFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
