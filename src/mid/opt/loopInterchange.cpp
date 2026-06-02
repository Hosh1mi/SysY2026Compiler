#include "../../include/mid/opt/loopInterchange.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>

// ── 入口 ──────────────────────────────────────────────────────────────────

void LoopInterchange::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}

void LoopInterchange::runOnFunction(Function *func) {
    LoopInfo LI;
    LI.analyze(func);
    if (LI.allLoops().empty()) return;

    AffineAnalysis     AA(LI);
    DependenceAnalysis DA(LI, AA);

    // 枚举所有 loop 当 innermost(k)：分析→合法→有益→变换。
    // 一次只处理一组，因为变换会改 CFG；如需再扫描则下一轮 invocation。
    for (auto &k_loop_ptr : LI.allLoops()) {
        Loop *k_loop = k_loop_ptr.get();
        if (!k_loop->children.empty()) continue;          // 必须是最内
        MatmulInfo info{};
        if (!detectMatmul(k_loop, LI, AA, info)) continue;
        if (!isLegalAndProfitable(info, DA)) continue;
        if (apply(info, func->parent_)) return;
    }
}

// ── 按数组维度按需创建 temp buffer ────────────────────────────────────────
// 每个 size 一份全局 `@__mm_tmp_<size>`，重复匹配相同 size 时复用。

GlobalVariable *LoopInterchange::getOrCreateTempBuffer(Module *module, int size) {
    auto it = temp_buf_.find(size);
    if (it != temp_buf_.end()) return it->second;
    std::string name = "__mm_tmp_" + std::to_string(size);
    for (auto *gv : module->global_list_) {
        if (gv->name_ == name) {
            temp_buf_[size] = gv;
            return gv;
        }
    }
    auto *i32 = module->int32_ty_;
    auto *arr = module->get_array_type(i32, size);
    auto *gv  = new GlobalVariable(name, module, arr, false, new ConstantZero(arr));
    temp_buf_[size] = gv;
    return gv;
}

// ── 检测：基于 LoopInfo + AffineAnalysis ──────────────────────────────────

bool LoopInterchange::detectMatmul(Loop *k_loop, LoopInfo &LI,
                                   AffineAnalysis &AA, MatmulInfo &out) {
    // 1. 三层嵌套：k.parent = j，j.parent = i
    Loop *j_loop = k_loop->parent;
    if (!j_loop) return false;
    Loop *i_loop = j_loop->parent;
    if (!i_loop) return false;
    if (!k_loop->hasCanonicalIV() || !j_loop->hasCanonicalIV() || !i_loop->hasCanonicalIV())
        return false;

    PhiInst *k_phi = k_loop->canonicalIV;
    PhiInst *j_phi = j_loop->canonicalIV;
    PhiInst *i_phi = i_loop->canonicalIV;
    Value   *bound = k_loop->tripCount;
    if (bound != j_loop->tripCount || bound != i_loop->tripCount) return false;

    // 2. k_loop 必须有 preheader + 单 latch
    if (!k_loop->preheader || !k_loop->singleLatch() || !k_loop->singleExit()) return false;

    // 3. k_loop header 中除了 canonical IV 还得有一个 sum 累加器 phi
    PhiInst *sum_phi = nullptr;
    int      phi_cnt = 0;
    for (auto *inst : k_loop->header->instr_list_) {
        if (!inst->is_phi()) break;
        phi_cnt++;
        auto *p = static_cast<PhiInst *>(inst);
        if (p != k_phi) sum_phi = p;
    }
    if (phi_cnt != 2 || !sum_phi || sum_phi->type_->tid_ != Type::IntegerTyID)
        return false;
    if (sum_phi->num_ops_ != 4) return false;

    // 4. sum init = 0（来自 preheader），latch 值 = add(sum_phi, prod)，prod = mul(load, load)
    Value *sum_init = nullptr, *sum_latch = nullptr;
    for (unsigned i = 0; i < sum_phi->num_ops_; i += 2) {
        auto *src = static_cast<BasicBlock *>(sum_phi->get_operand(i + 1));
        if (src == k_loop->preheader) sum_init = sum_phi->get_operand(i);
        else if (src == k_loop->singleLatch()) sum_latch = sum_phi->get_operand(i);
    }
    if (!sum_init || !sum_latch) return false;
    auto *ci_init = dynamic_cast<ConstantInt *>(sum_init);
    if (!ci_init || ci_init->value_ != 0) return false;

    auto *add = dynamic_cast<BinaryInst *>(sum_latch);
    if (!add || !add->is_add()) return false;
    Value *prod_v = (add->get_operand(0) == sum_phi) ? add->get_operand(1)
                  : (add->get_operand(1) == sum_phi) ? add->get_operand(0)
                  : nullptr;
    if (!prod_v) return false;
    auto *mul = dynamic_cast<BinaryInst *>(prod_v);
    if (!mul || !mul->is_mul()) return false;

    auto *load1 = dynamic_cast<LoadInst *>(mul->get_operand(0));
    auto *load2 = dynamic_cast<LoadInst *>(mul->get_operand(1));
    if (!load1 || !load2) return false;

    auto *gep1 = dynamic_cast<GetElementPtrInst *>(load1->get_operand(0));
    auto *gep2 = dynamic_cast<GetElementPtrInst *>(load2->get_operand(0));
    if (!gep1 || !gep2) return false;
    if (gep1->num_ops_ != 4 || gep2->num_ops_ != 4) return false;

    // 5. 用 AffineAnalysis 判断两个 GEP 的索引形态
    // 期望：一个 GEP 索引仿射形式是 [0, i_phi, k_phi]，另一个是 [0, k_phi, j_phi]
    auto match_form = [&](GetElementPtrInst *gep, PhiInst *outer, PhiInst *inner) -> bool {
        AffineExpr e1 = AA.analyze(gep->get_operand(1));
        AffineExpr e2 = AA.analyze(gep->get_operand(2));
        AffineExpr e3 = AA.analyze(gep->get_operand(3));
        return e1.isZero()
            && e2.valid && e2.constant == 0 && e2.coeffs.size() == 1 && e2.coeffOf(outer) == 1
            && e3.valid && e3.constant == 0 && e3.coeffs.size() == 1 && e3.coeffOf(inner) == 1;
    };

    GetElementPtrInst *gep_ik = nullptr, *gep_kj = nullptr;
    LoadInst          *load_ik = nullptr, *load_kj = nullptr;
    if (match_form(gep1, i_phi, k_phi) && match_form(gep2, k_phi, j_phi)) {
        gep_ik = gep1; gep_kj = gep2; load_ik = load1; load_kj = load2;
    } else if (match_form(gep2, i_phi, k_phi) && match_form(gep1, k_phi, j_phi)) {
        gep_ik = gep2; gep_kj = gep1; load_ik = load2; load_kj = load1;
    } else {
        return false;
    }

    // 基址必须是 loop-invariant（全局或参数）
    Value *base_ik = gep_ik->get_operand(0);
    Value *base_kj = gep_kj->get_operand(0);
    if (!dynamic_cast<GlobalVariable *>(base_ik) && !dynamic_cast<Argument *>(base_ik)) return false;
    if (!dynamic_cast<GlobalVariable *>(base_kj) && !dynamic_cast<Argument *>(base_kj)) return false;

    // 从 GEP 基址 `[N x [M x i32]]*` 提取 M（j 的上界，也是 temp buffer 需要的大小）
    auto inner_dim_of = [](Value *base) -> int {
        auto *ptr = dynamic_cast<PointerType *>(base->type_);
        if (!ptr) return -1;
        auto *outer = dynamic_cast<ArrayType *>(ptr->contained_);
        if (!outer) return -1;
        auto *inner = dynamic_cast<ArrayType *>(outer->contained_);
        if (!inner) return -1;
        return (int)inner->num_elements_;
    };
    int d_kj = inner_dim_of(base_kj);
    if (d_kj <= 0) return false;

    // 6. k_loop 内除了这两个 load 不能有其它 store/call/load
    int load_cnt = 0;
    for (auto *bb : k_loop->blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            if (inst->is_call())  return false;
            if (inst->is_load())  load_cnt++;
        }
    }
    if (load_cnt != 2) return false;

    // 7. k_loop.exit (= 单 exit) 必须有 store sum_phi → D[i][j]
    BasicBlock *k_exit = k_loop->singleExit();
    StoreInst  *store_inst = nullptr;
    for (auto *inst : k_exit->instr_list_) {
        if (inst->is_store()) {
            if (store_inst) return false;
            store_inst = static_cast<StoreInst *>(inst);
        }
        if (inst->is_call()) return false;
    }
    if (!store_inst) return false;
    if (store_inst->get_operand(0) != sum_phi) return false;

    auto *gep_store = dynamic_cast<GetElementPtrInst *>(store_inst->get_operand(1));
    if (!gep_store || gep_store->num_ops_ != 4) return false;
    {
        AffineExpr e1 = AA.analyze(gep_store->get_operand(1));
        AffineExpr e2 = AA.analyze(gep_store->get_operand(2));
        AffineExpr e3 = AA.analyze(gep_store->get_operand(3));
        if (!(e1.isZero()
              && e2.valid && e2.constant == 0 && e2.coeffs.size() == 1 && e2.coeffOf(i_phi) == 1
              && e3.valid && e3.constant == 0 && e3.coeffs.size() == 1 && e3.coeffOf(j_phi) == 1))
            return false;
    }
    Value *base_store = gep_store->get_operand(0);
    if (!dynamic_cast<GlobalVariable *>(base_store) && !dynamic_cast<Argument *>(base_store)) return false;
    int d_st = inner_dim_of(base_store);
    if (d_st <= 0 || d_st != d_kj) return false;   // 写回数组的内维必须和读入数组一致

    // 8. j_loop 内（除 k_loop 外）不能有其它 store/call
    for (auto *bb : j_loop->blocks) {
        if (k_loop->blocks.count(bb)) continue;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store() && inst != store_inst) return false;
            if (inst->is_call()) return false;
        }
    }

    out.i_loop     = i_loop;
    out.j_loop     = j_loop;
    out.k_loop     = k_loop;
    out.sum_phi    = sum_phi;
    out.sum_add    = add;
    out.prod_mul   = mul;
    out.load_ik    = load_ik;
    out.load_kj    = load_kj;
    out.gep_ik     = gep_ik;
    out.gep_kj     = gep_kj;
    out.base_ik    = base_ik;
    out.base_kj    = base_kj;
    out.store_inst = store_inst;
    out.gep_store  = gep_store;
    out.base_store = base_store;
    out.bound      = bound;
    out.inner_dim  = d_kj;
    return true;
}

// ── 合法 + 有益 ──────────────────────────────────────────────────────────

bool LoopInterchange::isLegalAndProfitable(const MatmulInfo &info,
                                           DependenceAnalysis &DA) {
    // 合法性：load_kj（列访问）和 store（写回）之间在 j-k 上不能形成反向依赖。
    // 我们简化判断：j-k 两层之间没有跨循环的真依赖（仅有 reduction，scalar expansion 后消除）。
    std::vector<Instruction *> accesses = {info.load_ik, info.load_kj, info.store_inst};
    if (!DA.isInterchangeLegal(info.j_loop, info.k_loop, accesses)) return false;

    // 收益：load_kj 的内层 IV 系数（在 k_loop 中）对应 row 还是 col：
    // 当前内层 IV = k_phi，gep_kj 形如 A[k][j]，在 k 上 stride = 数组 inner_dim（列访问，坏）
    // 交换后内层变 j_phi，stride = 1（行访问，好）→ 收益正向，直接判定有益
    return true;
}

// ── 变换（沿用旧版 CFG 构造逻辑）─────────────────────────────────────────

bool LoopInterchange::apply(const MatmulInfo &info, Module *module) {
    Function *func   = info.i_loop->header->parent_;
    auto     *i32    = module->int32_ty_;
    Value    *bound  = info.bound;
    PhiInst  *i_phi  = info.i_loop->canonicalIV;

    BasicBlock *j_preheader = info.j_loop->preheader;
    BasicBlock *j_exit_old  = info.j_loop->singleExit();
    if (!j_preheader || !j_exit_old) return false;

    auto *tmp_buf = getOrCreateTempBuffer(module, info.inner_dim);

    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 1000); };
    auto newBB = [&](const std::string &tag) {
        return new BasicBlock(module, "li_" + tag + "_" + bbNum(), func);
    };

    BasicBlock *clr_h = newBB("clrH"), *clr_b = newBB("clrB"), *clr_l = newBB("clrL");
    BasicBlock *nk_h  = newBB("nkH"),  *nk_b  = newBB("nkB"),  *nk_l  = newBB("nkL");
    BasicBlock *nj_h  = newBB("njH"),  *nj_b  = newBB("njB"),  *nj_l  = newBB("njL");
    BasicBlock *st_h  = newBB("stH"),  *st_b  = newBB("stB"),  *st_l  = newBB("stL");

    auto *const0 = new ConstantInt(i32, 0);
    auto *const1 = new ConstantInt(i32, 1);

    // ── clear loop: for(jc=0;jc<bound;jc++) tmp[jc]=0 ──
    PhiInst *clr_jc = PhiInst::create_phi(i32, clr_h);
    clr_jc->add_phi_pair_operand(const0, j_preheader);
    clr_h->add_instruction_front(clr_jc);
    auto *clr_cmp = new ICmpInst(ICmpInst::ICMP_SLT, clr_jc, bound, clr_h);
    new BranchInst(clr_cmp, clr_b, nk_h, clr_h);

    auto *clr_gep = new GetElementPtrInst(tmp_buf, {const0, clr_jc}, clr_b);
    new StoreInst(const0, clr_gep, clr_b);
    new BranchInst(clr_l, clr_b);

    auto *clr_inc = new BinaryInst(i32, Instruction::Add, clr_jc, const1, clr_l);
    clr_jc->add_phi_pair_operand(clr_inc, clr_l);
    new BranchInst(clr_h, clr_l);

    // ── new k loop: for(nk=0;nk<bound;nk++) ──
    PhiInst *nk_iv = PhiInst::create_phi(i32, nk_h);
    nk_iv->add_phi_pair_operand(const0, clr_h);
    nk_h->add_instruction_front(nk_iv);
    auto *nk_cmp = new ICmpInst(ICmpInst::ICMP_SLT, nk_iv, bound, nk_h);
    new BranchInst(nk_cmp, nk_b, st_h, nk_h);

    // c_ik = X[i][nk]
    auto *gep_c = new GetElementPtrInst(info.base_ik, {const0, i_phi, nk_iv}, nk_b);
    auto *c_ik  = new LoadInst(gep_c, nk_b);
    new BranchInst(nj_h, nk_b);

    // ── new inner j loop: for(nj=0;nj<bound;nj++) tmp[nj] += c_ik * A[nk][nj] ──
    PhiInst *nj_iv = PhiInst::create_phi(i32, nj_h);
    nj_iv->add_phi_pair_operand(const0, nk_b);
    nj_h->add_instruction_front(nj_iv);
    auto *nj_cmp = new ICmpInst(ICmpInst::ICMP_SLT, nj_iv, bound, nj_h);
    new BranchInst(nj_cmp, nj_b, nk_l, nj_h);

    auto *acc_gep = new GetElementPtrInst(tmp_buf, {const0, nj_iv}, nj_b);
    auto *acc_old = new LoadInst(acc_gep, nj_b);
    auto *gep_a   = new GetElementPtrInst(info.base_kj, {const0, nk_iv, nj_iv}, nj_b);
    auto *a_val   = new LoadInst(gep_a, nj_b);
    auto *prod    = new BinaryInst(i32, Instruction::Mul, c_ik, a_val, nj_b);
    auto *acc_new = new BinaryInst(i32, Instruction::Add, acc_old, prod, nj_b);
    new StoreInst(acc_new, acc_gep, nj_b);
    new BranchInst(nj_l, nj_b);

    auto *nj_inc = new BinaryInst(i32, Instruction::Add, nj_iv, const1, nj_l);
    nj_iv->add_phi_pair_operand(nj_inc, nj_l);
    new BranchInst(nj_h, nj_l);

    auto *nk_inc = new BinaryInst(i32, Instruction::Add, nk_iv, const1, nk_l);
    nk_iv->add_phi_pair_operand(nk_inc, nk_l);
    new BranchInst(nk_h, nk_l);

    // ── store-back loop: for(sj=0;sj<bound;sj++) D[i][sj] = tmp[sj] ──
    PhiInst *st_iv = PhiInst::create_phi(i32, st_h);
    st_iv->add_phi_pair_operand(const0, nk_h);
    st_h->add_instruction_front(st_iv);
    auto *st_cmp = new ICmpInst(ICmpInst::ICMP_SLT, st_iv, bound, st_h);
    new BranchInst(st_cmp, st_b, j_exit_old, st_h);

    auto *st_ld_gep  = new GetElementPtrInst(tmp_buf, {const0, st_iv}, st_b);
    auto *st_ld_val  = new LoadInst(st_ld_gep, st_b);
    auto *st_dst_gep = new GetElementPtrInst(info.base_store, {const0, i_phi, st_iv}, st_b);
    new StoreInst(st_ld_val, st_dst_gep, st_b);
    new BranchInst(st_l, st_b);

    auto *st_inc = new BinaryInst(i32, Instruction::Add, st_iv, const1, st_l);
    st_iv->add_phi_pair_operand(st_inc, st_l);
    new BranchInst(st_h, st_l);

    // ── CFG 重连：j_preheader → clr_h ──
    {
        auto *term = j_preheader->get_terminator();
        if (!term || !term->is_br() || term->num_ops_ != 1) return false;
        term->set_operand(0, clr_h);
        j_preheader->remove_succ_basic_block(info.j_loop->header);
        info.j_loop->header->remove_pre_basic_block(j_preheader);
        j_preheader->add_succ_basic_block(clr_h);
        clr_h->add_pre_basic_block(j_preheader);
    }
    j_exit_old->remove_pre_basic_block(info.j_loop->header);

    return true;
}
