#include "../../include/mid/opt/loopRepFold.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
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

// ── CFG / Dominator helpers（与 LICM 相同策略）──────────────────────────────

std::vector<BasicBlock *> LoopRepFold::computeRPO(Function *func) {
    std::vector<BasicBlock *> postorder;
    std::set<BasicBlock *> visited;
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ)) dfs(succ);
        postorder.push_back(bb);
    };
    if (!func->basic_blocks_.empty())
        dfs(func->basic_blocks_[0]);
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
}

void LoopRepFold::computeDominators(const std::vector<BasicBlock *> &rpo) {
    idom_.clear();
    rpoIdx_.clear();
    if (rpo.empty()) return;
    BasicBlock *entry = rpo[0];
    for (int i = 0; i < (int)rpo.size(); i++)
        rpoIdx_[rpo[i]] = i;
    idom_[entry] = entry;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : rpo) {
            if (bb == entry) continue;
            BasicBlock *new_idom = nullptr;
            for (auto pred : bb->pre_bbs_) {
                if (!idom_.count(pred)) continue;
                new_idom = new_idom ? intersect(pred, new_idom) : pred;
            }
            if (new_idom && idom_[bb] != new_idom) {
                idom_[bb] = new_idom;
                changed = true;
            }
        }
    }
}

BasicBlock *LoopRepFold::intersect(BasicBlock *a, BasicBlock *b) {
    while (a != b) {
        while (rpoIdx_[a] > rpoIdx_[b]) a = idom_[a];
        while (rpoIdx_[b] > rpoIdx_[a]) b = idom_[b];
    }
    return a;
}

bool LoopRepFold::dominates(BasicBlock *a, BasicBlock *b) {
    while (b != idom_[b]) {
        if (b == a) return true;
        b = idom_[b];
    }
    return b == a;
}

// ── 循环检测（多 latch 合并为同一 loop）────────────────────────────────────

std::vector<LoopRepFold::Loop> LoopRepFold::findLoops(Function *func) {
    std::map<BasicBlock *, Loop> headerToLoop;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!idom_.count(succ)) continue;
            if (!dominates(succ, bb)) continue;

            auto &loop = headerToLoop[succ];
            loop.header = succ;
            if (!loop.latch) loop.latch = bb;

            loop.blocks.insert(succ);
            std::queue<BasicBlock *> wl;
            wl.push(bb);
            while (!wl.empty()) {
                auto cur = wl.front();
                wl.pop();
                if (!loop.blocks.insert(cur).second) continue;
                for (auto pred : cur->pre_bbs_)
                    if (!loop.blocks.count(pred)) wl.push(pred);
            }
        }
    }

    // 找唯一外部前驱作为 preheader
    std::vector<Loop> loops;
    for (auto &kv : headerToLoop) {
        auto &loop = kv.second;
        BasicBlock *pre = nullptr;
        int ext_count = 0;
        for (auto pred : loop.header->pre_bbs_) {
            if (!loop.blocks.count(pred)) {
                pre = pred;
                ext_count++;
            }
        }
        loop.preheader = (ext_count == 1) ? pre : nullptr;
        loops.push_back(std::move(loop));
    }
    return loops;
}

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
bool LoopRepFold::isCountingIV(PhiInst *phi, const Loop &loop,
                               long long *init, long long *stride) {
    if (phi->type_->tid_ != Type::IntegerTyID) return false;
    if (phi->num_ops_ != 4) return false; // 恰好 2 对 (val, BB)

    Value *pre_val = nullptr, *latch_val = nullptr;
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        auto *bb = static_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (bb == loop.preheader) pre_val  = phi->get_operand(i);
        else if (bb == loop.latch) latch_val = phi->get_operand(i);
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

bool LoopRepFold::tryFoldAffineSum(Loop &loop, Module *module, ScalarEvolution *SE,
                                   ::Loop *analysisLoop, PhiInst *ivPhi,
                                   PhiInst *totalPhi, BasicBlock *loopExit,
                                   Value *bound, Value *totalInit,
                                   Value *totalLatch, long long ivInit,
                                   long long ivStride) {
    if (!SE || !analysisLoop) return debugReject("missing analysis loop");
    if (analysisLoop->singleLatch() != loop.latch) return debugReject("single latch mismatch");
    if (analysisLoop->singleExit() != loopExit) return debugReject("single exit mismatch");

    auto *ivAddRec = dynamic_cast<const SCEVAddRecExpr *>(SE->getSCEV(ivPhi));
    if (!ivAddRec || ivAddRec->loop() != analysisLoop || ivAddRec->phi() != ivPhi)
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
                                                        ivPhi, analysisLoop, SE,
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
    std::vector<BasicBlock *> deadBlocks(loop.blocks.begin(), loop.blocks.end());
    for (auto *bb : deadBlocks)
        func->remove_bb(bb);
    return true;
}

// ── 主变换 ──────────────────────────────────────────────────────────────────

bool LoopRepFold::tryFold(Loop &loop, Module *module, ScalarEvolution *SE,
                          ::Loop *analysisLoop) {
    if (!loop.preheader) return debugReject("missing local preheader");

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
    if (isCountingIV(phi0, loop, &ivInit, &ivStride)) {
        r_phi = phi0; total_phi = phi1;
    } else if (isCountingIV(phi1, loop, &ivInit, &ivStride)) {
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
        else if (bb == loop.latch) total_latch = total_phi->get_operand(i);
    }
    if (!total_init || !total_latch) return debugReject("cannot find total init/latch incoming");
    if (!isLoopInvariant(total_init, loop.blocks)) return debugReject("total init is not invariant");

    if (tryFoldAffineSum(loop, module, SE, analysisLoop, r_phi, total_phi,
                         loop_exit, N, total_init, total_latch,
                         ivInit, ivStride))
        return true;

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

    // 6b. total_latch 不能依赖计数 IV（每次迭代增量必须相同）
    {
        std::set<Value *> visited;
        std::function<bool(Value *)> dependsOnIV = [&](Value *v) -> bool {
            if (v == r_phi) return true;
            if (!visited.insert(v).second) return false;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst) return false;
            if (!loop.blocks.count(inst->parent_)) return false;
            for (unsigned i = 0; i < inst->num_ops_; i++)
                if (dependsOnIV(inst->get_operand(i))) return true;
            return false;
        };
        if (dependsOnIV(total_latch)) return false;
    }

    // ────────────────────────── 变换开始 ──────────────────────────────────
    {
        FILE *f = fopen("/tmp/repfold_debug.txt", "a");
        if (f) {
            fprintf(f, "[LoopRepFold] folding loop header=%s in func=%s\n",
                    loop.header->name_.c_str(), loop.header->parent_->name_.c_str());
            fclose(f);
        }
    }
    auto *int_ty = total_phi->type_;

    // 7a. 在 latch 中（terminator 之前）插入 total_final 计算
    //     total_final = total_init + (total_latch - total_init) * N
    //     当 total_init == 0 时简化为 total_latch * N
    Instruction *total_final = nullptr;
    {
        auto *ci_init = dynamic_cast<ConstantInt *>(total_init);
        if (ci_init && ci_init->value_ == 0) {
            auto *mul = new BinaryInst(int_ty, Instruction::Mul,
                                       total_latch, N, loop.latch, true);
            loop.latch->add_instruction_before_terminator(mul);
            total_final = mul;
        } else {
            auto *delta  = new BinaryInst(int_ty, Instruction::Sub,
                                          total_latch, total_init, loop.latch, true);
            auto *scaled = new BinaryInst(int_ty, Instruction::Mul,
                                          delta, N, loop.latch, true);
            auto *result = new BinaryInst(int_ty, Instruction::Add,
                                          total_init, scaled, loop.latch, true);
            loop.latch->add_instruction_before_terminator(delta);
            loop.latch->add_instruction_before_terminator(scaled);
            loop.latch->add_instruction_before_terminator(result);
            total_final = result;
        }
    }

    // 7b. 重定向 latch 的无条件跳转：header → loop_exit
    {
        auto *latch_br = loop.latch->get_terminator();
        // set_operand 会维护 BasicBlock 的 use_list
        latch_br->set_operand(0, loop_exit);
        loop.latch->remove_succ_basic_block(loop.header);
        loop.latch->add_succ_basic_block(loop_exit);
        loop.header->remove_pre_basic_block(loop.latch);
        loop_exit->add_pre_basic_block(loop.latch);
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
    removeIncoming(r_phi,     loop.latch);
    removeIncoming(total_phi, loop.latch);

    // 7d. 在 loop_exit 插入 phi 处理出口值，并替换 total_phi 的使用
    //     v_total = phi [total_phi, header], [total_final, latch]
    {
        bool used_in_exit = false;
        for (auto &use : total_phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && user->parent_ == loop_exit) {
                used_in_exit = true;
                break;
            }
        }

        if (used_in_exit) {
            std::vector<Value *>      phi_vals = {total_phi, total_final};
            std::vector<BasicBlock *> phi_bbs  = {loop.header, loop.latch};
            auto *exit_phi = new PhiInst(Instruction::PHI, phi_vals, phi_bbs,
                                         int_ty, loop_exit);
            loop_exit->add_instruction_front(exit_phi);

            // 收集并替换 loop_exit 中 total_phi 的使用
            std::vector<std::pair<Instruction *, unsigned>> to_replace;
            for (auto &use : total_phi->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && user->parent_ == loop_exit && user != exit_phi)
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
    if (func->basic_blocks_.empty()) return;

    bool changed = true;
    while (changed) {
        changed = false;
        auto rpo = computeRPO(func);
        computeDominators(rpo);
        auto loops = findLoops(func);
        if (isLoopRepFoldDebugEnabled())
            std::cerr << "[LoopRepFold] function=" << func->name_
                      << " loops=" << loops.size() << "\n";

        // 优先处理小循环（内层），但对于本 pass，外层 r-loop 体积最大 → 按 blocks 数降序
        std::sort(loops.begin(), loops.end(),
                  [](const Loop &a, const Loop &b) { return a.blocks.size() > b.blocks.size(); });

        for (auto &loop : loops) {
            ScalarEvolution *SE = nullptr;
            ::Loop *analysisLoop = nullptr;
            if (AM) {
                LoopInfo &LI = AM->getLoopInfo(func);
                SE = &AM->getScalarEvolution(func);
                analysisLoop = LI.getLoopFor(loop.header);
            }
            if (tryFold(loop, func->parent_, SE, analysisLoop)) {
                changed = true;
                if (AM) AM->clear(func);
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
