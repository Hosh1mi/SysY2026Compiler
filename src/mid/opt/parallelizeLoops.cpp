#include "../../include/mid/opt/parallelizeLoops.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/affineAnalysis.hpp"
#include "../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {

bool isParDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_PARALLEL") != nullptr;
    return enabled;
}

void debugPar(const std::string &msg) {
    if (isParDebugEnabled())
        std::cerr << "[Parallelize] " << msg << "\n";
}

// GEP 链回溯到基址
Value *gepRootBase(Value *ptr) {
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->get_operand(0);
    return ptr;
}

bool definedInLoop(Value *v, const std::set<BasicBlock *> &blocks) {
    auto *inst = dynamic_cast<Instruction *>(v);
    return inst && blocks.count(inst->parent_);
}

// ScalarExpandedInterchange 的 __mm_tmp_* scratch：每外层迭代先写后读的
// 私有缓冲。若其全部使用都在本循环内，可按线程私有化（外提体内换 alloca）。
bool isPrivatizableScratch(GlobalVariable *gv,
                           const std::set<BasicBlock *> &blocks) {
    if (gv->name_.rfind("__mm_tmp", 0) != 0) return false;
    for (auto &use : gv->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !blocks.count(user->parent_)) return false;
    }
    return true;
}

// 全局数组字节大小（SysY 元素 i32/float 均 4 字节）
long long globalArrayBytes(GlobalVariable *gv) {
    auto *ptrTy = dynamic_cast<PointerType *>(gv->type_);
    if (!ptrTy) return -1;
    long long elems = 1;
    Type *ty = ptrTy->contained_;
    while (auto *arr = dynamic_cast<ArrayType *>(ty)) {
        elems *= arr->num_elements_;
        ty = arr->contained_;
    }
    return elems * 4;
}

} // namespace

void ParallelizeLoops::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

bool ParallelizeLoops::matchShape(Loop &loop, LoopShape &shape) {
    if (!loop.preheader) return false;
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exitBlock = loop.singleExit();
    if (!latch || !exitBlock) return false;
    shape.latch = latch;
    shape.exitBlock = exitBlock;

    // 唯一 header phi = IV
    PhiInst *iv = nullptr;
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (iv) return false;
        iv = static_cast<PhiInst *>(inst);
    }
    if (!iv || iv->type_->tid_ != Type::IntegerTyID) return false;
    auto *ivTy = dynamic_cast<IntegerType *>(iv->type_);
    if (!ivTy || ivTy->num_bits_ != 32) return false;
    shape.ivPhi = iv;

    for (unsigned i = 0; i < iv->num_ops_; i += 2) {
        auto *pred = static_cast<BasicBlock *>(iv->get_operand(i + 1));
        if (pred == loop.preheader)
            shape.init = iv->get_operand(i);
        else if (pred == latch)
            shape.ivNext = dynamic_cast<Instruction *>(iv->get_operand(i));
        else
            return false;
    }
    if (!shape.init || !shape.ivNext) return false;

    // ivNext = add(iv, 1)
    if (!shape.ivNext->is_add()) return false;
    Value *a = shape.ivNext->get_operand(0), *b = shape.ivNext->get_operand(1);
    auto isOne = [](Value *v) {
        auto *c = dynamic_cast<ConstantInt *>(v);
        return c && c->value_ == 1;
    };
    if (!((a == iv && isOne(b)) || (b == iv && isOne(a)))) return false;

    // 出口：header（while 形）或 latch（do-while 形）的 slt 条件分支
    for (BasicBlock *cand : {loop.header, latch}) {
        auto *term = cand->get_terminator();
        if (!term || term->num_ops_ != 3) continue; // 非 cond br
        auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
        if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) continue;
        Value *lhs = cmp->get_operand(0);
        if (lhs != iv && lhs != shape.ivNext) continue;
        auto *tSucc = static_cast<BasicBlock *>(term->get_operand(1));
        auto *fSucc = static_cast<BasicBlock *>(term->get_operand(2));
        if (fSucc != exitBlock || !loop.blocks.count(tSucc)) continue;
        shape.exitCmp = cmp;
        shape.bound = cmp->get_operand(1);
        shape.exitingBlock = cand;
        break;
    }
    if (!shape.exitCmp) return false;
    if (definedInLoop(shape.bound, loop.blocks)) return false;

    // exit 块不得有 phi（含 LCSSA phi——意味着有 live-out）
    for (auto inst : exitBlock->instr_list_) {
        if (inst->is_phi()) return false;
        break;
    }

    // 逃逸边检查：循环内所有后继必须仍在循环内，唯一例外是
    // exitingBlock→exitBlock。dedicated exits（plan 1.1）未实现，
    // break 形循环的多条 exiting 边可能汇入同一 exit 块——若放过，
    // 变换只改写一条出口边，其余会留成跨函数分支。
    for (auto *bb : loop.blocksOrdered) {
        auto *term = bb->get_terminator();
        if (!term) return false;
        for (unsigned i = 0; i < term->num_ops_; i++) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ || loop.blocks.count(succ)) continue;
            if (bb == shape.exitingBlock && succ == exitBlock) continue;
            return false;
        }
    }
    return true;
}

bool ParallelizeLoops::isLegalDoall(Loop &loop, const LoopShape &shape,
                                    Function *func, AnalysisManager *AM,
                                    std::set<GlobalVariable *> *privatize) {
    std::vector<Instruction *> stores, accesses;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            if (dynamic_cast<CallInst *>(inst)) return false;
            if (dynamic_cast<AllocaInst *>(inst)) return false;
            if (inst->is_store()) {
                Value *ptr = inst->get_operand(1);
                if (!dynamic_cast<GetElementPtrInst *>(ptr)) return false;
                if (!dynamic_cast<GlobalVariable *>(gepRootBase(ptr)))
                    return false;
                stores.push_back(inst);
                accesses.push_back(inst);
            } else if (inst->is_load()) {
                Value *ptr = inst->get_operand(0);
                // 标量全局只读 load 允许；GEP 必须全局数组基址
                if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
                    if (!dynamic_cast<GlobalVariable *>(gepRootBase(gep)))
                        return false;
                } else if (!dynamic_cast<GlobalVariable *>(ptr)) {
                    return false;
                }
                accesses.push_back(inst);
            }
        }
    }
    if (stores.empty()) return false; // 无写循环交给其他 pass

    // 标量 live-out：循环内定义被循环外使用 → bail
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && !loop.blocks.count(user->parent_)) return false;
            }
        }
    }

    // live-in 类型限制：仅 i32（指针/float 阶段一不传 ctx）
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                if (dynamic_cast<Constant *>(op) ||
                    dynamic_cast<GlobalVariable *>(op) ||
                    dynamic_cast<BasicBlock *>(op))
                    continue;
                if (definedInLoop(op, loop.blocks)) continue;
                if (op == shape.init || op == shape.bound) continue; // 形参化
                auto *ity = dynamic_cast<IntegerType *>(op->type_);
                if (!ity || ity->num_bits_ != 32) return false;
            }
        }
    }

    // 硬规则：store 地址必须随本循环 IV 变化（某下标是本循环的 AddRec）。
    // 否则两线程会命中同一地址（如 ScalarExpandedInterchange 的
    // __mm_tmp_* 全局 scratch 缓冲，下标只随内层 IV 变化）——DA 对
    // "下标不含外层 IV"的 store 会漏报跨外层迭代依赖，不能只靠 DA。
    ScalarEvolution &SE = AM->getScalarEvolution(func);
    long long privBytes = 0;
    for (auto *s : stores) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(s->get_operand(1));
        auto *base = gep ? dynamic_cast<GlobalVariable *>(gepRootBase(gep))
                         : nullptr;
        if (base && isPrivatizableScratch(base, loop.blocks)) {
            // 私有拷贝放 worker 栈（静态 1MB），总量限 64KB 防溢出
            long long bytes = globalArrayBytes(base);
            if (bytes < 0) return false;
            if (!privatize->count(base)) {
                privBytes += bytes;
                if (privBytes > 64 * 1024) return false;
            }
            privatize->insert(base);
            continue;
        }
        bool variesWithIV = false;
        for (unsigned i = 1; gep && i < gep->num_ops_; i++) {
            auto *rec = dynamic_cast<const SCEVAddRecExpr *>(
                SE.getSCEV(gep->get_operand(i)));
            if (rec && rec->loop() == &loop) { variesWithIV = true; break; }
        }
        if (!variesWithIV) return false;
    }

    // 依赖：每个 (store, access) 对需证明独立或仅同迭代依赖
    LoopInfo &LI = AM->getLoopInfo(func);
    AffineAnalysis AA(LI);
    DependenceAnalysis DA(LI, AA);
    auto basePriv = [&](Instruction *acc) {
        Value *ptr = acc->is_store() ? acc->get_operand(1) : acc->get_operand(0);
        auto *g = dynamic_cast<GlobalVariable *>(gepRootBase(ptr));
        return g && privatize->count(g);
    };
    for (auto *s : stores) {
        if (basePriv(s)) continue;
        for (auto *a : accesses) {
            if (basePriv(a)) continue;
            auto r = DA.test(s, a);
            if (r.provably_independent) continue;
            int idx = -1;
            for (size_t i = 0; i < r.commonLoops.size(); i++) {
                if (r.commonLoops[i] == &loop) { idx = (int)i; break; }
            }
            if (idx < 0 || idx >= (int)r.direction.size()) return false;
            if (r.direction[idx] != DependenceAnalysis::DIR_EQ) return false;
        }
    }

    // 编译期可知的小 trip count 不值得
    auto *ci = dynamic_cast<ConstantInt *>(shape.init);
    auto *cb = dynamic_cast<ConstantInt *>(shape.bound);
    if (ci && cb && cb->value_ - ci->value_ < 64) return false;

    return true;
}

void ParallelizeLoops::transform(Loop &loop, const LoopShape &shape,
                                 Function *func, Module *module,
                                 const std::set<GlobalVariable *> &privatize) {
    int id = (int)bodies_.size();

    if (!parallelForDecl_) {
        auto *fty = new FunctionType(
            module->void_ty_,
            {module->int32_ty_, module->int32_ty_, module->int32_ty_});
        parallelForDecl_ = new Function(fty, "__sysy_parallel_for", module);
    }

    auto *bodyTy = new FunctionType(module->void_ty_,
                                    {module->int32_ty_, module->int32_ty_});
    auto *bodyFn = new Function(
        bodyTy, "__sysy_par_body_" + std::to_string(id), module);
    Value *lo = bodyFn->arguments_[0];
    Value *hi = bodyFn->arguments_[1];

    auto *entry = new BasicBlock(module, "label_par_entry", bodyFn);
    auto *retbb = new BasicBlock(module, "label_par_ret", bodyFn);
    auto *builder = new IRStmtBuilder(retbb, module);
    builder->create_void_ret();

    // live-in 收集（与 isLegalDoall 同口径）→ ctx 全局
    std::vector<Value *> liveIns;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                if (dynamic_cast<Constant *>(op) ||
                    dynamic_cast<GlobalVariable *>(op) ||
                    dynamic_cast<BasicBlock *>(op))
                    continue;
                if (definedInLoop(op, loop.blocks)) continue;
                if (inst == shape.ivPhi && op == shape.init) continue;
                if (inst == shape.exitCmp && op == shape.bound) continue;
                if (std::find(liveIns.begin(), liveIns.end(), op) ==
                    liveIns.end())
                    liveIns.push_back(op);
            }
        }
    }
    std::vector<GlobalVariable *> ctxSlots;
    builder->set_insert_point(entry);
    // scratch 私有化：外提体内用栈拷贝替换全局缓冲（每线程一份）
    for (auto *gv : privatize) {
        auto *ptrTy = static_cast<PointerType *>(gv->type_);
        auto *priv = builder->create_alloca(ptrTy->contained_);
        std::vector<std::pair<Instruction *, unsigned>> fixes;
        for (auto &use : gv->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && loop.blocks.count(user->parent_))
                fixes.push_back({user, use.arg_no_});
        }
        for (auto &f : fixes)
            f.first->set_operand(f.second, priv);
    }
    std::vector<Value *> ctxLoads;
    for (size_t k = 0; k < liveIns.size(); k++) {
        auto *gv = new GlobalVariable(
            "__sysy_par_ctx_" + std::to_string(id) + "_" + std::to_string(k),
            module, module->int32_ty_, false,
            new ConstantInt(module->int32_ty_, 0));
        ctxSlots.push_back(gv);
        ctxLoads.push_back(builder->create_load(gv));
    }
    builder->create_br(loop.header);
    entry->add_succ_basic_block(loop.header);

    // 环内对 live-in 的引用 → ctx load
    for (size_t k = 0; k < liveIns.size(); k++) {
        Value *v = liveIns[k];
        std::vector<std::pair<Instruction *, unsigned>> fixes;
        for (auto &use : v->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (user && loop.blocks.count(user->parent_))
                fixes.push_back({user, use.arg_no_});
        }
        for (auto &f : fixes)
            f.first->set_operand(f.second, ctxLoads[k]);
    }

    // IV init → lo（入边块 preheader → entry）；出口比较 bound → hi
    for (unsigned i = 0; i < shape.ivPhi->num_ops_; i += 2) {
        if (shape.ivPhi->get_operand(i + 1) ==
            static_cast<Value *>(loop.preheader)) {
            shape.ivPhi->set_operand(i, lo);
            shape.ivPhi->set_operand(i + 1, entry);
        }
    }
    shape.exitCmp->set_operand(1, hi);

    // 循环块迁移到 bodyFn
    for (auto *bb : loop.blocksOrdered) {
        auto &bbs = func->basic_blocks_;
        bbs.erase(std::remove(bbs.begin(), bbs.end(), bb), bbs.end());
        bb->parent_ = bodyFn;
        bodyFn->basic_blocks_.push_back(bb);
    }
    loop.header->remove_pre_basic_block(loop.preheader);
    loop.header->add_pre_basic_block(entry);

    // 出口边 → ret 块
    auto *exitTerm = shape.exitingBlock->get_terminator();
    for (unsigned i = 0; i < exitTerm->num_ops_; i++) {
        if (exitTerm->get_operand(i) == static_cast<Value *>(shape.exitBlock))
            exitTerm->set_operand(i, retbb);
    }
    shape.exitingBlock->remove_succ_basic_block(shape.exitBlock);
    shape.exitingBlock->add_succ_basic_block(retbb);
    retbb->add_pre_basic_block(shape.exitingBlock);
    shape.exitBlock->remove_pre_basic_block(shape.exitingBlock);

    // 调用点：preheader 可能是双后继 guard，不动它——新建 par_call 块
    auto *parCall = new BasicBlock(module, "label_par_call", func);
    builder->set_insert_point(parCall);
    for (size_t k = 0; k < liveIns.size(); k++)
        builder->create_store(liveIns[k], ctxSlots[k]);
    builder->create_call(parallelForDecl_,
                         {new ConstantInt(module->int32_ty_, id), shape.init,
                          shape.bound});
    builder->create_br(shape.exitBlock);
    parCall->add_succ_basic_block(shape.exitBlock);
    shape.exitBlock->add_pre_basic_block(parCall);

    auto *preTerm = loop.preheader->get_terminator();
    for (unsigned i = 0; i < preTerm->num_ops_; i++) {
        if (preTerm->get_operand(i) == static_cast<Value *>(loop.header))
            preTerm->set_operand(i, parCall);
    }
    loop.preheader->remove_succ_basic_block(loop.header);
    loop.preheader->add_succ_basic_block(parCall);
    parCall->add_pre_basic_block(loop.preheader);

    delete builder;
    bodies_.push_back(bodyFn);
    debugPar("parallelized func=" + func->name_ +
             " header=" + loop.header->name_ + " id=" + std::to_string(id));
}

PreservedAnalyses ParallelizeLoops::execute(Module *module,
                                            AnalysisManager &AM) {
    bodies_.clear();
    parallelForDecl_ = nullptr;

    std::vector<Function *> funcs;
    for (auto f : module->function_list_)
        if (!f->is_declaration()) funcs.push_back(f);

    // 调试钩子：PAR_LIMIT=N 只并行化前 N 个循环（错例二分定位用，
    // 与 DEBUG_PARALLEL 配合；缺省上限 32）
    size_t parLimit = 32;
    if (const char *lim = std::getenv("PAR_LIMIT"))
        parLimit = (size_t)atoi(lim);

    bool changed = false;
    for (auto *func : funcs) {
        // 每个函数最多反复尝试（变换后 LoopInfo 失效需重查）
        bool localChanged = true;
        while (localChanged && bodies_.size() < parLimit) {
            localChanged = false;
            LoopInfo &LI = AM.getLoopInfo(func);
            for (auto &lptr : LI.allLoops()) {
                Loop *loop = lptr.get();
                if (loop->depth != 0) continue; // 仅顶层
                LoopShape shape;
                if (!matchShape(*loop, shape)) continue;
                std::set<GlobalVariable *> privatize;
                if (!isLegalDoall(*loop, shape, func, &AM, &privatize))
                    continue;
                transform(*loop, shape, func, module, privatize);
                AM.clear(func);
                changed = true;
                localChanged = true;
                break;
            }
        }
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
