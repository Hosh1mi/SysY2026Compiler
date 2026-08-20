/**
 * @file instCombine.cpp
 * @brief 实现 InstCombine 工作列表驱动器，反复调度局部指令化简并安全替换旧值。
 * @details 工作列表只提交语义等价替换，并把受影响的用户重新入队；删除旧指令前同步维护 use-list。
 */

#include "../../../include/mid/opt/instCombine.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "instCombineInternal.hpp"

#include <iostream>
#include <unordered_set>
#include <vector>

thread_local RangeAnalysis *gInstCombineRangeAnalysis = nullptr;
thread_local DominatorTreeAnalysis *gInstCombineDominatorTree = nullptr;

// ── trySinkInstruction ──────────────────────────────────────────────
// If inst has exactly one user and that user is in a different block,
// sink (move) inst to just before the user.  This is a just-in-time
// companion to the worklist loop: when a replacement removes one use
// of an operand, we check whether the operand can now be sunk.
//
// Safe when: user's block has inst's block as its sole predecessor.
// (Simpler than full dominator analysis and correct for the common
//  "computed before branch, used only in one successor" case.)

/**
 * @brief 判断 isSinkableOp 所描述的结构、合法性或安全条件是否成立。
 * @param op 待检查的操作码或操作数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isSinkableOp(Instruction::OpID op) {
    switch (op) {
        case Instruction::Add: case Instruction::Sub: case Instruction::Mul:
        case Instruction::SDiv: case Instruction::SRem:
        case Instruction::Shl: case Instruction::LShr: case Instruction::AShr:
        case Instruction::And: case Instruction::Or: case Instruction::Xor:
        case Instruction::FAdd: case Instruction::FSub:
        case Instruction::FMul: case Instruction::FDiv:
        case Instruction::FNeg:
        case Instruction::ZExt: case Instruction::FPtoSI:
        case Instruction::SItoFP: case Instruction::BitCast:
            return true;
        default:
            return false;  // loads, stores, calls — never sink
    }
}

/**
 * @brief 在支配与唯一使用条件满足时把纯指令下沉到使用点之前。
 * @param inst 待分析、化简或克隆的指令。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void trySinkInstruction(Instruction *inst) {
    if (inst->use_list_.size() != 1) return;

    auto *user = (*inst->use_list_.begin()).user_;
    if (!user) return;

    BasicBlock *user_bb = user->parent_;
    BasicBlock *inst_bb = inst->parent_;

    // Already in the same block — nothing to do
    if (user_bb == inst_bb) return;

    // Don't sink into PHI or terminator
    if (user->isTerminator()) return;
    if (user->op_id_ == Instruction::PHI) return;

    // Don't sink terminators or non-sinkable instructions
    if (inst->isTerminator() || !isSinkableOp(inst->op_id_)) return;

    // 下沉的支配性保证：用户块必须只有 inst 所在块这一个前驱。
    // 这样所有到达用户的路径都已经执行过 inst，不会出现未定义值。
    if (user_bb->pre_bbs_.size() != 1 || user_bb->pre_bbs_[0] != inst_bb)
        return;

    // Perform the sink
    inst_bb->remove_instr(inst);
    user_bb->add_instruction_before_inst(inst, user);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void InstCombine::execute(Module *module) {
    AnalysisManager AM;
    runPass(module, AM);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses InstCombine::execute(Module *module, AnalysisManager &AM) {
    return runPass(module, AM).preserved;
}

/**
 * @brief 运行一次 InstCombine 并汇总 IR 与语义事实的变化。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回计算、分析或构造得到的结果。
 */
PassRunResult InstCombine::runPass(Module *module, AnalysisManager &AM) {
    bool changed = false;
    auto functions = module->function_list_;
    for (auto func : functions) {
        if (func->is_declaration()) continue;
        changed |= runOnFunction(func, &AM);
    }
    return {changed, changed ? PreservedAnalyses::cfgAnalyses()
                             : PreservedAnalyses::all()};
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool InstCombine::runOnFunction(Function *func, AnalysisManager *AM) {
    // 核心流程：先把现存指令加入工作列表，再按 opcode 尝试局部替换；
    // 每次成功替换后重新调度用户和操作数，使新暴露的模式继续化简。
    // 两类预算限制最坏编译时间；范围分析在首次改写后丢弃，避免使用陈旧事实。
    auto countInstructions = [&]() -> size_t {
        size_t total = 0;
        for (auto *bb : func->basic_blocks_)
            total += bb->instr_list_.size();
        return total;
    };

    auto enqueueIfAlive = [](Instruction *inst,
                             std::vector<Instruction *> &worklist,
                             std::unordered_set<Instruction *> &inWorklist) {
        if (!inst || !inst->parent_) return;
        if (!inWorklist.insert(inst).second) return;
        worklist.push_back(inst);
    };

    const size_t initialInstrCount = countInstructions();
    // 范围事实可能直接引用被替换的指令，任意一次改写都会让 RangeAnalysis
    // 过期。普通函数在首次改写前使用一次范围分析；大函数只启用无需全局
    // 范围事实的局部规则，避免每次折叠都重建整函数分析。
    constexpr size_t kRangeAnalysisInstructionLimit = 1024;
    const bool useRangeAnalysis =
        AM && initialInstrCount <= kRangeAnalysisInstructionLimit;
    const size_t processBudget =
        std::max<size_t>(20000, initialInstrCount * 32);
    const size_t rewriteBudget =
        std::max<size_t>(4000, initialInstrCount * 16);
    size_t processedCount = 0;
    size_t rewrittenCount = 0;
    bool budgetHit = false;
    bool changed = false;

    RangeAnalysis *savedRangeAnalysis = gInstCombineRangeAnalysis;
    DominatorTreeAnalysis *savedDominatorTree = gInstCombineDominatorTree;
    gInstCombineDominatorTree = AM ? &AM->getDominatorTree(func) : nullptr;
    gInstCombineRangeAnalysis =
        useRangeAnalysis ? &AM->getRangeAnalysis(func) : nullptr;

    std::vector<Instruction*> worklist;
    std::unordered_set<Instruction *> inWorklist;
    for (auto *bb : func->basic_blocks_)
        for (auto *inst : bb->instr_list_)
            enqueueIfAlive(inst, worklist, inWorklist);

    while (!worklist.empty()) {
        Instruction *inst = worklist.back();
        worklist.pop_back();
        inWorklist.erase(inst);

        if (!inst->parent_) continue;
        if (inst->isTerminator()) continue;
        if (rewrittenCount >= rewriteBudget) {
            budgetHit = true;
            break;
        }
        if (++processedCount > processBudget) {
            budgetHit = true;
            break;
        }

        Value *replacement = nullptr;

        switch (inst->op_id_) {
        // AddSub
        case Instruction::Add:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitAdd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Sub:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitSub(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FAdd:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitFAdd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FSub:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitFSub(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FNeg:
            replacement = visitFNeg(static_cast<UnaryInst*>(inst));
            break;
        // MulDivRem
        case Instruction::Mul:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitMul(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::SDiv:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitSDiv(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::SRem:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitSRem(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FMul:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitFMul(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::FDiv:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitFDiv(static_cast<BinaryInst*>(inst));
            break;
        // Shifts
        case Instruction::Shl:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitShl(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::LShr:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitLShr(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::AShr:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitAShr(static_cast<BinaryInst*>(inst));
            break;
        // Bitwise
        case Instruction::And:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitAnd(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Or:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitOr(static_cast<BinaryInst*>(inst));
            break;
        case Instruction::Xor:
            replacement = inst->type_->tid_ == Type::VectorTyID
                              ? visitVectorBinary(static_cast<BinaryInst*>(inst))
                              : visitXor(static_cast<BinaryInst*>(inst));
            break;
        // CmpSelect
        case Instruction::ICmp:
            replacement = visitICmp(static_cast<ICmpInst*>(inst));
            break;
        case Instruction::FCmp:
            replacement = visitFCmp(static_cast<FCmpInst*>(inst));
            break;
        case Instruction::Select:
            replacement = visitSelect(static_cast<SelectInst*>(inst));
            break;
        // Cast / Phi  (constant folding migrated from ConstantFold)
        case Instruction::ZExt:
        case Instruction::SItoFP:
        case Instruction::FPtoSI:
        case Instruction::BitCast:
        case Instruction::Clz:
            replacement = visitCast(inst);
            break;
        case Instruction::PHI:
            replacement = visitPhi(static_cast<PhiInst*>(inst));
            break;
        case Instruction::InsertElement:
            replacement = visitInsertElement(
                static_cast<InsertElementInst *>(inst));
            break;
        case Instruction::ExtractElement:
            replacement = visitExtractElement(
                static_cast<ExtractElementInst *>(inst));
            break;
        case Instruction::ShuffleVector:
            replacement = visitShuffleVector(
                static_cast<ShuffleVectorInst *>(inst));
            break;
        default:
            break;
        }

        auto *replacementInst = dynamic_cast<Instruction *>(replacement);
        if (replacementInst && replacementInst->parent_ &&
            sameInstructionShape(inst, replacementInst)) {
            replacementInst->parent_->delete_instr(replacementInst);
            replacement = nullptr;
            replacementInst = nullptr;
        }

        if (replacement) {
            // 先快照用户和指令操作数，再统一替换 use 并删除旧指令；删除后原
            // use-list 已不可遍历。最后重新入队这些邻居，形成局部定点迭代。
            // Bound every successful rewrite, including replacement by an
            // existing value or a constant.  Counting only newly-created
            // instructions leaves constant-rewrite cycles constrained solely
            // by the much larger worklist budget.
            ++rewrittenCount;
            changed = true;
            std::vector<Instruction*> users;
            for (auto &use : inst->use_list_) {
                if (auto *user_inst = use.user_)
                    users.push_back(user_inst);
            }

            std::vector<Instruction*> operands;
            for (unsigned i = 0; i < inst->num_ops(); i++) {
                if (auto *op = dynamic_cast<Instruction*>(inst->get_operand(i)))
                    operands.push_back(op);
            }

            inst->replace_all_use_with(replacement);
            inst->parent_->delete_instr(inst);
            if (useRangeAnalysis && gInstCombineRangeAnalysis) {
                // 首次替换后谓词操作数和过程间摘要都可能失效。这里一次性清空
                // 范围缓存，本轮剩余工作只做局部化简；后续 InstCombine 轮次
                // 可以重新构建新分析，避免每次替换都付出整函数分析成本。
                AM->clearRangeAnalyses();
                gInstCombineRangeAnalysis = nullptr;
            }

            for (auto *user : users)
                enqueueIfAlive(user, worklist, inWorklist);

            if (replacementInst && replacementInst->parent_) {
                enqueueIfAlive(replacementInst, worklist, inWorklist);
            }

            for (auto *op : operands) {
                trySinkInstruction(op);
                enqueueIfAlive(op, worklist, inWorklist);
            }
        }
    }

    gInstCombineRangeAnalysis = savedRangeAnalysis;
    gInstCombineDominatorTree = savedDominatorTree;

    if (budgetHit) {
        std::cerr << "[InstCombine] budget hit in @" << func->name_
                  << " processed=" << processedCount
                  << " rewritten=" << rewrittenCount
                  << " initial_insts=" << initialInstrCount << "\n";
    }
    return changed;
}
