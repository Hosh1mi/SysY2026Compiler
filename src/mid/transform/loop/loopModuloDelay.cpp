/**
 * @file loopModuloDelay.cpp
 * @brief 循环取模延迟：推迟循环递推中的取模操作，在范围与溢出安全时减少每次迭代的高代价余数计算。
 * @details 依据递推范围确定可安全延迟的迭代数，并在退出处恢复规范余数；任何溢出风险都会拒绝变换。
 */

#include "../../../include/mid/opt/loopModuloDelay.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/analysis/moduloRecurrenceAnalysis.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>
#include <vector>

namespace {

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    static bool enabled = std::getenv("DEBUG_LOOP_MODULO_DELAY") != nullptr;
    return enabled;
}

/**
 * @brief 生成 reject 对应的调试诊断，不参与程序语义。
 * @param loop 待检查或变换的循环。
 * @param reason 拒绝变换或匹配失败的原因。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool reject(const Loop &loop, const char *reason) {
    if (debugEnabled())
        std::cerr << "[LoopModuloDelay] reject header=" << loop.header->name_
                  << " reason=" << reason << '\n';
    return false;
}

/**
 * @brief 查询循环状态 PHI 来自指定前驱的入值。
 * @param phi 待查询的 PHI 指令。
 * @param predecessor 指定前驱基本块。
 * @return 找到时返回对应入值，否则返回 nullptr。
 */
Value *incomingFor(PhiInst *phi, BasicBlock *predecessor) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

/**
 * @brief 原地执行 rewriteLoop 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool rewriteLoop(Loop &loop, Module *module,
                 const DominatorTreeAnalysis &DT) {
    // 先证明控制 IV 有界、模递推更新不会发生 i32 溢出，再计算可延迟的贡献范围。
    // 改写把循环内频繁 srem 延后到安全位置，并在退出值处恢复标准有符号余数语义。
    BasicBlock *header = loop.header;
    BasicBlock *preheader = loop.preheader;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !latch || !exit)
        return reject(loop, "non-canonical-cfg");
    // 会终止的定步长 i32 归纳变量不会重复访问同一状态，因此迭代次数至多为
    // 2^32-1。下方还会证明每轮贡献落在有符号 i32 内，于是即使取极端值，
    // “初值 + 全部贡献”也不会溢出有符号 i64：
    //   INT32_MIN * 2^32 == INT64_MIN
    //   INT32_MAX * 2^32 <  INT64_MAX.
    const InductionDescriptor *control = loop.getInductionDescriptor();
    if (!control || !control->constantStep || *control->constantStep == 0 ||
        control->guardPosition != InductionGuardPosition::Header)
        return reject(loop, "unsupported-control-induction");
    if (loop.exiting.size() != 1 || loop.exiting.front() != header)
        return reject(loop, "exit-is-not-header");
    if (exit->pre_bbs_.size() != 1 || exit->pre_bbs_.front() != header)
        return reject(loop, "exit-is-not-dedicated");

    auto *headerBranch = header->get_terminator();
    if (!headerBranch || !headerBranch->is_br() ||
        headerBranch->num_ops() != 3)
        return reject(loop, "bad-header-branch");
    int exitOperand = -1;
    for (unsigned i = 1; i < headerBranch->num_ops(); ++i)
        if (headerBranch->get_operand(i) == exit)
            exitOperand = static_cast<int>(i);
    if (exitOperand < 0)
        return reject(loop, "header-does-not-target-exit");

    std::vector<PhiInst *> loopStates;
    for (auto *instruction : header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi) break;
        loopStates.push_back(phi);
    }

    PhiInst *state = nullptr;
    Value *initial = nullptr;
    ModuloRecurrenceAnalysis::Recurrence recurrence;
    for (PhiInst *candidate : loopStates) {
        if (candidate == control->phi)
            continue;
        Value *backedge = incomingFor(candidate, latch);
        Value *candidateInitial = incomingFor(candidate, preheader);
        auto *remainder = dynamic_cast<BinaryInst *>(backedge);
        ModuloRecurrenceAnalysis::Recurrence analyzed;
        if (!candidateInitial || !remainder ||
            !ModuloRecurrenceAnalysis::analyze(
                candidate, remainder, loop.blocks, analyzed) ||
            !ModuloRecurrenceAnalysis::hasPrivateUpdateChain(
                analyzed, loop.blocks, false))
            continue;
        bool oneBlockChain = true;
        for (Instruction *chain : analyzed.updateChain)
            if (chain->parent_ != remainder->parent_)
                oneBlockChain = false;
        if (!oneBlockChain || !DT.dominates(remainder->parent_, latch))
            continue;
        if (!ModuloRecurrenceAnalysis::proveNoI32UpdateWrap(
                analyzed, loopStates, control->phi, candidateInitial))
            continue;
        if (state)
            return reject(loop, "multiple-candidates");
        state = candidate;
        initial = candidateInitial;
        recurrence = std::move(analyzed);
    }
    if (!state)
        return reject(loop, "no-safe-recurrence");

    // 旧状态在循环内只能用于自身的私有更新链；其他循环内用途可能观察到尚未取模
    // 的中间值，因此不允许。循环外用途则在唯一出口处统一改写。
    for (const auto &use : state->use_list_) {
        auto *user = use.user_;
        if (!user || !user->parent_)
            continue;
        if (loop.blocks.count(user->parent_) &&
            !recurrence.updateChain.count(user))
            return reject(loop, "state-has-extra-loop-use");
    }
    for (auto *instruction : exit->instr_list_) {
        if (!instruction->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instruction);
        Value *incoming = incomingFor(phi, header);
        if (auto *incomingInstruction =
                dynamic_cast<Instruction *>(incoming))
            if (recurrence.updateChain.count(incomingInstruction))
                return reject(loop, "update-chain-escapes");
    }

    Function *function = header->parent_;
    auto *i32 = module->int32_ty_;
    auto *i64 = module->int64_ty_;

    auto *wideInitial = new ZextInst(Instruction::SExt, initial, i64,
                                     preheader, true);
    preheader->add_instruction_before_terminator(wideInitial);

    auto *wideState = PhiInst::create_phi(i64, header);
    header->add_instruction_front(wideState);
    wideState->add_phi_pair_operand(wideInitial, preheader);

    BasicBlock *updateBlock = recurrence.remainder->parent_;
    Value *iterationContribution = new ConstantInt(i32, 0);
    for (const auto &term : recurrence.contributionTerms) {
        auto op = term.sign > 0 ? Instruction::Add : Instruction::Sub;
        auto *next = new BinaryInst(i32, op, iterationContribution,
                                    term.value, updateBlock, true);
        updateBlock->add_instruction_before_inst(next, recurrence.remainder);
        iterationContribution = next;
    }
    auto *wideContribution = new ZextInst(
        Instruction::SExt, iterationContribution, i64, updateBlock, true);
    updateBlock->add_instruction_before_inst(
        wideContribution, recurrence.remainder);
    auto *wideNext = new BinaryInst(
        i64, Instruction::Add, wideState, wideContribution,
        updateBlock, true);
    updateBlock->add_instruction_before_inst(wideNext, recurrence.remainder);
    wideState->add_phi_pair_operand(wideNext, latch);

    auto *reduce = new BasicBlock(
        module, header->name_ + ".moddelay.exit", function);
    auto *modulus64 = new ConstantInt(i64, recurrence.modulus->value_);
    auto *wideRemainder = new BinaryInst(
        i64, Instruction::SRem, wideState, modulus64, reduce);
    auto *narrowRemainder = new ZextInst(
        Instruction::Trunc, wideRemainder, i32, reduce);
    auto *didExecute = new ICmpInst(
        ICmpInst::ICMP_NE, control->phi, control->start, reduce);
    auto *result = new SelectInst(
        didExecute, narrowRemainder, initial, reduce);
    new BranchInst(exit, reduce);

    headerBranch->set_operand(static_cast<unsigned>(exitOperand), reduce);
    header->remove_succ_basic_block(exit);
    header->add_succ_basic_block(reduce);
    reduce->add_pre_basic_block(header);
    exit->remove_pre_basic_block(header);

    for (auto *instruction : exit->instr_list_) {
        if (!instruction->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instruction);
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            if (phi->get_operand(i + 1) != header) continue;
            if (phi->get_operand(i) == state)
                phi->set_operand(i, result);
            phi->set_operand(i + 1, reduce);
        }
    }
    std::vector<std::pair<Instruction *, unsigned>> externalUses;
    for (const auto &use : state->use_list_) {
        auto *user = use.user_;
        if (user && user->parent_ && !loop.blocks.count(user->parent_) &&
            !(user->parent_ == exit && user->is_phi()))
            externalUses.push_back({user, use.operand_index_});
    }
    for (auto [user, operand] : externalUses)
        user->set_operand(operand, result);

    // 必须先修复全部循环活跃输出，再删除旧 i32 状态环，避免暂时留下悬空 use。
    header->delete_instr(state);
    std::vector<Instruction *> chain;
    for (auto *instruction : updateBlock->instr_list_)
        if (recurrence.updateChain.count(instruction))
            chain.push_back(instruction);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        updateBlock->delete_instr(*it);

    if (debugEnabled())
        std::cerr << "[LoopModuloDelay] transform func=" << function->name_
                  << " header=" << header->name_
                  << " modulus=" << recurrence.modulus->value_
                  << " terms=" << recurrence.contributionTerms.size()
                  << '\n';
    return true;
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param function 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param manager 参数 `manager`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool runOnFunction(Function *function, Module *module,
                   AnalysisManager &manager) {
    bool changed = false;
    for (;;) {
        LoopInfo &loops = manager.getLoopInfo(function);
        DominatorTreeAnalysis &DT = manager.getDominatorTree(function);
        std::vector<Loop *> ordered;
        for (const auto &loop : loops.allLoops())
            ordered.push_back(loop.get());
        std::sort(ordered.begin(), ordered.end(),
                  [](Loop *lhs, Loop *rhs) {
                      return lhs->depth > rhs->depth;
                  });
        bool progress = false;
        for (Loop *loop : ordered)
            if (rewriteLoop(*loop, module, DT)) {
                progress = true;
                changed = true;
                function->set_instr_name();
                manager.clear(function);
                break;
            }
        if (!progress) break;
    }
    return changed;
}

} // namespace

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopModuloDelay::execute(Module *module) {
    AnalysisManager manager;
    execute(module, manager);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param manager 参数 `manager`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopModuloDelay::execute(Module *module,
                                           AnalysisManager &manager) {
    bool changed = false;
    std::vector<Function *> functions = module->function_list_;
    for (Function *function : functions)
        if (!function->is_declaration())
            changed |= runOnFunction(function, module, manager);
    if (changed)
        manager.clear();
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
