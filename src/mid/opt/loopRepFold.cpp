#include "../../include/mid/opt/loopRepFold.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>

// ── 辅助检查 ────────────────────────────────────────────────────────────────

bool LoopRepFold::isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks) {
    if (dynamic_cast<Constant *>(val)) return true;
    if (dynamic_cast<GlobalVariable *>(val)) return true;
    if (dynamic_cast<Argument *>(val)) return true;
    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst) return true;
    return !blocks.count(inst->parent_);
}

// 判断 phi 是否为：初始值=0（来自 preheader），每次 latch 时 += 1
bool LoopRepFold::isCountingIV(PhiInst *phi, const Loop &loop, BasicBlock *latch) {
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
    if (!ci_init || ci_init->value_ != 0) return false;

    // latch_val 必须是 phi + 1
    auto *add = dynamic_cast<BinaryInst *>(latch_val);
    if (!add || !add->is_add()) return false;
    auto *op0 = add->get_operand(0);
    auto *op1 = add->get_operand(1);
    auto *ci0 = dynamic_cast<ConstantInt *>(op0);
    auto *ci1 = dynamic_cast<ConstantInt *>(op1);
    if (op0 == phi && ci1 && ci1->value_ == 1) return true;
    if (op1 == phi && ci0 && ci0->value_ == 1) return true;
    return false;
}

// ── 主变换 ──────────────────────────────────────────────────────────────────

bool LoopRepFold::tryFold(Loop &loop, Module *module) {
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
    if (phi_count != 2 || !phi0 || !phi1) return false;

    // 2. 识别 r_phi（计数 IV）和 total_phi（累加器）
    PhiInst *r_phi = nullptr, *total_phi = nullptr;
    if (isCountingIV(phi0, loop, latch)) {
        r_phi = phi0; total_phi = phi1;
    } else if (isCountingIV(phi1, loop, latch)) {
        r_phi = phi1; total_phi = phi0;
    } else {
        return false;
    }
    if (total_phi->type_->tid_ != Type::IntegerTyID) return false;
    if (total_phi->num_ops_ != 4) return false;

    // 3. 找循环条件和出口块
    auto *term = loop.header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;

    auto *cond_inst = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cond_inst || cond_inst->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    if (cond_inst->get_operand(0) != r_phi) return false;

    Value *N = cond_inst->get_operand(1);
    if (!isLoopInvariant(N, loop.blocks)) return false;

    auto *body_entry = static_cast<BasicBlock *>(term->get_operand(1));
    auto *loop_exit  = static_cast<BasicBlock *>(term->get_operand(2));
    if (!loop.blocks.count(body_entry)) return false;
    if (loop.blocks.count(loop_exit))   return false;

    // 4. loop body 内无 store，无 call（保守：保证每次 r 迭代计算结果相同）
    for (auto *bb : loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            if (inst->is_call())  return false;
        }
    }

    // 5. total_phi 在 loop body 内只作为 phi incoming（纯加法传递）
    int body_phi_uses = 0;
    for (auto &use : total_phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user) continue;
        if (!loop.blocks.count(user->parent_)) continue;
        if (!user->is_phi()) return false;
        body_phi_uses++;
    }
    if (body_phi_uses == 0) return false;

    // 6. 获取 total_init（preheader 入值）和 total_latch（latch 入值）
    Value *total_init = nullptr, *total_latch = nullptr;
    for (unsigned i = 0; i < total_phi->num_ops_; i += 2) {
        auto *bb = static_cast<BasicBlock *>(total_phi->get_operand(i + 1));
        if (bb == loop.preheader) total_init  = total_phi->get_operand(i);
        else if (bb == latch) total_latch = total_phi->get_operand(i);
    }
    if (!total_init || !total_latch) return false;
    if (!isLoopInvariant(total_init, loop.blocks)) return false;

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

void LoopRepFold::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return;

    LoopInfo LI;
    LI.analyze(func);
    if (LI.allLoops().empty()) return;

    // 外层 r-loop 体积最大 → 按 blocks 数降序（由外向内）。
    // tryFold 成功会拆掉该循环的回边，更内/外层的 Loop 结构随之过期——
    // 与迁移前行为一致：单轮快照，不重新分析；错过的机会留给下一次调度。
    std::vector<Loop *> loops;
    for (auto &l : LI.allLoops())
        loops.push_back(l.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->blocks.size() > b->blocks.size(); });

    for (auto *loop : loops)
        tryFold(*loop, func->parent_);
}

void LoopRepFold::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}
