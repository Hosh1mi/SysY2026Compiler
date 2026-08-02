#include "../../../include/mid/opt/loopRepFold.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/recurrenceAnalysis.hpp"
#include "../../../include/mid/analysis/summableExpressionAnalysis.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <unordered_set>
#include <vector>

namespace {

bool isLoopRepFoldDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_LOOP_REPFOLD") != nullptr;
    return enabled;
}

bool debugReject(const char *reason) {
    if (isLoopRepFoldDebugEnabled())
        std::cerr << "[LoopRepFold] affine reject: " << reason << "\n";
    return false;
}

struct PiecewiseModularSumMatch {
    PhiInst *induction = nullptr;
    PhiInst *state = nullptr;
    Value *start = nullptr;
    Value *bound = nullptr;
    Value *step = nullptr;
    Value *initial = nullptr;
    BinaryInst *stateRemainder = nullptr;
    Value *remainderBase = nullptr;
    int piecewiseEnabled = 0;
    int lhsMultiplier = 1;
    int lhsConstant = 0;
    int rhsMultiplier = 1;
    int rhsConstant = 0;
    int trueUsesRight = 1;
    int linearMultiplier = 0;
    int multiplier = 0;
    int divisor = 0;
    int quotientMultiplier = 0;
    int contributionConstant = 0;
    int innerModulus = 0;
    int additiveConstant = 0;
    int outerModulus = 0;
};

bool constantI32(Value *value, int &result) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    if (!constant || constant->value_ < std::numeric_limits<int>::min() ||
        constant->value_ > std::numeric_limits<int>::max())
        return false;
    result = static_cast<int>(constant->value_);
    return true;
}

bool flattenAdd(Value *value, std::vector<Value *> &terms,
                long long &constant) {
    if (auto *integer = dynamic_cast<ConstantInt *>(value)) {
        constant += integer->value_;
        return constant >= std::numeric_limits<int>::min() &&
               constant <= std::numeric_limits<int>::max();
    }
    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (binary && binary->is_add())
        return flattenAdd(binary->get_operand(0), terms, constant) &&
               flattenAdd(binary->get_operand(1), terms, constant);
    terms.push_back(value);
    return true;
}

bool matchAddConstant(Value *value, Value *base, int constant) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add) return false;
    int candidate = 0;
    if (add->is_add())
        return (add->get_operand(0) == base &&
                constantI32(add->get_operand(1), candidate) &&
                candidate == constant) ||
               (add->get_operand(1) == base &&
                constantI32(add->get_operand(0), candidate) &&
                candidate == constant);
    return add->is_sub() && add->get_operand(0) == base &&
           constantI32(add->get_operand(1), candidate) &&
           -static_cast<std::int64_t>(candidate) == constant;
}

bool matchCompareConstant(Value *value, ICmpInst::ICmpOp predicate,
                          Value *lhs, int rhs) {
    auto *compare = dynamic_cast<ICmpInst *>(value);
    int constant = 0;
    return compare && compare->icmp_op_ == predicate &&
           compare->get_operand(0) == lhs &&
           constantI32(compare->get_operand(1), constant) && constant == rhs;
}

// Recognize the exact one-step signed modulo lowering emitted by
// buildBoundedModulo for (base + additive) % modulus.  All intermediate nodes
// must be private to the lowering, so replacing base by a congruent fast-path
// representative cannot change any independently observable value.
bool matchExitModuloReconstruction(
    Value *base, int additive, int modulus, BasicBlock *exit,
    std::unordered_set<Instruction *> &chain) {
    auto reject = [&](const char *reason) {
        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] exit modulo reject reason="
                      << reason << "\n";
        return false;
    };
    std::int64_t highThresholdWide =
        static_cast<std::int64_t>(modulus) - additive;
    std::int64_t reducedAddWide =
        static_cast<std::int64_t>(additive) - modulus;
    if (highThresholdWide < std::numeric_limits<int>::min() ||
        highThresholdWide > std::numeric_limits<int>::max() ||
        reducedAddWide < std::numeric_limits<int>::min() ||
        reducedAddWide > std::numeric_limits<int>::max())
        return reject("constant-overflow");
    int highThreshold = static_cast<int>(highThresholdWide);
    int reducedAdd = static_cast<int>(reducedAddWide);

    Value *exitBase = base;
    for (Instruction *instruction : exit->instr_list_) {
        if (!instruction->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instruction);
        if (phi->num_ops_ == 2 && phi->get_operand(0) == base) {
            exitBase = phi;
            break;
        }
    }
    Value *dividend = additive == 0 ? exitBase : nullptr;
    Instruction *highCompare = nullptr;
    Instruction *highReduced = nullptr;
    SelectInst *highSelect = nullptr;
    Instruction *lowCompare = nullptr;
    Instruction *lowAdjusted = nullptr;
    SelectInst *lowSelect = nullptr;

    for (auto *instruction : exit->instr_list_) {
        if (!dividend && matchAddConstant(instruction, exitBase, additive))
            dividend = instruction;
        if (!highCompare &&
            (matchCompareConstant(instruction, ICmpInst::ICMP_SGE, exitBase,
                                  highThreshold)))
            highCompare = instruction;
        if (!highReduced &&
            matchAddConstant(instruction, exitBase, reducedAdd))
            highReduced = instruction;
    }
    if (!dividend) return reject("missing-dividend");

    for (auto *instruction : exit->instr_list_) {
        auto *remainder = dynamic_cast<BinaryInst *>(instruction);
        int candidateModulus = 0;
        if (!remainder || !remainder->is_rem() ||
            remainder->get_operand(0) != dividend ||
            !constantI32(remainder->get_operand(1), candidateModulus) ||
            candidateModulus != modulus)
            continue;
        chain.clear();
        if (auto *instruction = dynamic_cast<Instruction *>(dividend);
            instruction && instruction->parent_ == exit)
            chain.insert(instruction);
        chain.insert(remainder);
        for (const auto &use : dividend->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ == exit && user != remainder)
                return reject("exact-dividend-extra-use");
        }
        for (const auto &use : exitBase->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ == exit && user != dividend)
                return reject("exact-base-extra-use");
        }
        return true;
    }

    if (!highCompare || !highReduced)
        return reject("missing-high-correction");

    for (auto *instruction : exit->instr_list_) {
        auto *select = dynamic_cast<SelectInst *>(instruction);
        if (select && select->get_operand(0) == highCompare &&
            select->get_operand(1) == highReduced &&
            select->get_operand(2) == dividend) {
            highSelect = select;
            break;
        }
    }
    if (!highSelect) return reject("missing-high-select");

    for (auto *instruction : exit->instr_list_) {
        if (!lowCompare &&
            matchCompareConstant(instruction, ICmpInst::ICMP_SLE, highSelect,
                                 -modulus))
            lowCompare = instruction;
        if (!lowAdjusted && matchAddConstant(instruction, highSelect, modulus))
            lowAdjusted = instruction;
    }
    if (!lowCompare || !lowAdjusted)
        return reject("missing-low-correction");
    for (auto *instruction : exit->instr_list_) {
        auto *select = dynamic_cast<SelectInst *>(instruction);
        if (select && select->get_operand(0) == lowCompare &&
            select->get_operand(1) == lowAdjusted &&
            select->get_operand(2) == highSelect) {
            lowSelect = select;
            break;
        }
    }
    if (!lowSelect) return reject("missing-low-select");

    chain.clear();
    if (auto *instruction = dynamic_cast<Instruction *>(dividend);
        instruction && instruction->parent_ == exit)
        chain.insert(instruction);
    for (Instruction *instruction : std::vector<Instruction *>{
             highCompare, highReduced, highSelect,
             lowCompare, lowAdjusted, lowSelect})
        chain.insert(instruction);
    for (Instruction *instruction : chain) {
        if (instruction == lowSelect) continue;
        for (const auto &use : instruction->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !chain.count(user))
                return reject("correction-chain-extra-use");
        }
    }
    for (const auto &use : exitBase->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && user->parent_ == exit && !chain.count(user))
            return reject("base-extra-use");
    }
    return true;
}

bool matchPiecewiseContribution(Value *value, PhiInst *induction,
                                PiecewiseModularSumMatch &match) {
    SummableExpressionAnalysis::LinearFloorExpression expression;
    if (!SummableExpressionAnalysis::analyzeModular(
            value, induction, expression))
        return false;
    match.piecewiseEnabled = expression.piecewise ? 1 : 0;
    match.lhsMultiplier = expression.lhsMultiplier;
    match.lhsConstant = expression.lhsConstant;
    match.rhsMultiplier = expression.rhsMultiplier;
    match.rhsConstant = expression.rhsConstant;
    match.trueUsesRight = expression.trueUsesRight ? 1 : 0;
    match.linearMultiplier = expression.linearMultiplier;
    match.multiplier = expression.divisionMultiplier;
    match.divisor = expression.divisor;
    match.quotientMultiplier = expression.quotientMultiplier;
    match.contributionConstant = expression.constant;
    match.innerModulus = expression.modulus;
    return true;
}

bool analyzePiecewiseModularSum(Loop &loop,
                                PiecewiseModularSumMatch &match) {
    auto reject = [&](const char *reason) {
        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] piecewise reject header="
                      << loop.header->name_ << " reason=" << reason << "\n";
        return false;
    };
    const InductionDescriptor *control = loop.getInductionDescriptor();
    if (!control || control->stepNegated ||
        control->predicate != ICmpInst::ICMP_SLT ||
        control->guardPosition != InductionGuardPosition::Latch ||
        !control->comparesUpdate || loop.blocks.size() != 1 ||
        loop.singleLatch() != loop.header)
        return reject("control-induction");
    match.induction = control->phi;
    match.start = control->start;
    match.bound = control->bound;
    match.step = control->step;

    std::vector<PhiInst *> phis;
    for (auto *instruction : loop.header->instr_list_) {
        if (!instruction->is_phi()) break;
        phis.push_back(static_cast<PhiInst *>(instruction));
    }
    if (phis.size() != 2) return reject("phi-count");
    match.state = phis[0] == match.induction ? phis[1] : phis[0];
    if (match.state == match.induction ||
        match.state->type_->tid_ != Type::IntegerTyID)
        return reject("state-type");
    Value *back = nullptr;
    for (unsigned index = 0; index < match.state->num_ops_; index += 2) {
        auto *block = static_cast<BasicBlock *>(
            match.state->get_operand(index + 1));
        if (block == loop.preheader)
            match.initial = match.state->get_operand(index);
        else if (block == loop.header)
            back = match.state->get_operand(index);
    }
    match.stateRemainder = dynamic_cast<BinaryInst *>(back);
    if (!match.initial || !match.stateRemainder ||
        !match.stateRemainder->is_rem() ||
        !constantI32(match.stateRemainder->get_operand(1),
                     match.outerModulus) ||
        match.outerModulus <= match.innerModulus ||
        match.outerModulus >= std::numeric_limits<int>::max())
        return reject("outer-remainder");

    std::vector<Value *> terms;
    long long additive = 0;
    if (!flattenAdd(match.stateRemainder->get_operand(0), terms, additive) ||
        terms.size() != 2)
        return reject("outer-add-tree");
    Value *contribution = nullptr;
    if (terms[0] == match.state)
        contribution = terms[1];
    else if (terms[1] == match.state)
        contribution = terms[0];
    else
        return reject("state-not-in-add-tree");
    match.additiveConstant = static_cast<int>(additive);
    auto *dividendAdd = dynamic_cast<BinaryInst *>(
        match.stateRemainder->get_operand(0));
    if (match.additiveConstant == 0) {
        match.remainderBase = match.stateRemainder->get_operand(0);
    } else if (dividendAdd && dividendAdd->is_add()) {
        int directConstant = 0;
        if (constantI32(dividendAdd->get_operand(0), directConstant) &&
            directConstant == match.additiveConstant)
            match.remainderBase = dividendAdd->get_operand(1);
        else if (constantI32(dividendAdd->get_operand(1), directConstant) &&
                 directConstant == match.additiveConstant)
            match.remainderBase = dividendAdd->get_operand(0);
    }
    if (!matchPiecewiseContribution(contribution, match.induction, match))
        return reject("contribution-shape");
    // The helper combines contributions mathematically modulo outerModulus.
    // Prove that the original i32 add tree cannot wrap once the state is in
    // the signed remainder range; otherwise wrapping by 2^32 would change the
    // residue for a general modulus.
    std::int64_t minimumDividend =
        -static_cast<std::int64_t>(match.outerModulus - 1) -
        static_cast<std::int64_t>(match.innerModulus - 1) +
        match.additiveConstant;
    std::int64_t maximumDividend =
        static_cast<std::int64_t>(match.outerModulus - 1) +
        static_cast<std::int64_t>(match.innerModulus - 1) +
        match.additiveConstant;
    if (minimumDividend < std::numeric_limits<int>::min() ||
        maximumDividend > std::numeric_limits<int>::max())
        return reject("state-update-may-wrap");
    return true;
}

} // namespace

// ── 辅助检查 ────────────────────────────────────────────────────────────────

bool LoopRepFold::isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks) {
    if (dynamic_cast<Constant *>(val)) return true;
    if (dynamic_cast<GlobalVariable *>(val)) return true;
    if (dynamic_cast<Argument *>(val)) return true;
    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst) return true;
    return !blocks.count(inst->parent_);
}

// 判断 phi 是否为：常量初始值（来自 preheader），每次 latch 时 += 常量正步长
bool LoopRepFold::isCountingIV(PhiInst *phi, const Loop &loop, BasicBlock *latch,
                               long long *init, long long *stride) {
    if (phi->type_->tid_ != Type::IntegerTyID) return false;
    if (phi->num_ops_ != 4) return false; // 恰好 2 对 (val, BB)

    Value *pre_val = nullptr, *latch_val = nullptr;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto *bb = static_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (bb == loop.preheader) pre_val  = phi->get_operand(i);
        else if (bb == latch) latch_val = phi->get_operand(i);
    }
    if (!pre_val || !latch_val) return false;

    auto *ci_init = dynamic_cast<ConstantInt *>(pre_val);
    if (!ci_init) return false;

    // latch_val 必须是 phi + 常量正步长
    auto *add = dynamic_cast<BinaryInst *>(latch_val);
    if (!add || !add->is_add()) return false;
    auto *op0 = add->get_operand(0);
    auto *op1 = add->get_operand(1);
    auto *ci0 = dynamic_cast<ConstantInt *>(op0);
    auto *ci1 = dynamic_cast<ConstantInt *>(op1);
    long long step = 0;
    if (op0 == phi && ci1) {
        step = ci1->value_;
    } else if (op1 == phi && ci0) {
        step = ci0->value_;
    } else {
        return false;
    }
    if (step <= 0) return false;
    if (init) *init = ci_init->value_;
    if (stride) *stride = step;
    return true;
}

// 仿射求和闭式折叠：total += a*i+b（界/初值/步长全常量）→ 直接算出常量结果，
// 把出口处对 total_phi 的使用替换为该常量并整体删除循环。
bool LoopRepFold::tryFoldAffineSum(Loop &loop, Module *module, ScalarEvolution *SE,
                                   BasicBlock *latch, PhiInst *ivPhi,
                                   PhiInst *totalPhi, BasicBlock *loopExit,
                                   Value *bound, Value *totalInit,
                                   Value *totalLatch, long long ivInit,
                                   long long ivStride) {
    if (!SE) return debugReject("missing scalar evolution");
    if (loop.singleLatch() != latch) return debugReject("single latch mismatch");
    if (loop.singleExit() != loopExit) return debugReject("single exit mismatch");

    auto *ivAddRec = dynamic_cast<const SCEVAddRecExpr *>(SE->getSCEV(ivPhi));
    if (!ivAddRec || ivAddRec->loop() != &loop || ivAddRec->phi() != ivPhi)
        return debugReject("iv is not matching addrec");
    long long scevInit = 0;
    long long scevStride = 0;
    RecurrenceAnalysis RA(*SE);
    if (!RecurrenceAnalysis::scevConstant(ivAddRec->start(), scevInit) ||
        !RecurrenceAnalysis::scevConstant(ivAddRec->step(), scevStride))
        return debugReject("iv addrec is not constant");
    if (scevInit != ivInit || scevStride != ivStride)
        return debugReject("local iv and scev mismatch");
    if (ivStride <= 0) return debugReject("iv stride is not positive");

    auto *boundCI = dynamic_cast<ConstantInt *>(bound);
    if (!boundCI) return debugReject("non-constant bound");
    long long diff = 0;
    if (!RecurrenceAnalysis::checkedSub(boundCI->value_, ivInit, diff))
        return debugReject("bound/init overflow");
    long long iterations = 0;
    if (diff > 0) {
        long long adjusted = 0;
        if (!RecurrenceAnalysis::checkedAdd(diff, ivStride - 1, adjusted))
            return debugReject("trip ceil overflow");
        iterations = adjusted / ivStride;
    }

    auto *initCI = dynamic_cast<ConstantInt *>(totalInit);
    if (!initCI) return debugReject("non-constant total init");

    auto *update = dynamic_cast<Instruction *>(totalLatch);
    if (!update || !loop.blocks.count(update->parent_))
        return debugReject("total update is not in loop");

    std::set<Instruction *> accumulatorChain;
    AccumulatorRecurrenceStep accumulator =
        RA.analyzeAccumulatorStep(totalLatch, totalPhi, ivPhi, &loop,
                                  loop.blocks, accumulatorChain);
    if (!accumulator.valid || accumulator.totalRefs != 1)
        return debugReject("cannot identify accumulator step");

    for (auto &use : totalPhi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !loop.blocks.count(user->parent_)) continue;
        if (!accumulatorChain.count(user))
            return debugReject("total phi has extra in-loop use");
    }

    for (auto *inst : loopExit->instr_list_) {
        if (!inst->is_phi()) break;
        return debugReject("exit has phi");
    }

    auto *preheaderBr = loop.preheader->get_terminator();
    if (!preheaderBr || !preheaderBr->is_br()) return debugReject("bad preheader terminator");
    int headerOperand = -1;
    for (unsigned i = 0; i < preheaderBr->num_ops_; i++) {
        if (preheaderBr->get_operand(i) == loop.header) {
            headerOperand = static_cast<int>(i);
            break;
        }
    }
    if (headerOperand < 0) return debugReject("preheader does not branch to header");

    long long result = 0;
    if (!RA.computeAffineSumClosedForm(initCI->value_, accumulator.step,
                                       iterations, result))
        return debugReject("closed form overflow or out of i32 range");

    auto *folded = new ConstantInt(module->int32_ty_, static_cast<int>(result));
    std::vector<std::pair<Instruction *, unsigned>> exitUses;
    for (auto &use : totalPhi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (user && user->parent_ == loopExit)
            exitUses.push_back({user, use.arg_no_});
    }
    if (exitUses.empty()) return debugReject("total phi has no exit use");
    for (auto &[user, argNo] : exitUses)
        user->set_operand(argNo, folded);

    preheaderBr->set_operand(static_cast<unsigned>(headerOperand), loopExit);

    loop.preheader->remove_succ_basic_block(loop.header);
    loop.preheader->add_succ_basic_block(loopExit);
    loop.header->remove_pre_basic_block(loop.preheader);
    loopExit->add_pre_basic_block(loop.preheader);

    Function *func = loop.header->parent_;
    // 3.2 契约：影响 IR 的块遍历必须走 blocksOrdered（确定性顺序）
    std::vector<BasicBlock *> deadBlocks(loop.blocksOrdered.begin(),
                                         loop.blocksOrdered.end());
    for (auto *bb : deadBlocks)
        func->remove_bb(bb);
    return true;
}

// ── 模仿射递推折叠 ───────────────────────────────────────────────────────────
// 识别 while (i < N) { total = (total + c) % m; i++ }（i:0/+1，c>0,m>0 常量）。
// 保留原循环作慢路径，在 preheader 前插入守卫：
//   total0 >= 0 && N >= 1 && N <= (INT_MAX - m)/c
// 守卫成立时走快路径 total_final = (total0 % m + c*N) % m（全 i32 不溢出，且
// 被除数恒非负 ⇒ C 截断取模 == 数学取模 ⇒ 与逐次迭代严格相等）；否则走原循环。
bool LoopRepFold::tryFoldModularRecurrence(Loop &loop, Module *module,
                                           BasicBlock *latch, PhiInst *ivPhi,
                                           PhiInst *totalPhi, BasicBlock *loopExit,
                                           Value *bound, Value *totalInit,
                                           Value *totalLatch, long long ivInit,
                                           long long ivStride) {
    (void)latch;
    (void)ivPhi;
    auto reject = [&](const char *reason) {
        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] modular reject header="
                      << loop.header->name_ << " reason=" << reason << "\n";
        return false;
    };
    // 仅规范 0/+1 IV：此时 trip count 恰为 N（当 N>=1）。
    if (ivInit != 0 || ivStride != 1) return reject("non-canonical-iv");
    if (modFolded_.count(loop.header)) return reject("already-folded");

    // total_latch 必须恰为 srem(add(total_phi, c), m)，c>0, m>0 常量。
    auto *rem = dynamic_cast<BinaryInst *>(totalLatch);
    if (!rem || !rem->is_rem()) return reject("latch-value-is-not-rem");
    if (!loop.blocks.count(rem->parent_)) return reject("rem-outside-loop");
    auto *mCI = dynamic_cast<ConstantInt *>(rem->get_operand(1));
    if (!mCI || mCI->value_ <= 0) return reject("invalid-modulus");
    long long m = mCI->value_;

    auto *add = dynamic_cast<BinaryInst *>(rem->get_operand(0));
    if (!add || !add->is_add() || !loop.blocks.count(add->parent_))
        return reject("rem-input-is-not-loop-add");
    Value *addL = add->get_operand(0), *addR = add->get_operand(1);
    ConstantInt *cCI = nullptr;
    if (addL == totalPhi) cCI = dynamic_cast<ConstantInt *>(addR);
    else if (addR == totalPhi) cCI = dynamic_cast<ConstantInt *>(addL);
    if (!cCI || cCI->value_ <= 0) return reject("invalid-additive-step");
    long long c = cCI->value_;

    // total_phi 在循环内只能被那个 add 使用 → 递推确为 (total+c)%m。
    for (auto &use : totalPhi->use_list_) {
        auto *u = dynamic_cast<Instruction *>(use.val_);
        if (!u || !u->parent_ || !loop.blocks.count(u->parent_)) continue;
        if (u != add) return reject("state-has-extra-loop-use");
    }

    // 除 total_phi 外，循环内任何值（含 IV）不得被循环外使用：折叠后快路径
    // 不执行循环，这些值在外部将变为 undefined。
    for (auto *bb : loop.blocks)
        for (auto *inst : bb->instr_list_) {
            if (inst == totalPhi) continue;
            for (auto &use : inst->use_list_) {
                auto *u = dynamic_cast<Instruction *>(use.val_);
                if (u && u->parent_ && !loop.blocks.count(u->parent_))
                    return reject("loop-value-escapes");
            }
        }

    // 出口必须是循环专属（唯一前驱为 header）。LCSSA 可以在此放置
    // phi；先验证每个 phi 的 fast 边入值都可以精确构造，再修改 CFG。
    if (loopExit->pre_bbs_.size() != 1 || loopExit->pre_bbs_[0] != loop.header)
        return reject("exit-is-not-dedicated");

    enum class ExitIncomingKind { State, Induction, Invariant };
    struct ExitPhiPlan {
        PhiInst *phi;
        Value *headerIncoming;
        ExitIncomingKind kind;
    };
    std::vector<ExitPhiPlan> exitPhiPlans;
    PhiInst *stateExitPhi = nullptr;
    for (auto *inst : loopExit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        Value *incoming = nullptr;
        int headerIncomingCount = 0;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) != loop.header)
                return reject("exit-phi-has-non-header-incoming");
            incoming = phi->get_operand(i);
            headerIncomingCount++;
        }
        if (headerIncomingCount != 1)
            return reject("exit-phi-header-incoming-is-not-unique");

        ExitIncomingKind kind;
        if (incoming == totalPhi) {
            kind = ExitIncomingKind::State;
            if (!stateExitPhi) stateExitPhi = phi;
        } else if (incoming == ivPhi) {
            kind = ExitIncomingKind::Induction;
        } else if (isLoopInvariant(incoming, loop.blocks)) {
            kind = ExitIncomingKind::Invariant;
        } else {
            return reject("unsupported-loop-value-in-exit-phi");
        }
        exitPhiPlans.push_back({phi, incoming, kind});
    }

    // preheader 必须以无条件 br 跳向 header。
    BasicBlock *PH = loop.preheader;
    auto *phTerm = PH->get_terminator();
    if (!phTerm || !phTerm->is_br() || phTerm->num_ops_ != 1)
        return reject("invalid-preheader-terminator");
    if (phTerm->get_operand(0) != loop.header)
        return reject("preheader-does-not-target-header");

    // 防溢出上界：c*N <= INT_MAX - m  →  N <= (INT_MAX - m)/c。
    long long BOUND = ((long long)std::numeric_limits<int>::max() - m) / c;
    if (BOUND < 1) return reject("empty-safe-bound");

    Function *func = loop.header->parent_;
    auto *i32 = module->int32_ty_;
    auto *i1 = module->int1_ty_;

    // ── 1. preheader 守卫：(total0>=0) && (N>=1) && (N<=BOUND) ──
    auto *zero = new ConstantInt(i32, 0);
    auto *one = new ConstantInt(i32, 1);
    auto *boundC = new ConstantInt(i32, (int)BOUND);
    auto *condA = new ICmpInst(ICmpInst::ICMP_SGE, totalInit, zero, PH, true);
    auto *condN1 = new ICmpInst(ICmpInst::ICMP_SGE, bound, one, PH, true);
    auto *condN2 = new ICmpInst(ICmpInst::ICMP_SLE, bound, boundC, PH, true);
    auto *and1 = new BinaryInst(i1, Instruction::And, condA, condN1, PH, true);
    auto *cond = new BinaryInst(i1, Instruction::And, and1, condN2, PH, true);
    PH->add_instruction_before_terminator(condA);
    PH->add_instruction_before_terminator(condN1);
    PH->add_instruction_before_terminator(condN2);
    PH->add_instruction_before_terminator(and1);
    PH->add_instruction_before_terminator(cond);

    // ── 2. fast 块：total_final = (total0 % m + c*N) % m ──
    auto *fast = new BasicBlock(module, loop.header->name_ + ".modfold.fast", func);
    auto *cM = new ConstantInt(i32, (int)m);
    auto *cC = new ConstantInt(i32, (int)c);
    auto *a0 = new BinaryInst(i32, Instruction::SRem, totalInit, cM, fast, true);
    auto *cn = new BinaryInst(i32, Instruction::Mul, cC, bound, fast, true);
    auto *sum = new BinaryInst(i32, Instruction::Add, a0, cn, fast, true);
    auto *tot = new BinaryInst(i32, Instruction::SRem, sum, cM, fast, true);
    fast->add_instruction(a0);
    fast->add_instruction(cn);
    fast->add_instruction(sum);
    fast->add_instruction(tot);
    new BranchInst(loopExit, fast); // fast → loopExit（自动追加为终止指令）

    // ── 3. 改写 preheader 终止指令：br cond, fast, header ──
    PH->delete_instr(phTerm);
    new BranchInst(cond, fast, loop.header, PH);
    PH->add_succ_basic_block(fast);
    fast->add_pre_basic_block(PH);
    fast->add_succ_basic_block(loopExit);
    loopExit->add_pre_basic_block(fast);

    // ── 4. 补齐 LCSSA phi 的 fast 入边。若已有状态 LCSSA phi 则直接复用，
    //        否则创建出口合并 phi。 ──
    for (const auto &plan : exitPhiPlans) {
        Value *fastIncoming = nullptr;
        switch (plan.kind) {
        case ExitIncomingKind::State:
            fastIncoming = tot;
            break;
        case ExitIncomingKind::Induction:
            fastIncoming = bound;
            break;
        case ExitIncomingKind::Invariant:
            fastIncoming = plan.headerIncoming;
            break;
        }
        plan.phi->add_phi_pair_operand(fastIncoming, fast);
    }

    PhiInst *exitPhi = stateExitPhi;
    if (!exitPhi) {
        std::vector<Value *> vals = {totalPhi, tot};
        std::vector<BasicBlock *> bbs = {loop.header, fast};
        exitPhi = new PhiInst(Instruction::PHI, vals, bbs, i32, loopExit);
        loopExit->add_instruction_front(exitPhi);
    }

    std::vector<std::pair<Instruction *, unsigned>> toReplace;
    for (auto &use : totalPhi->use_list_) {
        auto *u = dynamic_cast<Instruction *>(use.val_);
        if (u && u != exitPhi && u->parent_ && !loop.blocks.count(u->parent_) &&
            !(u->parent_ == loopExit && u->is_phi()))
            toReplace.push_back({u, use.arg_no_});
    }
    for (auto &[u, argNo] : toReplace) u->set_operand(argNo, exitPhi);

    modFolded_.insert(loop.header);
    if (std::getenv("DEBUG_LOOP_REPFOLD"))
        std::cerr << "[LoopRepFold] modular fold func=" << func->name_
                  << " header=" << loop.header->name_ << " c=" << c
                  << " m=" << m << "\n";
    return true;
}

Function *LoopRepFold::getPiecewiseModSumDeclaration(Module *module) {
    if (piecewiseModSumDecl_)
        return piecewiseModSumDecl_;
    for (auto *function : module->function_list_)
        if (function->name_ == "__compiler.piecewise_mod_sum") {
            piecewiseModSumDecl_ = function;
            return function;
        }
    std::vector<Type *> arguments(18, module->int32_ty_);
    auto *type = new FunctionType(module->int32_ty_, arguments);
    piecewiseModSumDecl_ =
        new Function(type, "__compiler.piecewise_mod_sum", module);
    return piecewiseModSumDecl_;
}

bool LoopRepFold::tryFoldPiecewiseModularSum(Loop &loop, Module *module) {
    auto reject = [&](const char *reason) {
        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] piecewise transform reject header="
                      << loop.header->name_ << " reason=" << reason << "\n";
        return false;
    };
    PiecewiseModularSumMatch match;
    if (!analyzePiecewiseModularSum(loop, match)) return false;
    if (modFolded_.count(loop.header)) return reject("already-folded");
    if (!loop.preheader) return reject("no-preheader");
    if (loop.exits.size() != 1 || loop.exiting.size() != 1 ||
        loop.exiting.front() != loop.header)
        return reject("non-unique-exit");
    BasicBlock *exit = loop.singleExit();
    if (!exit || exit->pre_bbs_.size() != 1 ||
        exit->pre_bbs_.front() != loop.header)
        return reject("non-dedicated-exit");

    for (auto *instruction : loop.header->instr_list_)
        if (instruction->is_store() || instruction->is_call())
            return reject("side-effecting-instruction");

    std::unordered_set<Instruction *> updateExpression;
    std::function<void(Value *)> collectExpression = [&](Value *value) {
        auto *instruction = dynamic_cast<Instruction *>(value);
        if (!instruction || !loop.blocks.count(instruction->parent_) ||
            !updateExpression.insert(instruction).second)
            return;
        for (unsigned index = 0; index < instruction->num_ops_; ++index)
            collectExpression(instruction->get_operand(index));
    };
    collectExpression(match.stateRemainder);
    for (const auto &use : match.state->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !user->parent_) continue;
        if (loop.blocks.count(user->parent_)) {
            if (!updateExpression.count(user))
                return reject("state-extra-use");
        } else {
            return reject("state-escapes");
        }
    }

    std::unordered_set<Instruction *> remainderExitChain;
    bool remainderBaseEscapes = false;
    if (match.remainderBase) {
        for (const auto &use : match.remainderBase->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ && !loop.blocks.count(user->parent_)) {
                remainderBaseEscapes = true;
                break;
            }
        }
    }
    if (remainderBaseEscapes &&
        !matchExitModuloReconstruction(
            match.remainderBase, match.additiveConstant,
            match.outerModulus, exit, remainderExitChain))
        return reject("unsupported-remainder-export");

    // The fast path exports only the accumulator.  Any other loop-defined
    // value escaping would require reconstructing its exact final iteration.
    for (auto *instruction : loop.header->instr_list_) {
        if (instruction == match.state) continue;
        for (const auto &use : instruction->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ &&
                !loop.blocks.count(user->parent_)) {
                bool exportedRemainderBase =
                    instruction == match.remainderBase &&
                    user->parent_ == exit && remainderExitChain.count(user);
                if (exportedRemainderBase)
                    continue;
                if (isLoopRepFoldDebugEnabled())
                    std::cerr << "[LoopRepFold] escaping value="
                              << instruction->name_ << " user=" << user->name_
                              << " user-block=" << user->parent_->name_ << "\n";
                return reject("loop-value-escapes");
            }
        }
    }

    enum class ExitValueKind { RemainderBase, Invariant };
    struct ExitPhiPlan {
        PhiInst *phi;
        Value *incoming;
        ExitValueKind kind;
    };
    std::vector<ExitPhiPlan> exitPlans;
    PhiInst *remainderBaseExitPhi = nullptr;
    for (auto *instruction : exit->instr_list_) {
        if (!instruction->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(instruction);
        if (phi->num_ops_ != 2 || phi->get_operand(1) != loop.header)
            return reject("unsupported-exit-phi");
        Value *incoming = phi->get_operand(0);
        if (incoming == match.remainderBase) {
            remainderBaseExitPhi = phi;
            exitPlans.push_back(
                {phi, incoming, ExitValueKind::RemainderBase});
        } else if (isLoopInvariant(incoming, loop.blocks)) {
            exitPlans.push_back({phi, incoming, ExitValueKind::Invariant});
        } else {
            return reject("unsupported-exit-value");
        }
    }

    BasicBlock *preheader = loop.preheader;
    auto *oldBranch = preheader->get_terminator();
    if (!oldBranch || !oldBranch->is_br() || oldBranch->num_ops_ != 1 ||
        oldBranch->get_operand(0) != loop.header)
        return reject("invalid-preheader-branch");

    auto *i32 = module->int32_ty_;
    auto *i1 = module->int1_ty_;
    auto *zero = new ConstantInt(i32, 0);
    auto *strideLimit = new ConstantInt(i32, 65535);
    auto *intMax = new ConstantInt(i32, std::numeric_limits<int>::max());
    auto *one = new ConstantInt(i32, 1);
    auto *stepPositive = new ICmpInst(ICmpInst::ICMP_SGT, match.step, zero,
                                      preheader, true);
    auto *stepSmall = new ICmpInst(ICmpInst::ICMP_SLE, match.step,
                                   strideLimit, preheader, true);
    auto *startNonnegative = new ICmpInst(ICmpInst::ICMP_SGE, match.start,
                                          zero, preheader, true);
    auto *startBeforeBound = new ICmpInst(ICmpInst::ICMP_SLT, match.start,
                                          match.bound, preheader, true);
    auto *minimumState = new ConstantInt(i32, -match.outerModulus + 1);
    auto *maximumState = new ConstantInt(i32, match.outerModulus - 1);
    auto *initialInLowerRange = new ICmpInst(
        ICmpInst::ICMP_SGE, match.initial, minimumState, preheader, true);
    auto *initialInUpperRange = new ICmpInst(
        ICmpInst::ICMP_SLE, match.initial, maximumState, preheader, true);
    auto *maxMinusStep = new BinaryInst(i32, Instruction::Sub, intMax,
                                        match.step, preheader, true);
    auto *safeBound = new BinaryInst(i32, Instruction::Add, maxMinusStep,
                                     one, preheader, true);
    auto *boundSafe = new ICmpInst(ICmpInst::ICMP_SLE, match.bound,
                                   safeBound, preheader, true);
    std::vector<Value *> affineSafetyConditions;
    std::vector<Instruction *> affineSafetyInstructions;
    {
        auto *i64 = module->int64_ty_;
        auto remember = [&](Instruction *instruction) -> Instruction * {
            affineSafetyInstructions.push_back(instruction);
            return instruction;
        };
        auto *start64 = static_cast<Value *>(remember(new ZextInst(
            Instruction::SExt, match.start, i64, preheader, true)));
        auto *bound64 = static_cast<Value *>(remember(new ZextInst(
            Instruction::SExt, match.bound, i64, preheader, true)));
        auto *last64 = static_cast<Value *>(remember(new BinaryInst(
            i64, Instruction::Sub, bound64, new ConstantInt(i64, 1),
            preheader, true)));
        auto addLineSafety = [&](int multiplier, int constant) {
            auto evaluate = [&](Value *x) -> Value * {
                auto *product = static_cast<Value *>(remember(new BinaryInst(
                    i64, Instruction::Mul, x,
                    new ConstantInt(i64, multiplier), preheader, true)));
                return static_cast<Value *>(remember(new BinaryInst(
                    i64, Instruction::Add, product,
                    new ConstantInt(i64, constant), preheader, true)));
            };
            for (Value *value : {evaluate(start64), evaluate(last64)}) {
                auto *lower = static_cast<Value *>(remember(new ICmpInst(
                    ICmpInst::ICMP_SGE, value,
                    new ConstantInt(i64, std::numeric_limits<int>::min()),
                    preheader, true)));
                auto *upper = static_cast<Value *>(remember(new ICmpInst(
                    ICmpInst::ICMP_SLE, value,
                    new ConstantInt(i64, std::numeric_limits<int>::max()),
                    preheader, true)));
                affineSafetyConditions.push_back(lower);
                affineSafetyConditions.push_back(upper);
            }
        };
        addLineSafety(match.lhsMultiplier, match.lhsConstant);
        addLineSafety(match.rhsMultiplier, match.rhsConstant);
    }
    Value *guard = stepPositive;
    for (auto *condition : {static_cast<Value *>(stepSmall),
                            static_cast<Value *>(startNonnegative),
                            static_cast<Value *>(startBeforeBound),
                            static_cast<Value *>(initialInLowerRange),
                            static_cast<Value *>(initialInUpperRange),
                            static_cast<Value *>(boundSafe)})
        guard = new BinaryInst(i1, Instruction::And, guard, condition,
                               preheader, true);
    for (Value *condition : affineSafetyConditions)
        guard = new BinaryInst(i1, Instruction::And, guard, condition,
                               preheader, true);
    for (auto *instruction : {static_cast<Instruction *>(stepPositive),
                              static_cast<Instruction *>(stepSmall),
                              static_cast<Instruction *>(startNonnegative),
                              static_cast<Instruction *>(startBeforeBound),
                              static_cast<Instruction *>(initialInLowerRange),
                              static_cast<Instruction *>(initialInUpperRange),
                              static_cast<Instruction *>(maxMinusStep),
                              static_cast<Instruction *>(safeBound),
                              static_cast<Instruction *>(boundSafe)})
        preheader->add_instruction_before_terminator(instruction);
    for (Instruction *instruction : affineSafetyInstructions)
        preheader->add_instruction_before_terminator(instruction);
    // The guard's AND chain was created without insertion.  Insert it in
    // dependency order by walking backwards from the final node.
    std::vector<Instruction *> guardChain;
    for (Value *cursor = guard;;) {
        auto *binary = dynamic_cast<BinaryInst *>(cursor);
        if (!binary || binary->op_id_ != Instruction::And) break;
        guardChain.push_back(binary);
        cursor = binary->get_operand(0);
    }
    for (auto it = guardChain.rbegin(); it != guardChain.rend(); ++it)
        preheader->add_instruction_before_terminator(*it);

    Function *helper = getPiecewiseModSumDeclaration(module);
    Function *function = loop.header->parent_;
    auto *fast = new BasicBlock(module, loop.header->name_ + ".pms.fast",
                                function);
    auto *slowJoin = new BasicBlock(module,
                                    loop.header->name_ + ".pms.slow",
                                    function);
    std::vector<Value *> arguments = {
        match.start,
        match.bound,
        match.step,
        new ConstantInt(i32, match.piecewiseEnabled),
        new ConstantInt(i32, match.lhsMultiplier),
        new ConstantInt(i32, match.lhsConstant),
        new ConstantInt(i32, match.rhsMultiplier),
        new ConstantInt(i32, match.rhsConstant),
        new ConstantInt(i32, match.trueUsesRight),
        new ConstantInt(i32, match.linearMultiplier),
        new ConstantInt(i32, match.multiplier),
        new ConstantInt(i32, match.divisor),
        new ConstantInt(i32, match.quotientMultiplier),
        new ConstantInt(i32, match.contributionConstant),
        new ConstantInt(i32, match.innerModulus),
        new ConstantInt(i32, match.additiveConstant),
        new ConstantInt(i32, match.outerModulus),
        match.initial,
    };
    auto *result = new CallInst(helper, arguments, fast);
    Value *fastRemainderBase = result;
    if (match.remainderBase) {
        auto *adjusted = new BinaryInst(
            i32, Instruction::Sub, result,
            new ConstantInt(i32, match.additiveConstant), fast, true);
        fast->add_instruction(adjusted);
        fastRemainderBase = adjusted;
    }
    auto *success = new ICmpInst(
        ICmpInst::ICMP_NE, result,
        new ConstantInt(i32, std::numeric_limits<int>::min()), fast);
    new BranchInst(success, exit, slowJoin, fast);
    new BranchInst(loop.header, slowJoin);

    preheader->delete_instr(oldBranch);
    preheader->remove_succ_basic_block(loop.header);
    loop.header->remove_pre_basic_block(preheader);
    new BranchInst(guard, fast, slowJoin, preheader);

    for (auto *phi : {match.induction, match.state})
        for (unsigned index = 0; index < phi->num_ops_; index += 2)
            if (phi->get_operand(index + 1) == preheader) {
                phi->set_operand(index + 1, slowJoin);
                break;
            }

    for (const auto &plan : exitPlans) {
        Value *fastIncoming = plan.incoming;
        if (plan.kind == ExitValueKind::RemainderBase)
            fastIncoming = fastRemainderBase;
        plan.phi->add_phi_pair_operand(fastIncoming, fast);
    }
    if (match.remainderBase && !remainderBaseExitPhi) {
        bool hasExternalUse = false;
        for (const auto &use : match.remainderBase->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ && !loop.blocks.count(user->parent_)) {
                hasExternalUse = true;
                break;
            }
        }
        if (hasExternalUse) {
            std::vector<Value *> values = {match.remainderBase,
                                           fastRemainderBase};
            std::vector<BasicBlock *> blocks = {loop.header, fast};
            remainderBaseExitPhi = new PhiInst(Instruction::PHI, values,
                                                blocks, i32, exit);
            exit->add_instruction_front(remainderBaseExitPhi);
        }
    }

    if (remainderBaseExitPhi) {
        std::vector<std::pair<Instruction *, unsigned>> baseExternalUses;
        for (const auto &use : match.remainderBase->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user != remainderBaseExitPhi && user->parent_ &&
                !loop.blocks.count(user->parent_))
                baseExternalUses.push_back({user, use.arg_no_});
        }
        for (auto &[user, operand] : baseExternalUses)
            user->set_operand(operand, remainderBaseExitPhi);
    }

    modFolded_.insert(loop.header);
    if (isLoopRepFoldDebugEnabled())
        std::cerr << "[LoopRepFold] piecewise modular fold func="
                  << function->name_ << " header=" << loop.header->name_
                  << " divisor=" << match.divisor
                  << " inner-mod=" << match.innerModulus
                  << " outer-mod=" << match.outerModulus << "\n";
    return true;
}

// ── 主变换 ──────────────────────────────────────────────────────────────────

bool LoopRepFold::tryFold(Loop &loop, Module *module, ScalarEvolution *SE) {
    if (isLoopRepFoldDebugEnabled())
        std::cerr << "[LoopRepFold] inspect header=" << loop.header->name_
                  << " blocks=" << loop.blocks.size() << "\n";
    if (!loop.preheader) {
        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] reject header=" << loop.header->name_
                      << " reason=no-dedicated-preheader\n";
        return false;
    }
    if (tryFoldPiecewiseModularSum(loop, module))
        return true;
    // Every supported closed form summarizes the number of iterations implied
    // by the header condition.  An additional exiting edge (for example a
    // while-body break) can terminate earlier, so the summary is invalid even
    // when all scalar updates otherwise match.  Check this before dispatching
    // to any folding mode; individual realizers must never infer unique
    // control flow merely from a single latch or a single destination block.
    if (loop.exiting.size() != 1 || loop.exiting.front() != loop.header ||
        loop.exits.size() != 1)
        return debugReject("loop does not have a unique header exit");
    // 模式要求 header phi 恰好 (preheader, latch) 两对入边 → 单 latch
    BasicBlock *latch = loop.singleLatch();
    if (!latch) return false;

    // 1. header 必须恰好有 2 个 phi 节点
    PhiInst *phi0 = nullptr, *phi1 = nullptr;
    int phi_count = 0;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (phi_count == 0) phi0 = static_cast<PhiInst *>(inst);
        else if (phi_count == 1) phi1 = static_cast<PhiInst *>(inst);
        phi_count++;
    }
    if (phi_count != 2 || !phi0 || !phi1) return debugReject("header does not have exactly two phis");

    // 2. 识别 r_phi（计数 IV）和 total_phi（累加器）
    PhiInst *r_phi = nullptr, *total_phi = nullptr;
    long long ivInit = 0;
    long long ivStride = 0;
    if (isCountingIV(phi0, loop, latch, &ivInit, &ivStride)) {
        r_phi = phi0; total_phi = phi1;
    } else if (isCountingIV(phi1, loop, latch, &ivInit, &ivStride)) {
        r_phi = phi1; total_phi = phi0;
    } else {
        return debugReject("cannot find counting IV");
    }
    if (total_phi->type_->tid_ != Type::IntegerTyID) return debugReject("total phi is not integer");
    if (total_phi->num_ops_ != 4) return debugReject("total phi does not have two incomings");

    // 3. 找循环条件和出口块
    auto *term = loop.header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return debugReject("bad loop header terminator");

    auto *cond_inst = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cond_inst || cond_inst->icmp_op_ != ICmpInst::ICMP_SLT) return debugReject("loop condition is not slt");
    if (cond_inst->get_operand(0) != r_phi) return debugReject("condition does not use counting IV");

    Value *N = cond_inst->get_operand(1);
    if (!isLoopInvariant(N, loop.blocks)) return debugReject("loop bound is not invariant");

    auto *body_entry = static_cast<BasicBlock *>(term->get_operand(1));
    auto *loop_exit  = static_cast<BasicBlock *>(term->get_operand(2));
    if (!loop.blocks.count(body_entry)) return debugReject("true successor is not loop body");
    if (loop.blocks.count(loop_exit))   return debugReject("false successor is inside loop");

    // 4. loop body 内无 store，无 call（保守：保证每次 r 迭代计算结果相同）
    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) return debugReject("loop has store");
            if (inst->is_call())  return debugReject("loop has call");
        }
    }

    // 5. 获取 total_init（preheader 入值）和 total_latch（latch 入值）
    Value *total_init = nullptr, *total_latch = nullptr;
    for (unsigned i = 0; i < total_phi->num_ops_; i += 2) {
        auto *bb = static_cast<BasicBlock *>(total_phi->get_operand(i + 1));
        if (bb == loop.preheader) total_init  = total_phi->get_operand(i);
        else if (bb == latch) total_latch = total_phi->get_operand(i);
    }
    if (!total_init || !total_latch) return debugReject("cannot find total init/latch incoming");
    if (!isLoopInvariant(total_init, loop.blocks)) return debugReject("total init is not invariant");

    if (tryFoldModularRecurrence(loop, module, latch, r_phi, total_phi,
                                 loop_exit, N, total_init, total_latch,
                                 ivInit, ivStride))
        return true;

    if (tryFoldAffineSum(loop, module, SE, latch, r_phi, total_phi,
                         loop_exit, N, total_init, total_latch,
                         ivInit, ivStride))
        return true;

    // 仿射路径未命中时，纯重复折叠只支持 init=0 / 步长=1 的计数 IV
    if (ivInit != 0 || ivStride != 1)
        return false;

    // 6. total_phi 在 loop body 内只作为 phi incoming（纯加法传递）
    int body_phi_uses = 0;
    for (auto &use : total_phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user) continue;
        if (!loop.blocks.count(user->parent_)) continue;
        if (!user->is_phi()) return false;
        body_phi_uses++;
    }
    if (body_phi_uses == 0) return false;

    // 6b. 折叠公式 init + (total_latch_1 - init) * N 成立的充分条件：
    //     total_latch 作为 total_phi 的函数必须恰为 total_phi + C，且 C 每圈
    //     恒定。数据流上要求 total_latch 沿"累加链"可达 total_phi：
    //       acc := total_phi | phi(全部入边均为 acc) | acc ± q（q 与 IV/total
    //       无数据依赖）
    //     phi 汇合允许（路径间增量不同时由下面的控制检查兜底）；总和系数
    //     不为 1（如 t+t）或经"选择"引入非 acc 值（如 phi[total, x]）即拒绝。
    //     控制流上要求：除本循环 header 外，循环内所有条件分支的条件都不
    //     依赖计数 IV——否则路径选择随圈变化，增量不再恒定。
    //     （唯一的跨圈状态只有 r_phi/total_phi 两个 header phi；条件无法
    //     依赖 total_phi——那是非 phi 使用，已被检查 5 拒绝。）
    {
        std::set<Value *> indepVisited;
        std::function<bool(Value *)> indepOfIV = [&](Value *v) -> bool {
            if (v == r_phi || v == total_phi) return false;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst) return true;
            if (!loop.blocks.count(inst->parent_)) return true;
            if (!indepVisited.insert(v).second) return true;
            for (unsigned i = 0; i < inst->num_ops_; i++)
                if (!indepOfIV(inst->get_operand(i))) return false;
            return true;
        };

        std::set<Value *> accVisited;
        std::function<bool(Value *)> isAcc = [&](Value *v) -> bool {
            if (v == total_phi) return true;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst || !loop.blocks.count(inst->parent_)) return false;
            if (!accVisited.insert(v).second) return true; // 环上节点正在验证
            if (inst->is_phi()) {
                auto *phi = static_cast<PhiInst *>(inst);
                for (unsigned i = 0; i < phi->num_ops_; i += 2)
                    if (!isAcc(phi->get_operand(i))) return false;
                return true;
            }
            auto *bin = dynamic_cast<BinaryInst *>(inst);
            if (!bin) return false;
            Value *lhs = bin->get_operand(0);
            Value *rhs = bin->get_operand(1);
            if (bin->is_add())
                return (isAcc(lhs) && indepOfIV(rhs)) ||
                       (isAcc(rhs) && indepOfIV(lhs));
            if (bin->is_sub())
                return isAcc(lhs) && indepOfIV(rhs);
            return false;
        };
        if (!isAcc(total_latch)) return false;

        for (auto *bb : loop.blocks) {
            if (bb == loop.header) continue;
            auto *t = bb->get_terminator();
            if (t && t->is_br() && t->num_ops_ == 3 &&
                !indepOfIV(t->get_operand(0)))
                return false;
        }
    }

    // 6c. 折叠会拆掉回边并让循环只走一遍：除 total_phi 外，循环内任何值
    //     （含 r_phi）被循环外使用时，其"终值"在折叠后都是错的 → bail。
    //     total_phi 的循环外使用由 7d 统一改写到出口 phi。
    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst == total_phi) continue;
            for (auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && !loop.blocks.count(user->parent_))
                    return false;
            }
        }
    }

    // Folding adds latch as a new predecessor of loop_exit. Existing exit phis
    // must therefore be extendable before any CFG mutation happens.
    for (auto *inst : loop_exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred != loop.header) continue;
            Value *incoming = phi->get_operand(i);
            if (incoming != total_phi && !isLoopInvariant(incoming, loop.blocks))
                return false;
        }
    }

    // ────────────────────────── 变换开始 ──────────────────────────────────
    if (std::getenv("DEBUG_LOOP_REPFOLD"))
        std::cerr << "[LoopRepFold] fold func=" << loop.header->parent_->name_
                  << " header=" << loop.header->name_ << "\n";
    auto *int_ty = total_phi->type_;

    // 7a. 在 latch 中（terminator 之前）插入 total_final 计算
    //     total_final = total_init + (total_latch - total_init) * N
    //     当 total_init == 0 时简化为 total_latch * N
    Instruction *total_final = nullptr;
    {
        auto *ci_init = dynamic_cast<ConstantInt *>(total_init);
        if (ci_init && ci_init->value_ == 0) {
            auto *mul = new BinaryInst(int_ty, Instruction::Mul,
                                       total_latch, N, latch, true);
            latch->add_instruction_before_terminator(mul);
            total_final = mul;
        } else {
            auto *delta  = new BinaryInst(int_ty, Instruction::Sub,
                                          total_latch, total_init, latch, true);
            auto *scaled = new BinaryInst(int_ty, Instruction::Mul,
                                          delta, N, latch, true);
            auto *result = new BinaryInst(int_ty, Instruction::Add,
                                          total_init, scaled, latch, true);
            latch->add_instruction_before_terminator(delta);
            latch->add_instruction_before_terminator(scaled);
            latch->add_instruction_before_terminator(result);
            total_final = result;
        }
    }

    // 7b. 重定向 latch 的无条件跳转：header → loop_exit
    {
        auto *latch_br = latch->get_terminator();
        // set_operand 会维护 BasicBlock 的 use_list
        latch_br->set_operand(0, loop_exit);
        latch->remove_succ_basic_block(loop.header);
        latch->add_succ_basic_block(loop_exit);
        loop.header->remove_pre_basic_block(latch);
        loop_exit->add_pre_basic_block(latch);
    }

    // 7b2. loop_exit 多了 latch 前驱，已有 exit phi 必须补齐 incoming。
    //      total_phi 在新边上的值是 total_final；循环不变量保持原值。
    for (auto *inst : loop_exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        bool hasLatchIncoming = false;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == latch) {
                hasLatchIncoming = true;
                break;
            }
        }
        if (hasLatchIncoming) continue;

        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred != loop.header) continue;
            Value *incoming = phi->get_operand(i);
            phi->add_phi_pair_operand(incoming == total_phi ? total_final : incoming,
                                      latch);
            break;
        }
    }

    // 7c. 删除 header 各 phi 中来自 latch 的 incoming
    auto removeIncoming = [](PhiInst *phi, BasicBlock *pred) {
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            if (phi->get_operand(i + 1) == pred) {
                phi->remove_operands(i, i + 1);
                return;
            }
        }
    };
    removeIncoming(r_phi,     latch);
    removeIncoming(total_phi, latch);

    // 7d. 在 loop_exit 插入 phi 处理出口值，并替换 total_phi 的全部循环外
    //     使用：v_total = phi [total_phi, header], [total_final, latch]。
    //     header 是唯一 exiting 块且 loop_exit 是专用出口 ⇒ 任何从循环到
    //     循环外的路径必经 loop_exit ⇒ exit_phi 支配所有循环外使用点
    //     （不限于 loop_exit 块内，远处的使用同样必须改写——折叠后
    //     total_phi 本身收缩为 init，留着不改写就是错值）。
    {
        bool used_outside = false;
        for (auto &use : total_phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ && !loop.blocks.count(user->parent_)) {
                used_outside = true;
                break;
            }
        }

        if (used_outside) {
            std::vector<Value *>      phi_vals = {total_phi, total_final};
            std::vector<BasicBlock *> phi_bbs  = {loop.header, latch};
            auto *exit_phi = new PhiInst(Instruction::PHI, phi_vals, phi_bbs,
                                         int_ty, loop_exit);
            loop_exit->add_instruction_front(exit_phi);

            std::vector<std::pair<Instruction *, unsigned>> to_replace;
            for (auto &use : total_phi->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ && user != exit_phi &&
                    !(user->parent_ == loop_exit && user->is_phi()) &&
                    !loop.blocks.count(user->parent_))
                    to_replace.push_back({user, use.arg_no_});
            }
            for (auto &[user, arg_no] : to_replace)
                user->set_operand(arg_no, exit_phi);
        }
    }

    return true;
}

// ── 函数级 / 模块级入口 ─────────────────────────────────────────────────────

void LoopRepFold::runOnFunction(Function *func, AnalysisManager *AM) {
    if (func->basic_blocks_.empty() || !AM) return;
    modFolded_.clear();

    // 折叠成功后 CFG 已变（仿射路径还会整体删块），Loop 快照过期 →
    // 失效缓存、重新分析再扫，直到无折叠机会。
    bool changed = true;
    while (changed) {
        changed = false;
        LoopInfo &LI = AM->getLoopInfo(func);
        if (LI.allLoops().empty()) return;
        ScalarEvolution *SE = &AM->getScalarEvolution(func);

        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] function=" << func->name_
                      << " loops=" << LI.allLoops().size() << "\n";

        // 外层 r-loop 体积最大 → 按 blocks 数降序（由外向内）
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->blocks.size() > b->blocks.size(); });

        for (auto *loop : loops) {
            if (tryFold(*loop, func->parent_, SE)) {
                changed = true;
                AM->clear(func);
                break;
            }
        }
    }
}

void LoopRepFold::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LoopRepFold::execute(Module *module, AnalysisManager &AM) {
    piecewiseModSumDecl_ = nullptr;
    std::vector<Function *> functions = module->function_list_;
    for (auto func : functions) {
        if (!func->is_declaration())
            runOnFunction(func, &AM);
    }
    return PreservedAnalyses::none();
}
