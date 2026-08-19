// 典型示例：
//   优化前：汇合块含 phi [%x, %left], [%x, %right]，随后又重复计算 add %x, 1。
//   优化后：PHI 折叠为 %x，重复表达式复用支配位置已有的结果。
// 它清理后期 CFG 和循环变换遗留的局部 SSA 冗余。

#include "../../include/mid/opt/lateValueCleanup.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <vector>

// LateValueCleanup 收集 CFG 变换后相邻块产生的重复纯表达式。
// 范围刻意限制在单前驱边，前驱中的候选天然支配当前块，因而无需重建完整 GVN 状态。

namespace {

// 比较 SSA 身份或常量内容，使独立创建但数值相同的常量也可匹配。
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

// 筛选无内存副作用且可用既有结果替换的值指令。
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

// 比较 opcode、比较谓词与全部操作数，确认两条指令计算同一表达式。
bool sameExpression(Instruction *a, Instruction *b) {
    if (!a || !b || a->op_id_ != b->op_id_ || a->num_ops() != b->num_ops())
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
    for (unsigned i = 0; i < a->num_ops(); ++i) {
        if (!sameValue(a->get_operand(i), b->get_operand(i)))
            return false;
    }
    return true;
}

// 从前驱末尾向前搜索等价表达式，优先复用离分支最近的可用值。
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

// 处理单前驱块：用前驱中的等价值替换当前指令，并在无 use 后删除旧指令。
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

// 旧式入口，逐函数执行相邻块表达式清理。
void LateValueCleanup::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            cleanupFunction(func);
    }
}

// 分析管理器入口；发生替换时保守失效全部分析。
PreservedAnalyses LateValueCleanup::execute(Module *module, AnalysisManager &) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        changed |= cleanupFunction(func);
    }
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
