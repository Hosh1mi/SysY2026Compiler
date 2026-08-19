// 典型示例：
//   优化前：%sum = add %a, %b 位于分支前，且仅在 %then 中使用。
//   优化后：%sum 移入 %then，并放在首次使用之前。
// 下沉缩短值的生存区间，也避免未走 %then 路径时执行无用计算。

#include "../../include/mid/opt/codeSink.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <set>
#include <vector>

// CodeSink 将纯计算移动到所有 use 的最近公共使用区域，缩短值的活跃区间。
// 下沉前同时检查支配性与循环深度，防止操作数在新位置不可用或计算被移入更深循环。

namespace {

// 限定可下沉的无副作用指令集合，并排除 PHI、终结指令和无 use 的死值。
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

// PHI 的某个 value 操作数只在其配对 predecessor edge 上使用。
BasicBlock *phiIncomingBlock(PhiInst *phi, unsigned argNo) {
    if (!phi || (argNo % 2) != 0 || argNo + 1 >= phi->num_ops())
        return nullptr;
    return dynamic_cast<BasicBlock *>(phi->get_operand(argNo + 1));
}

// 将普通 use 映射到 user 所在块，将 PHI use 映射到对应入边的前驱块。
BasicBlock *useBlock(const Use &use) {
    auto *user = use.user_;
    if (!user)
        return nullptr;
    if (auto *phi = dynamic_cast<PhiInst *>(user))
        return phiIncomingBlock(phi, use.operand_index_);
    return user->parent_;
}

// 返回基本块所属的最内层循环深度，循环外为 0。
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

// 检查 user 当前是否仍引用 value，过滤前序替换留下的过期 Use 快照。
bool userUsesValue(Instruction *user, Value *value) {
    if (!user)
        return false;
    for (unsigned i = 0; i < user->num_ops(); ++i) {
        if (user->get_operand(i) == value)
            return true;
    }
    return false;
}

// 在目标块中选择最早且满足全部依赖的插入点，同时保证新定义位于所有本块 use 之前。
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

// 兼容旧式入口，内部创建分析管理器。
void CodeSink::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

// 逐函数执行下沉；只移动指令，不改 CFG，因此保留 CFG 类分析。
PreservedAnalyses CodeSink::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

// 计算所有 use 块的最近公共支配块，验证循环深度后将候选指令移动到该块。
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
