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

bool debugEnabled() {
    static bool enabled = std::getenv("DEBUG_LOOP_MODULO_DELAY") != nullptr;
    return enabled;
}

bool reject(const Loop &loop, const char *reason) {
    if (debugEnabled())
        std::cerr << "[LoopModuloDelay] reject header=" << loop.header->name_
                  << " reason=" << reason << '\n';
    return false;
}

Value *incomingFor(PhiInst *phi, BasicBlock *predecessor) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

bool rewriteLoop(Loop &loop, Module *module) {
    BasicBlock *header = loop.header;
    BasicBlock *preheader = loop.preheader;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exit = loop.singleExit();
    if (!preheader || !latch || !exit)
        return reject(loop, "non-canonical-cfg");
    // A terminating fixed-step i32 induction visits no state twice, hence it
    // executes at most 2^32-1 iterations.  Each complete contribution is
    // proven to fit signed i32 below, so initial + all contributions fits
    // signed i64 even at the extreme endpoints:
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
        headerBranch->num_ops_ != 3)
        return reject(loop, "bad-header-branch");
    int exitOperand = -1;
    for (unsigned i = 1; i < headerBranch->num_ops_; ++i)
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
        if (!oneBlockChain || !header->parent_->dominates(
                                  remainder->parent_, latch))
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

    // The old state may only participate in its private update chain or be
    // consumed after the unique loop exit.
    for (const auto &use : state->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
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
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) != header) continue;
            if (phi->get_operand(i) == state)
                phi->set_operand(i, result);
            phi->set_operand(i + 1, reduce);
        }
    }
    std::vector<std::pair<Instruction *, unsigned>> externalUses;
    for (const auto &use : state->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && user->parent_ && !loop.blocks.count(user->parent_) &&
            !(user->parent_ == exit && user->is_phi()))
            externalUses.push_back({user, use.arg_no_});
    }
    for (auto [user, operand] : externalUses)
        user->set_operand(operand, result);

    // Remove the old i32 state cycle after all live-outs have been repaired.
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

bool runOnFunction(Function *function, Module *module) {
    bool changed = false;
    for (;;) {
        LoopInfo loops;
        loops.analyze(function);
        std::vector<Loop *> ordered;
        for (const auto &loop : loops.allLoops())
            ordered.push_back(loop.get());
        std::sort(ordered.begin(), ordered.end(),
                  [](Loop *lhs, Loop *rhs) {
                      return lhs->depth > rhs->depth;
                  });
        bool progress = false;
        for (Loop *loop : ordered)
            if (rewriteLoop(*loop, module)) {
                progress = true;
                changed = true;
                function->set_instr_name();
                break;
            }
        if (!progress) break;
    }
    return changed;
}

} // namespace

void LoopModuloDelay::execute(Module *module) {
    AnalysisManager manager;
    execute(module, manager);
}

PreservedAnalyses LoopModuloDelay::execute(Module *module,
                                           AnalysisManager &manager) {
    bool changed = false;
    std::vector<Function *> functions = module->function_list_;
    for (Function *function : functions)
        if (!function->is_declaration())
            changed |= runOnFunction(function, module);
    if (changed)
        manager.clear();
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
