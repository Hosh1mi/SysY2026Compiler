#include "../../include/mid/opt/scalarExpandedInterchange.hpp"
#include "../../include/mid/opt/cfgUtils.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <set>
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
                // apply 把 preheader 改接到新巢，旧循环巢整体不可达，立即清除
                removeUnreachableBlocks(func);
                changed = true;
                break;                                    // CFG 已变，重新分析
            }
        }
        if (!changed) return;
    }
}

// ── 每次创建独立的 temp buffer ────────────────────────────────────────
// 多 reduction 场景下每个 reduction 都需要独立 buffer，故不按 size 缓存。
// 名字带递增 idx 保证唯一。

GlobalVariable *ScalarExpandedInterchange::createTempBuffer(Module *module, int size) {
    std::string name = "__mm_tmp_" + std::to_string(tmp_counter_++) + "_" + std::to_string(size);
    auto *i32 = module->int32_ty_;
    auto *arr = module->get_array_type(i32, size);
    return new GlobalVariable(name, module, arr, false, new ConstantZero(arr));
}

// ── 检测：基于 LoopInfo + AffineAnalysis，支持 N 个 reduction phi ────────
//
// 通用要求（不写死层数 / reduction 个数 / 输出形态）：
//   - L (= 候选 innermost) 有 canonicalIV，preheader、singleLatch、singleExit
//   - L.parent = P 存在且有 canonicalIV
//   - L.header 有 N+1 个 phi：L.IV + N 个 reduction phi（N ≥ 1）
//   - 每个 reduction 的 init 在 P-loop 外可见
//   - L body 全部访存是 load；GEP 仿射、base loop-invariant；
//     索引中出现的 IV 只能是 L 的祖先链上的 canonicalIV（任意层）
//   - L.singleExit 恰好 N 条 store，与 N 个 reduction phi 一一对应：
//     sum_phi_i → gep[base_i, 0, …, P.IV]
//   - P body 除 reduction store 外没有别的 store/call

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

    auto availableOutsideP = [&](Value *v) -> bool {
        if (dynamic_cast<Constant *>(v))       return true;
        if (dynamic_cast<GlobalVariable *>(v)) return true;
        if (dynamic_cast<Argument *>(v))       return true;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst) return true;
        return !P->blocks.count(inst->parent_);
    };

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

    // header phi 扫描：收集所有非 L.IV 的 reduction phi
    std::vector<ReductionInfo> reductions;
    for (auto *inst : L->header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *p = static_cast<PhiInst *>(inst);
        if (p == L_iv) continue;
        if (p->type_->tid_ != Type::IntegerTyID) return false;
        if (p->num_ops_ != 4) return false;

        ReductionInfo r{};
        r.sum_phi = p;
        for (unsigned i = 0; i < p->num_ops_; i += 2) {
            auto *src = static_cast<BasicBlock *>(p->get_operand(i + 1));
            if (src == L->preheader)         r.sum_init  = p->get_operand(i);
            else if (src == L->singleLatch()) r.sum_latch = p->get_operand(i);
        }
        if (!r.sum_init || !r.sum_latch) return false;
        if (!availableOutsideP(r.sum_init)) return false;
        reductions.push_back(r);
    }
    if (reductions.empty()) return false;

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

    // L.singleExit：收集所有 store，逐一配对到 reduction phi
    BasicBlock *L_exit = L->singleExit();
    std::vector<StoreInst *> exit_stores;
    for (auto *inst : L_exit->instr_list_) {
        if (inst->is_store()) exit_stores.push_back(static_cast<StoreInst *>(inst));
        if (inst->is_call())  return false;
    }
    if (exit_stores.size() != reductions.size()) return false;

    std::vector<bool> matched(reductions.size(), false);
    for (auto *s : exit_stores) {
        Value *stored = s->get_operand(0);
        int    mi     = -1;
        for (size_t i = 0; i < reductions.size(); i++) {
            if (!matched[i] && reductions[i].sum_phi == stored) { mi = (int)i; break; }
        }
        if (mi < 0) return false;
        matched[mi] = true;

        auto &r = reductions[mi];
        r.store_inst = s;

        auto *gep_store = dynamic_cast<GetElementPtrInst *>(s->get_operand(1));
        if (!gep_store || gep_store->num_ops_ < 2) return false;
        // 首索引为 0
        AffineExpr e0 = AA.analyze(gep_store->get_operand(1));
        if (!e0.isZero()) return false;
        // 末索引恰好为 P.IV
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
        r.gep_store  = gep_store;
        r.base_store = gep_store->get_operand(0);
        if (!dynamic_cast<GlobalVariable *>(r.base_store) && !dynamic_cast<Argument *>(r.base_store))
            return false;
        r.inner_dim = inner_dim_of(r.base_store);
        if (r.inner_dim <= 0) return false;
    }

    // P body 除 reduction store 外没有别的 store/call
    for (auto *bb : P->blocks) {
        if (L->blocks.count(bb)) continue;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_store()) {
                auto *s = static_cast<StoreInst *>(inst);
                bool is_red = false;
                for (auto &r : reductions) {
                    if (r.store_inst == s) { is_red = true; break; }
                }
                if (!is_red) return false;
            }
            if (inst->is_call()) return false;
        }
    }

    out.inner_loop   = L;
    out.parent_loop  = P;
    out.body_geps    = std::move(body_geps);
    out.reductions   = std::move(reductions);
    out.inner_bound  = L_bound;
    out.parent_bound = P_bound;
    return true;
}

// ── 合法 + 有益 ──────────────────────────────────────────────────────────

bool ScalarExpandedInterchange::isLegalAndProfitable(const ReductionNestInfo &info,
                                                      DependenceAnalysis &DA,
                                                      CostModel &CM) {
    // 合法性：P↔L 两层间没有跨循环的真依赖（reduction 通过 scalar expansion 消除）
    std::vector<Instruction *> accesses;
    for (auto *bb : info.inner_loop->blocks) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load()) accesses.push_back(inst);
        }
    }
    for (auto &r : info.reductions) accesses.push_back(r.store_inst);
    if (!DA.isInterchangeLegal(info.parent_loop, info.inner_loop, accesses)) return false;

    // 收益：CostModel 比较 swap 前后所有 GEP 对当前内层 IV 的 byte-stride 之和
    PhiInst *L_iv = info.inner_loop->canonicalIV;
    PhiInst *P_iv = info.parent_loop->canonicalIV;
    std::vector<GetElementPtrInst *> geps = info.body_geps;
    for (auto &r : info.reductions) geps.push_back(r.gep_store);

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

    BasicBlock *P_preheader = P->preheader;
    BasicBlock *P_exit_old  = P->singleExit();
    BasicBlock *L_header_o  = L->header;
    BasicBlock *L_latch_o   = L->singleLatch();
    if (!P_preheader || !P_exit_old || !L_header_o || !L_latch_o) return false;

    // 每个 reduction 一份独立 tmp buffer
    std::vector<GlobalVariable *> tmp_bufs;
    tmp_bufs.reserve(info.reductions.size());
    for (auto &r : info.reductions) {
        tmp_bufs.push_back(createTempBuffer(module, r.inner_dim));
    }

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

    // ── clear loop: for(jc=0; jc<P_bound; jc++) tmp_i[jc] = sum_init_i  (∀i) ──
    PhiInst *clr_jc = PhiInst::create_phi(i32, clr_h);
    clr_jc->add_phi_pair_operand(const0, P_preheader);
    clr_h->add_instruction_front(clr_jc);
    auto *clr_cmp = new ICmpInst(ICmpInst::ICMP_SLT, clr_jc, P_bound, clr_h);
    new BranchInst(clr_cmp, clr_b, nk_h, clr_h);

    for (size_t i = 0; i < info.reductions.size(); i++) {
        auto *gep = new GetElementPtrInst(tmp_bufs[i], {const0, clr_jc}, clr_b);
        new StoreInst(info.reductions[i].sum_init, gep, clr_b);
    }
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

    // ── body clone：L.blocks - {L.header} 复制到 nj_inner 体内 ──
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

    ValMap vm;
    vm[L_iv_o] = nk_iv;
    vm[P_iv_o] = nj_iv;

    // 在 body_entry_n 起始处为每个 reduction 插入 sum_load
    // 插入顺序决定指令顺序：所有 reduction 的 load 排在最前
    for (size_t i = 0; i < info.reductions.size(); i++) {
        auto *gep      = new GetElementPtrInst(tmp_bufs[i], {const0, nj_iv}, body_entry_n);
        auto *sum_load = new LoadInst(gep, body_entry_n);
        vm[info.reductions[i].sum_phi] = sum_load;
    }

    // 收集所有 reduction phi 集合，用于 phi 克隆时跳过
    std::set<PhiInst *> red_phis;
    for (auto &r : info.reductions) red_phis.insert(r.sum_phi);

    // phi 空壳（跳过 reduction phi）
    for (auto *bb : orig_bbs) {
        BasicBlock *nb = bbMap[bb];
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi()) break;
            auto *p = static_cast<PhiInst *>(inst);
            if (red_phis.count(p)) continue;
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

    auto mapBBOrLatch = [&](BasicBlock *src) -> BasicBlock * {
        if (src == L_header_o) return nj_l;             // 回边目标
        auto it = bbMap.find(src);
        return it != bbMap.end() ? it->second : src;
    };
    for (auto *bb : orig_bbs) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_phi()) break;
            auto *p = static_cast<PhiInst *>(inst);
            if (red_phis.count(p)) continue;
            auto *np = static_cast<PhiInst *>(vm[p]);
            for (unsigned i = 0; i < p->num_ops_; i += 2) {
                Value      *v   = p->get_operand(i);
                BasicBlock *src = static_cast<BasicBlock *>(p->get_operand(i + 1));
                if (src == L_header_o || src == L->preheader) continue;
                np->add_phi_pair_operand(remapVal(v, vm), mapBBOrLatch(src));
            }
        }
    }

    // emit clone BB terminator；latch 的 clone 先写回所有 reduction
    for (auto *bb : orig_bbs) {
        BasicBlock *nb = bbMap[bb];
        if (bb == L_latch_o) {
            for (size_t i = 0; i < info.reductions.size(); i++) {
                auto *gep = new GetElementPtrInst(tmp_bufs[i], {const0, nj_iv}, nb);
                new StoreInst(remapVal(info.reductions[i].sum_latch, vm), gep, nb);
            }
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

    // ── store-back loop: 每次迭代写回所有 reduction 的最终值 ──
    PhiInst *st_iv = PhiInst::create_phi(i32, st_h);
    st_iv->add_phi_pair_operand(const0, nk_h);
    st_h->add_instruction_front(st_iv);
    auto *st_cmp = new ICmpInst(ICmpInst::ICMP_SLT, st_iv, P_bound, st_h);
    new BranchInst(st_cmp, st_b, P_exit_old, st_h);

    for (size_t i = 0; i < info.reductions.size(); i++) {
        auto *orig_gep  = info.reductions[i].gep_store;
        auto *st_ld_gep = new GetElementPtrInst(tmp_bufs[i], {const0, st_iv}, st_b);
        auto *st_ld_val = new LoadInst(st_ld_gep, st_b);

        std::vector<Value *> st_dst_idxs;
        unsigned gs_last = orig_gep->num_ops_ - 1;
        for (unsigned m = 1; m < orig_gep->num_ops_; m++) {
            st_dst_idxs.push_back(m == gs_last ? (Value *)st_iv
                                                : orig_gep->get_operand(m));
        }
        auto *st_dst_gep = new GetElementPtrInst(info.reductions[i].base_store,
                                                  st_dst_idxs, st_b);
        new StoreInst(st_ld_val, st_dst_gep, st_b);
    }
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
