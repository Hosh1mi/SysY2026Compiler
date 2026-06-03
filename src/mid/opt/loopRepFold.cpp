#include "../../include/mid/opt/loopRepFold.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <functional>
#include <queue>

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

// 判断 phi 是否为：初始值=0（来自 preheader），每次 latch 时 += 1
bool LoopRepFold::isCountingIV(PhiInst *phi, const Loop &loop) {
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
    if (isCountingIV(phi0, loop)) {
        r_phi = phi0; total_phi = phi1;
    } else if (isCountingIV(phi1, loop)) {
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
        else if (bb == loop.latch) total_latch = total_phi->get_operand(i);
    }
    if (!total_init || !total_latch) return false;
    if (!isLoopInvariant(total_init, loop.blocks)) return false;

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

void LoopRepFold::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return;

    auto rpo = computeRPO(func);
    computeDominators(rpo);
    auto loops = findLoops(func);

    // 优先处理小循环（内层），但对于本 pass，外层 r-loop 体积最大 → 按 blocks 数降序
    std::sort(loops.begin(), loops.end(),
              [](const Loop &a, const Loop &b) { return a.blocks.size() > b.blocks.size(); });

    for (auto &loop : loops)
        tryFold(loop, func->parent_);
}

void LoopRepFold::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}
