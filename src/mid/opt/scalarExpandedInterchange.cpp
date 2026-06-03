#include "../../include/mid/opt/scalarExpandedInterchange.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <unordered_map>

// ── 入口 ──────────────────────────────────────────────────────────────────

void ScalarExpandedInterchange::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}

void ScalarExpandedInterchange::runOnFunction(Function *func) {
    // 一次成功变换会改 CFG，需重建 LoopInfo/AffineAnalysis/Dependence/Cost
    // 以便发现同一函数内的下一个 matmul。无变换时退出。
    for (int iter = 0; iter < 32; iter++) {
        LoopInfo LI;
        LI.analyze(func);
        if (LI.allLoops().empty()) return;

        AffineAnalysis     AA(LI);
        DependenceAnalysis DA(LI, AA);
        CostModel          CM(AA);

        bool changed = false;
        for (auto &L_ptr : LI.allLoops()) {
            Loop *L = L_ptr.get();
            if (!L->children.empty()) continue;           // 必须是最内
            ReductionNestInfo info{};
            if (!detectScalarExpandableReduction(L, LI, AA, info)) continue;
            if (!isLegalAndProfitable(info, DA, CM)) continue;
            if (apply(info, func->parent_)) {
                changed = true;
                break;                                    // CFG 已变，重新分析
            }
        }
        if (!changed) return;
    }
}

// ── 按数组维度按需创建 temp buffer ────────────────────────────────────────
// 每个 size 一份全局 `@__mm_tmp_<size>`，重复匹配相同 size 时复用。

GlobalVariable *ScalarExpandedInterchange::getOrCreateTempBuffer(Module *module, int size) {
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
//
// 通用要求（不写死层数）：
//   - L (= 候选 innermost) 有 canonicalIV，preheader、singleLatch、singleExit
//   - L.parent = P 存在且有 canonicalIV
//   - L.header 恰好 2 个 phi：L.IV + sum_phi
//   - sum_init 在 P-loop 外可见
//   - L body 全部访存是 load；GEP 4 操作数；base loop-invariant；
//     索引中出现的 IV 只能是 L 的祖先链上的 canonicalIV（任意层）
//   - L.singleExit 单 store: sum_phi → gep[base, 0, …, P.IV]，
//     即"最后一维"由 P.IV 索引，其余维必须不依赖 P.IV/L.IV
//   - P body 除 L 外没有别的 store/call

bool ScalarExpandedInterchange::detectScalarExpandableReduction(Loop *L, LoopInfo &LI,
                                                                 AffineAnalysis &AA,
                                                                 ReductionNestInfo &out) {
    Loop *P = L->parent;
    if (!P) return false;
    if (!L->hasCanonicalIV() || !P->hasCanonicalIV()) return false;

    PhiInst *L_iv    = L->canonicalIV;
    PhiInst *P_iv    = P->canonicalIV;
    Value   *L_bound = L->tripCount;
    Value   *P_bound = P->tripCount;

    if (!L->preheader || !L->singleLatch() || !L->singleExit()) return false;

    // header 必须恰好 2 个 phi：L_iv + sum_phi
    PhiInst *sum_phi = nullptr;
    int      phi_cnt = 0;
    for (auto *inst : L->header->instr_list_) {
        if (!inst->is_phi()) break;
        phi_cnt++;
        auto *p = static_cast<PhiInst *>(inst);
        if (p != L_iv) {
            if (sum_phi) return false;
            sum_phi = p;
        }
    }
    if (phi_cnt != 2 || !sum_phi) return false;
    if (sum_phi->type_->tid_ != Type::IntegerTyID) return false;
    if (sum_phi->num_ops_ != 4) return false;

    Value *sum_init = nullptr, *sum_latch = nullptr;
    for (unsigned i = 0; i < sum_phi->num_ops_; i += 2) {
        auto *src = static_cast<BasicBlock *>(sum_phi->get_operand(i + 1));
        if (src == L->preheader)         sum_init  = sum_phi->get_operand(i);
        else if (src == L->singleLatch()) sum_latch = sum_phi->get_operand(i);
    }
    if (!sum_init || !sum_latch) return false;

    auto availableOutsideP = [&](Value *v) -> bool {
        if (dynamic_cast<Constant *>(v))       return true;
        if (dynamic_cast<GlobalVariable *>(v)) return true;
        if (dynamic_cast<Argument *>(v))       return true;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst) return true;
        return !P->blocks.count(inst->parent_);
    };
    if (!availableOutsideP(sum_init)) return false;

    // 判断 v 是否是 L 的某层祖先（含 L 自身）的 canonicalIV
    auto isAncestorIV = [&](PhiInst *p) -> bool {
        for (Loop *anc = L; anc; anc = anc->parent) {
            if (anc->canonicalIV == p) return true;
        }
        return false;
    };

    auto inner_dim_of = [](Value *base) -> int {
        auto *ptr = dynamic_cast<PointerType *>(base->type_);
        if (!ptr) return -1;
        Type *t = ptr->contained_;
        int last = -1;
        while (auto *arr = dynamic_cast<ArrayType *>(t)) {
            last = (int)arr->num_elements_;
            t = arr->contained_;
        }
        return last;
    };

    // L body：禁止 store/call；GEP 索引必须仿射于祖先 IV 链；base loop-invariant
    std::vector<GetElementPtrInst *> body_geps;
    for (auto *bb : L->blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            if (inst->is_call())  return false;
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst)) {
                if (gep->num_ops_ < 2) return false;
                for (unsigned m = 1; m < gep->num_ops_; m++) {
                    AffineExpr e = AA.analyze(gep->get_operand(m));
                    if (!e.valid) return false;
                    for (auto &kv : e.coeffs) {
                        if (!isAncestorIV(kv.first)) return false;
                    }
                }
                Value *base = gep->get_operand(0);
                if (!dynamic_cast<GlobalVariable *>(base) && !dynamic_cast<Argument *>(base))
                    return false;
                body_geps.push_back(gep);
            }
        }
    }

    // L.singleExit 单 store: sum_phi → gep[base, 0, …, P.IV]
    BasicBlock *L_exit     = L->singleExit();
    StoreInst  *store_inst = nullptr;
    for (auto *inst : L_exit->instr_list_) {
        if (inst->is_store()) {
            if (store_inst) return false;
            store_inst = static_cast<StoreInst *>(inst);
        }
        if (inst->is_call()) return false;
    }
    if (!store_inst) return false;
    if (store_inst->get_operand(0) != sum_phi) return false;

    auto *gep_store = dynamic_cast<GetElementPtrInst *>(store_inst->get_operand(1));
    if (!gep_store || gep_store->num_ops_ < 2) return false;
    {
        // 首索引应为 0（消耗外层聚合）
        AffineExpr e0 = AA.analyze(gep_store->get_operand(1));
        if (!e0.isZero()) return false;
        // 末索引必须恰好是 P.IV（系数 1，常数 0）
        unsigned last = gep_store->num_ops_ - 1;
        AffineExpr eL = AA.analyze(gep_store->get_operand(last));
        if (!eL.valid || eL.constant != 0 || eL.coeffs.size() != 1 || eL.coeffOf(P_iv) != 1)
            return false;
        // 中间各索引：仿射，不依赖 P.IV 或 L.IV
        for (unsigned m = 2; m < last; m++) {
            AffineExpr em = AA.analyze(gep_store->get_operand(m));
            if (!em.valid) return false;
            if (em.coeffOf(P_iv) != 0) return false;
            if (em.coeffOf(L_iv) != 0) return false;
        }
    }
    Value *base_store = gep_store->get_operand(0);
    if (!dynamic_cast<GlobalVariable *>(base_store) && !dynamic_cast<Argument *>(base_store))
        return false;
    int d_st = inner_dim_of(base_store);
    if (d_st <= 0) return false;

    // P body 除 L 外没有别的 store/call
    for (auto *bb : P->blocks) {
        if (L->blocks.count(bb)) continue;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store() && inst != store_inst) return false;
            if (inst->is_call()) return false;
        }
    }

    out.inner_loop   = L;
    out.parent_loop  = P;
    out.sum_phi      = sum_phi;
    out.sum_init     = sum_init;
    out.sum_latch    = sum_latch;
    out.body_geps    = std::move(body_geps);
    out.store_inst   = store_inst;
    out.gep_store    = gep_store;
    out.base_store   = base_store;
    out.inner_bound  = L_bound;
    out.parent_bound = P_bound;
    out.inner_dim    = d_st;
    return true;
}

// ── 合法 + 有益 ──────────────────────────────────────────────────────────

bool ScalarExpandedInterchange::isLegalAndProfitable(const ReductionNestInfo &info,
                                                      DependenceAnalysis &DA,
                                                      CostModel &CM) {
    // 合法性：P↔L 两层间没有跨循环的真依赖（仅 reduction，scalar expansion 后消除）。
    std::vector<Instruction *> accesses;
    for (auto *bb : info.inner_loop->blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load()) accesses.push_back(inst);
        }
    }
    accesses.push_back(info.store_inst);
    if (!DA.isInterchangeLegal(info.parent_loop, info.inner_loop, accesses)) return false;

    // 收益：CostModel 比较 swap 前后所有 GEP 对当前内层 IV 的 byte-stride 之和
    PhiInst *L_iv = info.inner_loop->canonicalIV;
    PhiInst *P_iv = info.parent_loop->canonicalIV;
    std::vector<GetElementPtrInst *> geps = info.body_geps;
    geps.push_back(info.gep_store);

    long before = CM.totalStride(geps, L_iv);
    long after  = CM.totalStride(geps, P_iv);
    if (before < 0 || after < 0) return false;
    return after < before;
}

// ── 通用单指令克隆 + valMap remap ────────────────────────────────────────
// 复用 LoopUnroll 同款代码结构（参见 src/mid/opt/loopUnroll.cpp::cloneInst）。
// 不处理 phi/branch/call/alloca/ret——这些由 apply 单独编排。
namespace {
using ValMap = std::unordered_map<Value *, Value *>;

Value *remapVal(Value *v, const ValMap &m) {
    auto it = m.find(v);
    return it != m.end() ? it->second : v;
}

Instruction *cloneNonTermInst(Instruction *orig, BasicBlock *destBB, const ValMap &vm) {
    if (auto *bi = dynamic_cast<BinaryInst *>(orig))
        return new BinaryInst(bi->type_, bi->op_id_,
                              remapVal(bi->get_operand(0), vm),
                              remapVal(bi->get_operand(1), vm), destBB);
    if (auto *ui = dynamic_cast<UnaryInst *>(orig))
        return new UnaryInst(ui->type_, ui->op_id_,
                             remapVal(ui->get_operand(0), vm), destBB);
    if (auto *ci = dynamic_cast<ICmpInst *>(orig))
        return new ICmpInst(ci->icmp_op_,
                            remapVal(ci->get_operand(0), vm),
                            remapVal(ci->get_operand(1), vm), destBB);
    if (auto *fi = dynamic_cast<FCmpInst *>(orig))
        return new FCmpInst(fi->fcmp_op_,
                            remapVal(fi->get_operand(0), vm),
                            remapVal(fi->get_operand(1), vm), destBB);
    if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++)
            idxs.push_back(remapVal(gi->get_operand(i), vm));
        return new GetElementPtrInst(remapVal(gi->get_operand(0), vm), idxs, destBB);
    }
    if (auto *li = dynamic_cast<LoadInst *>(orig))
        return new LoadInst(remapVal(li->get_operand(0), vm), destBB);
    if (auto *zi = dynamic_cast<ZextInst *>(orig))
        return new ZextInst(zi->op_id_, remapVal(zi->get_operand(0), vm), zi->dest_ty_, destBB);
    if (auto *fp = dynamic_cast<FpToSiInst *>(orig))
        return new FpToSiInst(fp->op_id_, remapVal(fp->get_operand(0), vm), fp->dest_ty_, destBB);
    if (auto *sf = dynamic_cast<SiToFpInst *>(orig))
        return new SiToFpInst(sf->op_id_, remapVal(sf->get_operand(0), vm), sf->dest_ty_, destBB);
    if (auto *bc = dynamic_cast<Bitcast *>(orig))
        return new Bitcast(bc->op_id_, remapVal(bc->get_operand(0), vm), bc->dest_ty_, destBB);
    return nullptr;
}
} // anonymous

// ── 变换：通用 P-L 嵌套交换 + reduction 标量提升 ─────────────────────────
//
// 骨架：
//   P.preheader → clr_h → ... → nk_h(新外，原 L) → ... → st_h → ... → P.exit_old
// nk 体内嵌套 nj_inner(新内，原 P)；nj_inner 体 = L body 的克隆（除 header）。
// ValMap：L.IV→nk_iv；P.IV→nj_iv；sum_phi→load tmp[nj_iv]；
// 任何更外层的 IV 走 remapVal 默认 identity（自动保留）。
// 跳回 L.header 的回边 → "store tmp[nj_iv]; br nj_l"。

bool ScalarExpandedInterchange::apply(const ReductionNestInfo &info, Module *module) {
    Loop     *L          = info.inner_loop;
    Loop     *P          = info.parent_loop;
    Function *func       = P->header->parent_;
    auto     *i32        = module->int32_ty_;
    Value    *P_bound    = info.parent_bound;
    Value    *L_bound    = info.inner_bound;
    PhiInst  *P_iv_o     = P->canonicalIV;
    PhiInst  *L_iv_o     = L->canonicalIV;
    PhiInst  *sum_phi    = info.sum_phi;

    BasicBlock *P_preheader = P->preheader;
    BasicBlock *P_exit_old  = P->singleExit();
    BasicBlock *L_header_o  = L->header;
    BasicBlock *L_latch_o   = L->singleLatch();
    if (!P_preheader || !P_exit_old || !L_header_o || !L_latch_o) return false;

    auto *tmp_buf = getOrCreateTempBuffer(module, info.inner_dim);

    auto bbNum = [&]() { return std::to_string((int)func->basic_blocks_.size() + 1000); };
    auto newBB = [&](const std::string &tag) {
        return new BasicBlock(module, "se_" + tag + "_" + bbNum(), func);
    };

    BasicBlock *clr_h = newBB("clrH"), *clr_b = newBB("clrB"), *clr_l = newBB("clrL");
    BasicBlock *nk_h  = newBB("nkH"),  *nk_b  = newBB("nkB"),  *nk_l  = newBB("nkL");
    BasicBlock *nj_h  = newBB("njH"),                          *nj_l  = newBB("njL");
    BasicBlock *st_h  = newBB("stH"),  *st_b  = newBB("stB"),  *st_l  = newBB("stL");

    auto *const0 = new ConstantInt(i32, 0);
    auto *const1 = new ConstantInt(i32, 1);

    // ── clear loop: for(jc=0; jc<P_bound; jc++) tmp[jc] = sum_init ──
    PhiInst *clr_jc = PhiInst::create_phi(i32, clr_h);
    clr_jc->add_phi_pair_operand(const0, P_preheader);
    clr_h->add_instruction_front(clr_jc);
    auto *clr_cmp = new ICmpInst(ICmpInst::ICMP_SLT, clr_jc, P_bound, clr_h);
    new BranchInst(clr_cmp, clr_b, nk_h, clr_h);

    auto *clr_gep = new GetElementPtrInst(tmp_buf, {const0, clr_jc}, clr_b);
    new StoreInst(info.sum_init, clr_gep, clr_b);
    new BranchInst(clr_l, clr_b);

    auto *clr_inc = new BinaryInst(i32, Instruction::Add, clr_jc, const1, clr_l);
    clr_jc->add_phi_pair_operand(clr_inc, clr_l);
    new BranchInst(clr_h, clr_l);

    // ── new outer (原 L): for(nk=0; nk<L_bound; nk++) ──
    PhiInst *nk_iv = PhiInst::create_phi(i32, nk_h);
    nk_iv->add_phi_pair_operand(const0, clr_h);
    nk_h->add_instruction_front(nk_iv);
    auto *nk_cmp = new ICmpInst(ICmpInst::ICMP_SLT, nk_iv, L_bound, nk_h);
    new BranchInst(nk_cmp, nk_b, st_h, nk_h);
    new BranchInst(nj_h, nk_b);

    // ── new inner (原 P) header ──
    PhiInst *nj_iv = PhiInst::create_phi(i32, nj_h);
    nj_iv->add_phi_pair_operand(const0, nk_b);
    nj_h->add_instruction_front(nj_iv);
    auto *nj_cmp = new ICmpInst(ICmpInst::ICMP_SLT, nj_iv, P_bound, nj_h);

    // ── 通用 body clone：把 L.blocks - {L.header} 复制到 nj_inner 体内 ──
    // body 入口 = L.header 在 loop 内的那条分支目标
    BasicBlock *body_entry_o = nullptr;
    {
        auto *term = L_header_o->get_terminator();
        auto *br   = dynamic_cast<BranchInst *>(term);
        if (!br || br->num_ops_ != 3) return false;
        auto *t = static_cast<BasicBlock *>(br->get_operand(1));
        auto *f = static_cast<BasicBlock *>(br->get_operand(2));
        if (L->blocks.count(t))      body_entry_o = t;
        else if (L->blocks.count(f)) body_entry_o = f;
        else return false;
    }

    std::vector<BasicBlock *> orig_bbs;
    for (auto *bb : L->blocks) {
        if (bb != L_header_o) orig_bbs.push_back(bb);
    }

    std::unordered_map<BasicBlock *, BasicBlock *> bbMap;
    for (auto *bb : orig_bbs) {
        bbMap[bb] = newBB("c" + std::to_string(bbMap.size()));
    }
    BasicBlock *body_entry_n = bbMap[body_entry_o];

    // ValMap：L.IV/P.IV 替换；其余外层 IV 走 remapVal 默认 identity（不在 vm 即原值）
    ValMap vm;
    vm[L_iv_o] = nk_iv;
    vm[P_iv_o] = nj_iv;

    // sum_load 在 body 入口最前面（早于任何使用 sum_phi 的指令）
    auto *sum_load_gep = new GetElementPtrInst(tmp_buf, {const0, nj_iv}, body_entry_n);
    auto *sum_load     = new LoadInst(sum_load_gep, body_entry_n);
    vm[sum_phi] = sum_load;

    // phi 空壳（除 sum_phi）
    for (auto *bb : orig_bbs) {
        BasicBlock *nb = bbMap[bb];
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi()) break;
            auto *p = static_cast<PhiInst *>(inst);
            if (p == sum_phi) continue;
            auto *np = PhiInst::create_phi(p->type_, nb);
            nb->add_instruction_front(np);
            vm[p] = np;
        }
    }

    // 克隆非 phi 非 terminator 指令
    for (auto *bb : orig_bbs) {
        BasicBlock *nb = bbMap[bb];
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi() || inst->isTerminator()) continue;
            Instruction *cl = cloneNonTermInst(inst, nb, vm);
            if (!cl) return false;
            vm[inst] = cl;
        }
    }

    // 填 phi 操作数
    auto mapBBOrLatch = [&](BasicBlock *src) -> BasicBlock * {
        if (src == L_header_o) return nj_l;             // 回边目标
        auto it = bbMap.find(src);
        return it != bbMap.end() ? it->second : src;
    };
    for (auto *bb : orig_bbs) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi()) break;
            auto *p = static_cast<PhiInst *>(inst);
            if (p == sum_phi) continue;
            auto *np = static_cast<PhiInst *>(vm[p]);
            for (unsigned i = 0; i < p->num_ops_; i += 2) {
                Value      *v   = p->get_operand(i);
                BasicBlock *src = static_cast<BasicBlock *>(p->get_operand(i + 1));
                if (src == L_header_o || src == L->preheader) continue;
                np->add_phi_pair_operand(remapVal(v, vm), mapBBOrLatch(src));
            }
        }
    }

    // emit clone BB terminator；latch 的 clone 先 store tmp[nj_iv]
    for (auto *bb : orig_bbs) {
        BasicBlock *nb = bbMap[bb];
        if (bb == L_latch_o) {
            auto *st_gep = new GetElementPtrInst(tmp_buf, {const0, nj_iv}, nb);
            new StoreInst(remapVal(info.sum_latch, vm), st_gep, nb);
        }
        auto *term = bb->get_terminator();
        auto *br   = dynamic_cast<BranchInst *>(term);
        if (!br) return false;
        if (br->num_ops_ == 1) {
            BasicBlock *dst = static_cast<BasicBlock *>(br->get_operand(0));
            new BranchInst(mapBBOrLatch(dst), nb);
        } else {
            Value      *cond = remapVal(br->get_operand(0), vm);
            BasicBlock *t    = static_cast<BasicBlock *>(br->get_operand(1));
            BasicBlock *f    = static_cast<BasicBlock *>(br->get_operand(2));
            new BranchInst(cond, mapBBOrLatch(t), mapBBOrLatch(f), nb);
        }
    }

    // nj_h 终止符
    new BranchInst(nj_cmp, body_entry_n, nk_l, nj_h);

    auto *nj_inc = new BinaryInst(i32, Instruction::Add, nj_iv, const1, nj_l);
    nj_iv->add_phi_pair_operand(nj_inc, nj_l);
    new BranchInst(nj_h, nj_l);

    auto *nk_inc = new BinaryInst(i32, Instruction::Add, nk_iv, const1, nk_l);
    nk_iv->add_phi_pair_operand(nk_inc, nk_l);
    new BranchInst(nk_h, nk_l);

    // ── store-back loop: D[…, sj] = tmp[sj] ──
    // 用原 gep_store 的索引结构构造目标 GEP，最后一维替换为 st_iv，
    // 其余外层索引（i, j …）保持原值（它们由 remapVal 默认 identity 处理）。
    PhiInst *st_iv = PhiInst::create_phi(i32, st_h);
    st_iv->add_phi_pair_operand(const0, nk_h);
    st_h->add_instruction_front(st_iv);
    auto *st_cmp = new ICmpInst(ICmpInst::ICMP_SLT, st_iv, P_bound, st_h);
    new BranchInst(st_cmp, st_b, P_exit_old, st_h);

    auto *st_ld_gep = new GetElementPtrInst(tmp_buf, {const0, st_iv}, st_b);
    auto *st_ld_val = new LoadInst(st_ld_gep, st_b);

    std::vector<Value *> st_dst_idxs;
    unsigned gs_last = info.gep_store->num_ops_ - 1;
    for (unsigned m = 1; m < info.gep_store->num_ops_; m++) {
        st_dst_idxs.push_back(m == gs_last ? (Value *)st_iv
                                            : info.gep_store->get_operand(m));
    }
    auto *st_dst_gep = new GetElementPtrInst(info.base_store, st_dst_idxs, st_b);
    new StoreInst(st_ld_val, st_dst_gep, st_b);
    new BranchInst(st_l, st_b);

    auto *st_inc = new BinaryInst(i32, Instruction::Add, st_iv, const1, st_l);
    st_iv->add_phi_pair_operand(st_inc, st_l);
    new BranchInst(st_h, st_l);

    // ── CFG 重连：P.preheader → clr_h，断开 P.header ──
    {
        auto *term = P_preheader->get_terminator();
        if (!term || !term->is_br() || term->num_ops_ != 1) return false;
        term->set_operand(0, clr_h);
        P_preheader->remove_succ_basic_block(P->header);
        P->header->remove_pre_basic_block(P_preheader);
        P_preheader->add_succ_basic_block(clr_h);
        clr_h->add_pre_basic_block(P_preheader);
    }
    P_exit_old->remove_pre_basic_block(P->header);

    return true;
}
