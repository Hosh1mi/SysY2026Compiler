#include "../../../include/mid/opt/loopRepFold.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <set>

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

bool fitsI32(long long value) {
    return value >= std::numeric_limits<int>::min() &&
           value <= std::numeric_limits<int>::max();
}

bool checkedAdd(long long a, long long b, long long &out) {
    if ((b > 0 && a > std::numeric_limits<long long>::max() - b) ||
        (b < 0 && a < std::numeric_limits<long long>::min() - b))
        return false;
    out = a + b;
    return true;
}

bool checkedMul(long long a, long long b, long long &out) {
    __int128 product = static_cast<__int128>(a) * static_cast<__int128>(b);
    if (product > std::numeric_limits<long long>::max() ||
        product < std::numeric_limits<long long>::min())
        return false;
    out = static_cast<long long>(product);
    return true;
}

bool checkedSub(long long a, long long b, long long &out) {
    if (b == std::numeric_limits<long long>::min()) return false;
    return checkedAdd(a, -b, out);
}

bool scevConst(const SCEV *s, long long &value) {
    auto *c = dynamic_cast<const SCEVConstant *>(s);
    if (!c) return false;
    value = c->value();
    return true;
}

struct AffineStep {
    bool valid = false;
    long long coeff = 0;
    long long constant = 0;
};

bool addAffine(AffineStep &lhs, const AffineStep &rhs) {
    long long coeff = 0;
    long long constant = 0;
    if (!checkedAdd(lhs.coeff, rhs.coeff, coeff)) return false;
    if (!checkedAdd(lhs.constant, rhs.constant, constant)) return false;
    lhs.valid = true;
    lhs.coeff = coeff;
    lhs.constant = constant;
    return true;
}

bool scaleAffine(AffineStep &expr, long long factor) {
    long long coeff = 0;
    long long constant = 0;
    if (!checkedMul(expr.coeff, factor, coeff)) return false;
    if (!checkedMul(expr.constant, factor, constant)) return false;
    expr.coeff = coeff;
    expr.constant = constant;
    return true;
}

AffineStep extractAffineStep(const SCEV *s, PhiInst *iv, ::Loop *loop) {
    AffineStep invalid;
    if (!s || !iv || !loop) return invalid;

    long long c = 0;
    if (scevConst(s, c)) {
        return {true, 0, c};
    }

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s)) {
        if (unknown->value() == iv)
            return {true, 1, 0};
        return invalid;
    }

    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(s)) {
        if (addrec->loop() != loop || addrec->phi() != iv)
            return invalid;
        long long start = 0;
        long long step = 0;
        if (!scevConst(addrec->start(), start) || !scevConst(addrec->step(), step))
            return invalid;
        return {true, step, start};
    }

    if (auto *add = dynamic_cast<const SCEVAddExpr *>(s)) {
        AffineStep result{true, 0, 0};
        for (auto *op : add->operands()) {
            AffineStep term = extractAffineStep(op, iv, loop);
            if (!term.valid || !addAffine(result, term))
                return invalid;
        }
        return result;
    }

    if (auto *mul = dynamic_cast<const SCEVMulExpr *>(s)) {
        AffineStep result{true, 0, 1};
        bool sawAffine = false;
        long long factor = 1;
        for (auto *op : mul->operands()) {
            long long constVal = 0;
            if (scevConst(op, constVal)) {
                if (!checkedMul(factor, constVal, factor))
                    return invalid;
                continue;
            }

            if (sawAffine) return invalid;
            result = extractAffineStep(op, iv, loop);
            if (!result.valid) return invalid;
            sawAffine = true;
        }
        if (!sawAffine)
            return {true, 0, factor};
        if (!scaleAffine(result, factor))
            return invalid;
        return result;
    }

    return invalid;
}

struct AccumulatorStep {
    bool valid = false;
    int totalRefs = 0;
    AffineStep step;
};

AccumulatorStep invalidAccumulatorStep() {
    return {false, 0, {}};
}

AccumulatorStep combineAccumulatorSteps(AccumulatorStep lhs,
                                        AccumulatorStep rhs,
                                        bool subtractRhs) {
    if (!lhs.valid || !rhs.valid) return invalidAccumulatorStep();
    if (subtractRhs && !scaleAffine(rhs.step, -1)) return invalidAccumulatorStep();
    if (!addAffine(lhs.step, rhs.step)) return invalidAccumulatorStep();
    lhs.totalRefs += rhs.totalRefs;
    return lhs;
}

AccumulatorStep extractAccumulatorStep(Value *value, PhiInst *totalPhi,
                                       PhiInst *iv, ::Loop *loop,
                                       ScalarEvolution *SE,
                                       const std::set<BasicBlock *> &loopBlocks,
                                       std::set<Instruction *> &chain) {
    if (!value || !totalPhi || !iv || !loop || !SE) return invalidAccumulatorStep();
    if (value == totalPhi)
        return {true, 1, {true, 0, 0}};

    auto *inst = dynamic_cast<Instruction *>(value);
    if (inst && loopBlocks.count(inst->parent_) &&
        (inst->is_add() || inst->is_sub())) {
        chain.insert(inst);
        AccumulatorStep lhs = extractAccumulatorStep(inst->get_operand(0),
                                                     totalPhi, iv, loop, SE,
                                                     loopBlocks, chain);
        AccumulatorStep rhs = extractAccumulatorStep(inst->get_operand(1),
                                                     totalPhi, iv, loop, SE,
                                                     loopBlocks, chain);
        return combineAccumulatorSteps(lhs, rhs, inst->is_sub());
    }

    AffineStep step = extractAffineStep(SE->getSCEV(value), iv, loop);
    if (!step.valid) return invalidAccumulatorStep();
    return {true, 0, step};
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
    if (!scevConst(ivAddRec->start(), scevInit) ||
        !scevConst(ivAddRec->step(), scevStride))
        return debugReject("iv addrec is not constant");
    if (scevInit != ivInit || scevStride != ivStride)
        return debugReject("local iv and scev mismatch");
    if (ivStride <= 0) return debugReject("iv stride is not positive");

    auto *boundCI = dynamic_cast<ConstantInt *>(bound);
    if (!boundCI) return debugReject("non-constant bound");
    long long diff = 0;
    if (!checkedSub(boundCI->value_, ivInit, diff)) return debugReject("bound/init overflow");
    long long iterations = 0;
    if (diff > 0) {
        long long adjusted = 0;
        if (!checkedAdd(diff, ivStride - 1, adjusted)) return debugReject("trip ceil overflow");
        iterations = adjusted / ivStride;
    }

    auto *initCI = dynamic_cast<ConstantInt *>(totalInit);
    if (!initCI) return debugReject("non-constant total init");

    auto *update = dynamic_cast<Instruction *>(totalLatch);
    if (!update || !loop.blocks.count(update->parent_))
        return debugReject("total update is not in loop");

    std::set<Instruction *> accumulatorChain;
    AccumulatorStep accumulator = extractAccumulatorStep(totalLatch, totalPhi,
                                                        ivPhi, &loop, SE,
                                                        loop.blocks,
                                                        accumulatorChain);
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

    AffineStep step = accumulator.step;

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

    long long nMinusOne = 0;
    long long pairCount = 0;
    long long triangular = 0;
    long long linearTerm = 0;
    long long constantTerm = 0;
    long long result = 0;
    if (!checkedAdd(iterations, -1, nMinusOne)) return debugReject("n-1 overflow");
    if (!checkedMul(iterations, nMinusOne, pairCount)) return debugReject("n*(n-1) overflow");
    triangular = pairCount / 2;
    if (!checkedMul(step.coeff, triangular, linearTerm)) return debugReject("linear term overflow");
    if (!checkedMul(step.constant, iterations, constantTerm)) return debugReject("constant term overflow");
    if (!checkedAdd(initCI->value_, linearTerm, result)) return debugReject("result add overflow");
    if (!checkedAdd(result, constantTerm, result)) return debugReject("result add overflow");
    if (!fitsI32(result)) return debugReject("result does not fit i32");

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
    // 仅规范 0/+1 IV：此时 trip count 恰为 N（当 N>=1）。
    if (ivInit != 0 || ivStride != 1) return false;
    if (modFolded_.count(loop.header)) return false;

    // total_latch 必须恰为 srem(add(total_phi, c), m)，c>0, m>0 常量。
    auto *rem = dynamic_cast<BinaryInst *>(totalLatch);
    if (!rem || !rem->is_rem()) return false;
    if (!loop.blocks.count(rem->parent_)) return false;
    auto *mCI = dynamic_cast<ConstantInt *>(rem->get_operand(1));
    if (!mCI || mCI->value_ <= 0) return false;
    long long m = mCI->value_;

    auto *add = dynamic_cast<BinaryInst *>(rem->get_operand(0));
    if (!add || !add->is_add() || !loop.blocks.count(add->parent_)) return false;
    Value *addL = add->get_operand(0), *addR = add->get_operand(1);
    ConstantInt *cCI = nullptr;
    if (addL == totalPhi) cCI = dynamic_cast<ConstantInt *>(addR);
    else if (addR == totalPhi) cCI = dynamic_cast<ConstantInt *>(addL);
    if (!cCI || cCI->value_ <= 0) return false;
    long long c = cCI->value_;

    // total_phi 在循环内只能被那个 add 使用 → 递推确为 (total+c)%m。
    for (auto &use : totalPhi->use_list_) {
        auto *u = dynamic_cast<Instruction *>(use.val_);
        if (!u || !u->parent_ || !loop.blocks.count(u->parent_)) continue;
        if (u != add) return false;
    }

    // 除 total_phi 外，循环内任何值（含 IV）不得被循环外使用：折叠后快路径
    // 不执行循环，这些值在外部将变为 undefined。
    for (auto *bb : loop.blocks)
        for (auto *inst : bb->instr_list_) {
            if (inst == totalPhi) continue;
            for (auto &use : inst->use_list_) {
                auto *u = dynamic_cast<Instruction *>(use.val_);
                if (u && u->parent_ && !loop.blocks.count(u->parent_))
                    return false;
            }
        }

    // 出口必须是循环专属（唯一前驱为 header）且暂不含 phi（保持简单、安全）。
    if (loopExit->pre_bbs_.size() != 1 || loopExit->pre_bbs_[0] != loop.header)
        return false;
    if (!loopExit->instr_list_.empty() &&
        loopExit->instr_list_.front()->is_phi())
        return false;

    // preheader 必须以无条件 br 跳向 header。
    BasicBlock *PH = loop.preheader;
    auto *phTerm = PH->get_terminator();
    if (!phTerm || !phTerm->is_br() || phTerm->num_ops_ != 1) return false;
    if (phTerm->get_operand(0) != loop.header) return false;

    // 防溢出上界：c*N <= INT_MAX - m  →  N <= (INT_MAX - m)/c。
    long long BOUND = ((long long)std::numeric_limits<int>::max() - m) / c;
    if (BOUND < 1) return false;

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

    // ── 4. 出口合并：exit_phi = [total_phi(来自header), tot(来自fast)]，
    //        改写 total_phi 的全部循环外使用。 ──
    std::vector<Value *> vals = {totalPhi, tot};
    std::vector<BasicBlock *> bbs = {loop.header, fast};
    auto *exitPhi = new PhiInst(Instruction::PHI, vals, bbs, i32, loopExit);
    loopExit->add_instruction_front(exitPhi);

    std::vector<std::pair<Instruction *, unsigned>> toReplace;
    for (auto &use : totalPhi->use_list_) {
        auto *u = dynamic_cast<Instruction *>(use.val_);
        if (u && u != exitPhi && u->parent_ && !loop.blocks.count(u->parent_))
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

// ── 主变换 ──────────────────────────────────────────────────────────────────

bool LoopRepFold::tryFold(Loop &loop, Module *module, ScalarEvolution *SE) {
    if (!loop.preheader) return false;
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
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func, &AM);
    }
    return PreservedAnalyses::none();
}
