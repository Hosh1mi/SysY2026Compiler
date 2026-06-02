#include "../../include/mid/opt/loopInterchange.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include <algorithm>
#include <functional>
#include <queue>
#include <cstdio>

static constexpr int TEMP_BUF_SIZE = 1024;

// ── CFG / Dominator helpers ─────────────────────────────────────────────────

std::vector<BasicBlock *> LoopInterchange::computeRPO(Function *func) {
    std::vector<BasicBlock *> postorder;
    std::set<BasicBlock *> visited;
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ)) dfs(succ);
        postorder.push_back(bb);
    };
    if (!func->basic_blocks_.empty()) dfs(func->basic_blocks_[0]);
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
}

void LoopInterchange::computeDominators(const std::vector<BasicBlock *> &rpo) {
    idom_.clear();
    rpoIdx_.clear();
    if (rpo.empty()) return;
    BasicBlock *entry = rpo[0];
    for (int i = 0; i < (int)rpo.size(); i++) rpoIdx_[rpo[i]] = i;
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

BasicBlock *LoopInterchange::intersect(BasicBlock *a, BasicBlock *b) {
    while (a != b) {
        while (rpoIdx_[a] > rpoIdx_[b]) a = idom_[a];
        while (rpoIdx_[b] > rpoIdx_[a]) b = idom_[b];
    }
    return a;
}

bool LoopInterchange::dominates(BasicBlock *a, BasicBlock *b) {
    while (b != idom_[b]) {
        if (b == a) return true;
        b = idom_[b];
    }
    return b == a;
}

// ── Loop discovery ─────────────────────────────────────────────────────────

std::vector<LoopInterchange::Loop> LoopInterchange::findLoops(Function *func) {
    std::map<BasicBlock *, Loop> headerToLoop;
    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            if (!idom_.count(succ)) continue;
            if (!dominates(succ, bb)) continue;          // back edge bb→succ
            auto &loop = headerToLoop[succ];
            loop.header = succ;
            if (!loop.latch) loop.latch = bb;
            loop.blocks.insert(succ);
            std::queue<BasicBlock *> wl;
            wl.push(bb);
            while (!wl.empty()) {
                auto cur = wl.front(); wl.pop();
                if (!loop.blocks.insert(cur).second) continue;
                for (auto pred : cur->pre_bbs_)
                    if (!loop.blocks.count(pred)) wl.push(pred);
            }
        }
    }
    std::vector<Loop> loops;
    for (auto &kv : headerToLoop) {
        auto &loop = kv.second;
        BasicBlock *pre = nullptr;
        int ext = 0;
        for (auto pred : loop.header->pre_bbs_) {
            if (!loop.blocks.count(pred)) { pre = pred; ext++; }
        }
        loop.preheader = (ext == 1) ? pre : nullptr;
        loops.push_back(std::move(loop));
    }
    return loops;
}

// 检测形如 for(iv=0; iv<bound; iv++) 的规范循环：
//   header 有 1 个计数 phi（初始 0，latch 值 = phi+1）
//   header 的 terminator 是 icmp_slt(phi, bound) 的条件分支
bool LoopInterchange::analyzeCanonicalLoop(Loop &loop) {
    if (!loop.preheader || !loop.latch) return false;

    PhiInst *iv = nullptr;
    int phi_cnt = 0;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        phi_cnt++;
        if (!iv && inst->type_->tid_ == Type::IntegerTyID) {
            auto *phi = static_cast<PhiInst *>(inst);
            // init 0 from preheader, +1 from latch
            if (phi->num_ops_ != 4) continue;
            Value *pre_val = nullptr, *latch_val = nullptr;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto *src = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                if (src == loop.preheader) pre_val  = phi->get_operand(i);
                else if (src == loop.latch) latch_val = phi->get_operand(i);
            }
            if (!pre_val || !latch_val) continue;
            auto *ci = dynamic_cast<ConstantInt *>(pre_val);
            if (!ci || ci->value_ != 0) continue;
            auto *add = dynamic_cast<BinaryInst *>(latch_val);
            if (!add || !add->is_add()) continue;
            auto *op0 = add->get_operand(0);
            auto *op1 = add->get_operand(1);
            auto *c0 = dynamic_cast<ConstantInt *>(op0);
            auto *c1 = dynamic_cast<ConstantInt *>(op1);
            bool ok = (op0 == phi && c1 && c1->value_ == 1) ||
                      (op1 == phi && c0 && c0->value_ == 1);
            if (ok) iv = phi;
        }
    }
    if (!iv) return false;

    auto *term = loop.header->get_terminator();
    if (!term || !term->is_br() || term->num_ops_ != 3) return false;
    auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
    if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) return false;
    if (cmp->get_operand(0) != iv) return false;

    auto *body  = static_cast<BasicBlock *>(term->get_operand(1));
    auto *exit_ = static_cast<BasicBlock *>(term->get_operand(2));
    if (!loop.blocks.count(body))  return false;
    if (loop.blocks.count(exit_)) return false;

    loop.iv_phi     = iv;
    loop.bound      = cmp->get_operand(1);
    loop.body_entry = body;
    loop.exit       = exit_;
    return true;
}

// ── Temp buffer helper ─────────────────────────────────────────────────────

GlobalVariable *LoopInterchange::getOrCreateTempBuffer(Module *module) {
    if (temp_buf_) return temp_buf_;
    for (auto *gv : module->global_list_) {
        if (gv->name_ == "__mm_tmp") { temp_buf_ = gv; return temp_buf_; }
    }
    auto *i32 = module->int32_ty_;
    auto *arr = module->get_array_type(i32, TEMP_BUF_SIZE);
    temp_buf_ = new GlobalVariable("__mm_tmp", module, arr, /*is_const=*/false,
                                   new ConstantZero(arr));
    return temp_buf_;
}

// ── Matmul pattern detection + transformation ─────────────────────────────

bool LoopInterchange::tryInterchange(Loop &i_loop, Loop &j_loop, Loop &k_loop, Module *module) {
    // 0. 三层都得是规范的 for-loop
    if (!analyzeCanonicalLoop(i_loop) ||
        !analyzeCanonicalLoop(j_loop) ||
        !analyzeCanonicalLoop(k_loop)) return false;

    // 1. 三个循环的 bound 必须是同一个 loop-invariant value
    if (i_loop.bound != j_loop.bound || j_loop.bound != k_loop.bound) return false;

    // 2. 检查嵌套关系：i ⊃ j ⊃ k
    if (!j_loop.blocks.count(k_loop.header)) return false;
    if (!i_loop.blocks.count(j_loop.header)) return false;

    // 3. k_loop header 应该恰好有 2 个 phi：k_phi + sum_phi
    PhiInst *k_phi = k_loop.iv_phi;
    PhiInst *sum_phi = nullptr;
    int phi_cnt = 0;
    for (auto *inst : k_loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        phi_cnt++;
        auto *p = static_cast<PhiInst *>(inst);
        if (p != k_phi) sum_phi = p;
    }
    if (phi_cnt != 2 || !sum_phi) return false;
    if (sum_phi->type_->tid_ != Type::IntegerTyID) return false;
    if (sum_phi->num_ops_ != 4) return false;

    // sum init from preheader = 0, latch = add(sum_phi, product)
    Value *sum_init = nullptr, *sum_latch = nullptr;
    for (unsigned i = 0; i < sum_phi->num_ops_; i += 2) {
        auto *src = static_cast<BasicBlock *>(sum_phi->get_operand(i + 1));
        if (src == k_loop.preheader) sum_init  = sum_phi->get_operand(i);
        else if (src == k_loop.latch) sum_latch = sum_phi->get_operand(i);
    }
    if (!sum_init || !sum_latch) return false;
    auto *ci_init = dynamic_cast<ConstantInt *>(sum_init);
    if (!ci_init || ci_init->value_ != 0) return false;

    auto *add = dynamic_cast<BinaryInst *>(sum_latch);
    if (!add || !add->is_add()) return false;
    Value *prod_v = nullptr;
    if (add->get_operand(0) == sum_phi) prod_v = add->get_operand(1);
    else if (add->get_operand(1) == sum_phi) prod_v = add->get_operand(0);
    if (!prod_v) return false;
    auto *mul = dynamic_cast<BinaryInst *>(prod_v);
    if (!mul || !mul->is_mul()) return false;

    auto *load1 = dynamic_cast<LoadInst *>(mul->get_operand(0));
    auto *load2 = dynamic_cast<LoadInst *>(mul->get_operand(1));
    if (!load1 || !load2) return false;

    auto *gep1 = dynamic_cast<GetElementPtrInst *>(load1->get_operand(0));
    auto *gep2 = dynamic_cast<GetElementPtrInst *>(load2->get_operand(0));
    if (!gep1 || !gep2) return false;

    // 期望两个 GEP 形如 gep base, 0, idx1, idx2，且都是 3 个操作数（base + 3 indices）
    if (gep1->num_ops_ != 4 || gep2->num_ops_ != 4) return false;

    auto *zero1 = dynamic_cast<ConstantInt *>(gep1->get_operand(1));
    auto *zero2 = dynamic_cast<ConstantInt *>(gep2->get_operand(1));
    if (!zero1 || zero1->value_ != 0 || !zero2 || zero2->value_ != 0) return false;

    // 找哪个 GEP 是 X[i][k]（第三 index = k_phi），哪个是 Y[k][j]（第二 index = k_phi）
    GetElementPtrInst *gep_ik = nullptr;  // X[outer_i][k]，第二 index = i_phi，第三 index = k_phi
    GetElementPtrInst *gep_kj = nullptr;  // Y[k][outer_j]，第二 index = k_phi，第三 index = j_phi
    PhiInst *i_phi = i_loop.iv_phi;
    PhiInst *j_phi = j_loop.iv_phi;

    auto check_ik = [&](GetElementPtrInst *gep) -> bool {
        return gep->get_operand(2) == i_phi && gep->get_operand(3) == k_phi;
    };
    auto check_kj = [&](GetElementPtrInst *gep) -> bool {
        return gep->get_operand(2) == k_phi && gep->get_operand(3) == j_phi;
    };

    if (check_ik(gep1) && check_kj(gep2)) { gep_ik = gep1; gep_kj = gep2; }
    else if (check_ik(gep2) && check_kj(gep1)) { gep_ik = gep2; gep_kj = gep1; }
    else return false;

    // 基址必须是全局/参数（loop invariant 的 base 指针）
    auto base_ik = gep_ik->get_operand(0);
    auto base_kj = gep_kj->get_operand(0);
    if (!dynamic_cast<GlobalVariable *>(base_ik) && !dynamic_cast<Argument *>(base_ik)) return false;
    if (!dynamic_cast<GlobalVariable *>(base_kj) && !dynamic_cast<Argument *>(base_kj)) return false;

    // 4. 检查 k-loop 内只有这两个 load，且无 store / call
    int load_cnt = 0;
    for (auto *bb : k_loop.blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            if (inst->is_call())  return false;
            if (inst->is_load())  load_cnt++;
        }
    }
    if (load_cnt != 2) return false;

    // 5. 找 k_loop.exit 中的 store sum -> D[i][j]
    //    exit 块需要恰好一个 store，目标是 gep base_D, 0, i_phi, j_phi
    BasicBlock *k_exit = k_loop.exit;
    StoreInst *store_inst = nullptr;
    for (auto *inst : k_exit->instr_list_) {
        if (inst->is_store()) {
            if (store_inst) return false;
            store_inst = static_cast<StoreInst *>(inst);
        }
        if (inst->is_call()) return false;
    }
    if (!store_inst) return false;

    // store value 必须是 sum_phi（或来自 sum_phi 的 lcssa-like phi，本项目通常直接是 sum_phi）
    if (store_inst->get_operand(0) != sum_phi) return false;

    auto *gep_store = dynamic_cast<GetElementPtrInst *>(store_inst->get_operand(1));
    if (!gep_store || gep_store->num_ops_ != 4) return false;
    auto *zs = dynamic_cast<ConstantInt *>(gep_store->get_operand(1));
    if (!zs || zs->value_ != 0) return false;
    if (gep_store->get_operand(2) != i_phi || gep_store->get_operand(3) != j_phi) return false;
    auto base_store = gep_store->get_operand(0);
    if (!dynamic_cast<GlobalVariable *>(base_store) && !dynamic_cast<Argument *>(base_store)) return false;

    // 6. j-loop 体内除了 k-loop 和这个 store 不能有别的 store/call
    for (auto *bb : j_loop.blocks) {
        if (k_loop.blocks.count(bb)) continue;     // k-loop 内部已检查
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store() && inst != store_inst) return false;
            if (inst->is_call()) return false;
        }
    }

    // ────────────────────── 变换开始 ──────────────────────
    {
        FILE *f = fopen("/tmp/interchange_debug.txt", "a");
        if (f) {
            fprintf(f, "[LoopInterchange] matmul matched: i=%s j=%s k=%s in %s\n",
                    i_loop.header->name_.c_str(), j_loop.header->name_.c_str(),
                    k_loop.header->name_.c_str(), i_loop.header->parent_->name_.c_str());
            fclose(f);
        }
    }

    Function *func = i_loop.header->parent_;
    Module   *mod  = module;
    auto     *i32  = mod->int32_ty_;
    auto     *i32p = mod->get_pointer_type(i32);
    auto     *bound = i_loop.bound;

    // 临时 buffer 全局数组的 i32* 起始指针：gep @__mm_tmp, 0, 0
    auto *tmp_buf = getOrCreateTempBuffer(mod);

    // 找 j_loop 在 i_loop 中的入口块（即 i_header 的 body_entry 一路下来到 j_loop.preheader）。
    // 简化：j_loop.preheader 就是接入点。
    BasicBlock *j_preheader = j_loop.preheader;
    BasicBlock *j_exit_old  = j_loop.exit;     // 原 j-loop 的出口，应衔接到 i_latch 方向

    if (!j_preheader || !j_exit_old) return false;

    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 1000); };
    auto newBB = [&](const std::string &tag) {
        return new BasicBlock(mod, "li_" + tag + "_" + bbNum(), func);
    };

    // 创建新基本块（共 12 个）
    BasicBlock *clr_h   = newBB("clrH");   // clear loop header
    BasicBlock *clr_b   = newBB("clrB");   // clear loop body
    BasicBlock *clr_l   = newBB("clrL");   // clear loop latch
    BasicBlock *nk_h    = newBB("nkH");    // new k header
    BasicBlock *nk_b    = newBB("nkB");    // new k body (loads c_ik)
    BasicBlock *nj_h    = newBB("njH");    // new j-inner header
    BasicBlock *nj_b    = newBB("njB");    // new j-inner body
    BasicBlock *nj_l    = newBB("njL");    // new j-inner latch
    BasicBlock *nk_l    = newBB("nkL");    // new k latch
    BasicBlock *st_h    = newBB("stH");    // store loop header
    BasicBlock *st_b    = newBB("stB");    // store loop body
    BasicBlock *st_l    = newBB("stL");    // store loop latch

    auto *const0 = new ConstantInt(i32, 0);
    auto *const1 = new ConstantInt(i32, 1);

    // ─── clr_h: 清零循环头  for(jc=0; jc<bound; jc++) ─────────────
    PhiInst *clr_jc = PhiInst::create_phi(i32, clr_h);
    clr_jc->add_phi_pair_operand(const0, j_preheader);
    clr_h->add_instruction_front(clr_jc);
    auto *clr_cmp = new ICmpInst(ICmpInst::ICMP_SLT, clr_jc, bound, clr_h);
    new BranchInst(clr_cmp, clr_b, nk_h, clr_h);

    // ─── clr_b: __mm_tmp[jc] = 0 ──────────────────────────────────
    auto *tmp_gep_clr = new GetElementPtrInst(tmp_buf, {const0, clr_jc}, clr_b);
    new StoreInst(const0, tmp_gep_clr, clr_b);
    new BranchInst(clr_l, clr_b);

    // ─── clr_l: jc + 1 ────────────────────────────────────────────
    auto *clr_inc = new BinaryInst(i32, Instruction::Add, clr_jc, const1, clr_l);
    clr_jc->add_phi_pair_operand(clr_inc, clr_l);
    new BranchInst(clr_h, clr_l);

    // ─── nk_h: 新 k 循环头  for(nk=0; nk<bound; nk++) ─────────────
    PhiInst *nk_iv = PhiInst::create_phi(i32, nk_h);
    nk_iv->add_phi_pair_operand(const0, clr_h);
    nk_h->add_instruction_front(nk_iv);
    auto *nk_cmp = new ICmpInst(ICmpInst::ICMP_SLT, nk_iv, bound, nk_h);
    new BranchInst(nk_cmp, nk_b, st_h, nk_h);

    // ─── nk_b: c_ik = X[i][nk] ────────────────────────────────────
    auto *gep_c = new GetElementPtrInst(base_ik, {const0, i_phi, nk_iv}, nk_b);
    auto *c_ik  = new LoadInst(gep_c, nk_b);
    new BranchInst(nj_h, nk_b);

    // ─── nj_h: 新 j 内层  for(nj=0; nj<bound; nj++) ───────────────
    PhiInst *nj_iv = PhiInst::create_phi(i32, nj_h);
    nj_iv->add_phi_pair_operand(const0, nk_b);
    nj_h->add_instruction_front(nj_iv);
    auto *nj_cmp = new ICmpInst(ICmpInst::ICMP_SLT, nj_iv, bound, nj_h);
    new BranchInst(nj_cmp, nj_b, nk_l, nj_h);

    // ─── nj_b: __mm_tmp[nj] += c_ik * Y[nk][nj] ───────────────────
    auto *tmp_gep_acc = new GetElementPtrInst(tmp_buf, {const0, nj_iv}, nj_b);
    auto *tmp_old     = new LoadInst(tmp_gep_acc, nj_b);
    auto *gep_a       = new GetElementPtrInst(base_kj, {const0, nk_iv, nj_iv}, nj_b);
    auto *a_val       = new LoadInst(gep_a, nj_b);
    auto *prod        = new BinaryInst(i32, Instruction::Mul, c_ik, a_val, nj_b);
    auto *tmp_new     = new BinaryInst(i32, Instruction::Add, tmp_old, prod, nj_b);
    new StoreInst(tmp_new, tmp_gep_acc, nj_b);
    new BranchInst(nj_l, nj_b);

    // ─── nj_l: nj+1 ───────────────────────────────────────────────
    auto *nj_inc = new BinaryInst(i32, Instruction::Add, nj_iv, const1, nj_l);
    nj_iv->add_phi_pair_operand(nj_inc, nj_l);
    new BranchInst(nj_h, nj_l);

    // ─── nk_l: nk+1 ───────────────────────────────────────────────
    auto *nk_inc = new BinaryInst(i32, Instruction::Add, nk_iv, const1, nk_l);
    nk_iv->add_phi_pair_operand(nk_inc, nk_l);
    new BranchInst(nk_h, nk_l);

    // ─── st_h: 写回 for(sj=0; sj<bound; sj++) ─────────────────────
    PhiInst *st_iv = PhiInst::create_phi(i32, st_h);
    st_iv->add_phi_pair_operand(const0, nk_h);
    st_h->add_instruction_front(st_iv);
    auto *st_cmp = new ICmpInst(ICmpInst::ICMP_SLT, st_iv, bound, st_h);
    new BranchInst(st_cmp, st_b, j_exit_old, st_h);

    // ─── st_b: D[i][sj] = __mm_tmp[sj] ────────────────────────────
    auto *tmp_gep_ld = new GetElementPtrInst(tmp_buf, {const0, st_iv}, st_b);
    auto *tmp_val    = new LoadInst(tmp_gep_ld, st_b);
    auto *gep_d_new  = new GetElementPtrInst(base_store, {const0, i_phi, st_iv}, st_b);
    new StoreInst(tmp_val, gep_d_new, st_b);
    new BranchInst(st_l, st_b);

    // ─── st_l: sj+1 ───────────────────────────────────────────────
    auto *st_inc = new BinaryInst(i32, Instruction::Add, st_iv, const1, st_l);
    st_iv->add_phi_pair_operand(st_inc, st_l);
    new BranchInst(st_h, st_l);

    // ─── 重连 CFG：j_preheader → clr_h（替换原 → j_header）─────────
    {
        auto *term = j_preheader->get_terminator();
        // j_preheader 只有一条无条件跳转到 j_header
        if (!term || !term->is_br() || term->num_ops_ != 1) return false;
        term->set_operand(0, clr_h);
        j_preheader->remove_succ_basic_block(j_loop.header);
        j_loop.header->remove_pre_basic_block(j_preheader);
        j_preheader->add_succ_basic_block(clr_h);
        clr_h->add_pre_basic_block(j_preheader);
    }

    // st_h → j_exit_old：把 j_exit_old 中原来从 j_header 来的 pre 关系迁移到 st_h
    {
        // j_loop.header 不再是 j_exit_old 的前驱
        j_exit_old->remove_pre_basic_block(j_loop.header);
        // st_h 已经在 BranchInst 创建时被加到 succ/pre
    }

    return true;
}

// ── 函数级入口 ──────────────────────────────────────────────────────────────

void LoopInterchange::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return;

    auto rpo = computeRPO(func);
    computeDominators(rpo);
    auto loops_vec = findLoops(func);

    // 建立 header→loop 映射 + 父子嵌套关系
    std::map<BasicBlock *, Loop *> headerToLoop;
    for (auto &l : loops_vec) headerToLoop[l.header] = &l;

    // 枚举所有三层嵌套：找最内的 k-loop，向外找 j-loop（包含 k.header），再向外找 i-loop（包含 j.header）
    for (auto &k_loop : loops_vec) {
        // 找 j_loop: 包含 k_loop.header 的最小循环（不等于 k_loop 自身）
        Loop *j_cand = nullptr;
        for (auto &cand : loops_vec) {
            if (&cand == &k_loop) continue;
            if (!cand.blocks.count(k_loop.header)) continue;
            if (!j_cand || cand.blocks.size() < j_cand->blocks.size()) j_cand = &cand;
        }
        if (!j_cand) continue;
        Loop *i_cand = nullptr;
        for (auto &cand : loops_vec) {
            if (&cand == &k_loop || &cand == j_cand) continue;
            if (!cand.blocks.count(j_cand->header)) continue;
            if (!i_cand || cand.blocks.size() < i_cand->blocks.size()) i_cand = &cand;
        }
        if (!i_cand) continue;

        if (tryInterchange(*i_cand, *j_cand, k_loop, func->parent_)) {
            // 一次只交换一组（变换会改 CFG，再扫描需要重算）
            return;
        }
    }
}

void LoopInterchange::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}
