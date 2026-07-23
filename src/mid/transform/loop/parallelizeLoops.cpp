#include "../../../include/mid/opt/parallelizeLoops.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/affineAnalysis.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../../include/mid/ir/instruction.hpp"
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
    return ArgumentAliasAnalysis::underlyingObject(ptr);
}

bool isAcceptedMemoryRoot(Value *root) {
    if (dynamic_cast<GlobalVariable *>(root)) return true;
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    if (alloca && alloca->isLoopExpansionScratch())
        return true;
    auto *arg = dynamic_cast<Argument *>(root);
    return arg && dynamic_cast<PointerType *>(arg->type_);
}

std::string valueName(Value *v) {
    return v && !v->name_.empty() ? v->name_ : "<unnamed>";
}

std::string loopName(const Loop &loop) {
    return loop.header ? loop.header->name_ : "<no-header>";
}

bool isScalarExpansionScratch(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    return alloca && alloca->isLoopExpansionScratch();
}

bool rootsNoAlias(Value *a, Value *b, const ArgumentAliasAnalysis &argAA) {
    if (!a || !b || a == b) return false;
    if (dynamic_cast<GlobalVariable *>(a) && dynamic_cast<GlobalVariable *>(b))
        return true;
    if (isScalarExpansionScratch(a) || isScalarExpansionScratch(b))
        return true;
    return argAA.noAlias(a, b);
}

bool hasProvenSafeMemoryRoots(
    const std::vector<Instruction *> &stores,
    const std::vector<Instruction *> &accesses,
    const ArgumentAliasAnalysis &argAA,
    std::string *reason) {
    auto accessRoot = [](Instruction *acc) -> Value * {
        Value *ptr = acc->is_store() ? acc->get_operand(1) : acc->get_operand(0);
        auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
        return gep ? gepRootBase(gep) : nullptr;
    };

    for (auto *store : stores) {
        Value *storeRoot = accessRoot(store);
        for (auto *acc : accesses) {
            Value *root = accessRoot(acc);
            if (!storeRoot || !root || storeRoot == root) continue;
            if (!rootsNoAlias(storeRoot, root, argAA)) {
                if (reason)
                    *reason = "cannot prove distinct memory roots " +
                              valueName(storeRoot) + " and " +
                              valueName(root);
                return false;
            }
        }
    }
    return true;
}

bool definedInLoop(Value *v, const std::set<BasicBlock *> &blocks) {
    auto *inst = dynamic_cast<Instruction *>(v);
    return inst && blocks.count(inst->parent_);
}

// ScalarExpansion scratch：每个父循环迭代先清零、后累加、再写回。
// 若某个并行循环内完整使用该 scratch，可在 worker 内改成线程私有 alloca。
bool isPrivatizableScratch(Value *root, const std::set<BasicBlock *> &blocks) {
    if (!isScalarExpansionScratch(root)) return false;
    for (auto &use : root->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user || !blocks.count(user->parent_)) return false;
    }
    return true;
}

long long typeBytes(Type *ty) {
    if (dynamic_cast<IntegerType *>(ty)) return 4;
    if (ty && ty->tid_ == Type::FloatTyID) return 4;
    if (auto *arr = dynamic_cast<ArrayType *>(ty)) {
        long long elem = typeBytes(arr->contained_);
        return elem < 0 ? -1 : elem * arr->num_elements_;
    }
    if (dynamic_cast<PointerType *>(ty)) return 8;
    return -1;
}

long long scratchBytes(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    if (!alloca) return -1;
    return typeBytes(alloca->alloca_ty_);
}

Type *scratchAllocaType(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    return alloca ? alloca->alloca_ty_ : nullptr;
}

} // namespace

void ParallelizeLoops::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

bool ParallelizeLoops::matchShape(Loop &loop, LoopShape &shape,
                                  std::string *reason) {
    auto fail = [&](const std::string &why) {
        if (reason) *reason = why;
        return false;
    };

    if (!loop.preheader) return fail("missing preheader");
    BasicBlock *latch = loop.singleLatch();
    BasicBlock *exitBlock = loop.singleExit();
    if (!latch) return fail("missing single latch");
    if (!exitBlock) return fail("missing single exit");
    shape.latch = latch;
    shape.exitBlock = exitBlock;

    // 唯一 header phi = IV
    PhiInst *iv = nullptr;
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        if (iv) return fail("multiple header phi nodes");
        iv = static_cast<PhiInst *>(inst);
    }
    if (!iv) return fail("missing header IV phi");
    if (iv->type_->tid_ != Type::IntegerTyID)
        return fail("IV phi is not integer");
    auto *ivTy = dynamic_cast<IntegerType *>(iv->type_);
    if (!ivTy || ivTy->num_bits_ != 32) return fail("IV phi is not i32");
    shape.ivPhi = iv;

    for (unsigned i = 0; i < iv->num_ops_; i += 2) {
        auto *pred = static_cast<BasicBlock *>(iv->get_operand(i + 1));
        if (pred == loop.preheader)
            shape.init = iv->get_operand(i);
        else if (pred == latch)
            shape.ivNext = dynamic_cast<Instruction *>(iv->get_operand(i));
        else
            return fail("IV phi has unexpected predecessor");
    }
    if (!shape.init) return fail("missing IV init");
    if (!shape.ivNext) return fail("missing IV next");

    // ivNext = add(iv, 1)
    if (!shape.ivNext->is_add()) return fail("IV next is not add");
    Value *a = shape.ivNext->get_operand(0), *b = shape.ivNext->get_operand(1);
    auto isOne = [](Value *v) {
        auto *c = dynamic_cast<ConstantInt *>(v);
        return c && c->value_ == 1;
    };
    if (!((a == iv && isOne(b)) || (b == iv && isOne(a))))
        return fail("IV step is not +1");

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
    if (!shape.exitCmp) return fail("missing i < bound exit condition");
    if (definedInLoop(shape.bound, loop.blocks))
        return fail("loop bound is defined in loop");

    // exit 块不得有 phi（含 LCSSA phi——意味着有 live-out）
    for (auto inst : exitBlock->instr_list_) {
        if (inst->is_phi()) return fail("exit block has phi live-out");
        break;
    }

    // 逃逸边检查：循环内所有后继必须仍在循环内，唯一例外是
    // exitingBlock→exitBlock。dedicated exits（plan 1.1）未实现，
    // break 形循环的多条 exiting 边可能汇入同一 exit 块——若放过，
    // 变换只改写一条出口边，其余会留成跨函数分支。
    for (auto *bb : loop.blocksOrdered) {
        auto *term = bb->get_terminator();
        if (!term) return fail("block without terminator");
        for (unsigned i = 0; i < term->num_ops_; i++) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ || loop.blocks.count(succ)) continue;
            if (bb == shape.exitingBlock && succ == exitBlock) continue;
            return fail("loop has unsupported escape edge");
        }
    }
    return true;
}

bool ParallelizeLoops::isLegalDoall(Loop &loop, const LoopShape &shape,
                                    Function *func, AnalysisManager *AM,
                                    const ArgumentAliasAnalysis &argAA,
                                    std::set<Value *> *privatize,
                                    std::vector<Reduction> *reductions) {
    auto fail = [&](const std::string &why) {
        debugPar("reject func=" + func->name_ + " loop=" + loopName(loop) +
                 ": " + why);
        return false;
    };

    std::vector<Instruction *> stores, accesses;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            if (auto *call = dynamic_cast<CallInst *>(inst)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (!callee || !callee->hasSemFlag(SemFlag::FnPure))
                    return fail("call in loop");
            }
            if (dynamic_cast<AllocaInst *>(inst)) return fail("alloca in loop");
            if (inst->is_store()) {
                Value *ptr = inst->get_operand(1);
                auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
                if (!gep) return fail("store target is not GEP");
                Value *root = gepRootBase(gep);
                if (!isAcceptedMemoryRoot(root))
                    return fail("store has unsupported memory root " +
                                valueName(root));
                stores.push_back(inst);
                accesses.push_back(inst);
            } else if (inst->is_load()) {
                Value *ptr = inst->get_operand(0);
                // 标量全局只读 load 允许；GEP 必须全局数组基址
                if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
                    Value *root = gepRootBase(gep);
                    if (!isAcceptedMemoryRoot(root))
                        return fail("load has unsupported memory root " +
                                    valueName(root));
                } else if (!dynamic_cast<GlobalVariable *>(ptr)) {
                    return fail("load target is not GEP/global");
                }
                accesses.push_back(inst);
            }
        }
    }
    if (stores.empty()) return fail("no stores"); // 无写循环交给其他 pass

    std::string aliasReason;
    if (!hasProvenSafeMemoryRoots(stores, accesses, argAA, &aliasReason))
        return fail(aliasReason);

    // 标量 live-out：循环内定义被循环外使用 → bail
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (auto &use : inst->use_list_) {
                auto *user = dynamic_cast<Instruction *>(use.val_);
                if (user && !loop.blocks.count(user->parent_))
                    return fail("loop-defined value is used outside loop");
            }
        }
    }

    // live-in 类型限制：i32 和指针可通过 ctx 传递；其它类型仍保守拒绝。
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops_; i++) {
                Value *op = inst->get_operand(i);
                if (dynamic_cast<Constant *>(op) ||
                    dynamic_cast<GlobalVariable *>(op) ||
                    dynamic_cast<Function *>(op) ||
                    dynamic_cast<BasicBlock *>(op))
                    continue;
                if (definedInLoop(op, loop.blocks)) continue;
                if (op == shape.init || op == shape.bound) continue; // 形参化
                auto *ity = dynamic_cast<IntegerType *>(op->type_);
                if (ity && ity->num_bits_ == 32) continue;
                if (dynamic_cast<PointerType *>(op->type_)) continue;
                return fail("unsupported live-in type for " + valueName(op));
            }
        }
    }

    // 归约检测：在 store 必须随 IV 变化的硬规则之前，找出归约 store，
    // 以便为其豁免 DIR_EQ 依赖检查。仅当 store/load 所在块的最内层
    // 循环恰为当前循环时才视为本循环归约（避免内层循环的归约误判给外层）。
    std::vector<Reduction> localReductions;
    {
        LoopInfo &LI2 = AM->getLoopInfo(func);
        ScalarEvolution &SE2 = AM->getScalarEvolution(func);
        for (auto *s : stores) {
            // store 所在块的最内层循环必须是当前循环
            if (LI2.getLoopFor(s->parent_) != &loop) continue;
            auto *sVal = s->get_operand(0);
            auto *bin = dynamic_cast<BinaryInst *>(sVal);
            if (!bin || !(bin->is_add() || bin->is_sub() || bin->is_mul()))
                continue;
            Value *sPtr = s->get_operand(1);
            auto *sGep = dynamic_cast<GetElementPtrInst *>(sPtr);
            if (!sGep) continue;
            // 确认 store 地址随本循环 IV 变化
            bool variesWithThisIV = false;
            for (unsigned i = 1; i < sGep->num_ops_; i++) {
                auto *rec = dynamic_cast<const SCEVAddRecExpr *>(
                    SE2.getSCEV(sGep->get_operand(i)));
                if (rec && rec->loop() == &loop) { variesWithThisIV = true; break; }
            }
            if (!variesWithThisIV) continue;
            Value *sRoot = gepRootBase(sGep);
            for (auto *a : accesses) {
                if (!a->is_load()) continue;
                // load 所在块的最内层循环也必须匹配
                if (LI2.getLoopFor(a->parent_) != &loop) continue;
                Value *aPtr = a->get_operand(0);
                auto *aGep = dynamic_cast<GetElementPtrInst *>(aPtr);
                if (!aGep || gepRootBase(aGep) != sRoot) continue;
                bool usesLoad = false;
                for (unsigned i = 0; i < bin->num_ops_; i++)
                    if (bin->get_operand(i) == a) { usesLoad = true; break; }
                if (!usesLoad) continue;
                localReductions.push_back({s, a, sRoot});
                debugPar("reduction detected: store=" + valueName(s) + " load=" +
                         valueName(a));
                break;
            }
        }
        debugPar("found " + std::to_string(localReductions.size()) +
                 " reductions in loop " + loopName(loop));
    }

    // 硬规则：store 地址必须随本循环 IV 变化（某下标是本循环的 AddRec）。
    // 否则两线程会命中同一地址。ScalarExpansion scratch 若完整局限在
    // 当前循环内，则可改成 worker 私有 alloca。
    ScalarEvolution &SE = AM->getScalarEvolution(func);
    long long privBytes = 0;
    for (auto *s : stores) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(s->get_operand(1));
        Value *base = gep ? gepRootBase(gep) : nullptr;

        bool variesWithIV = false;
        for (unsigned i = 1; gep && i < gep->num_ops_; i++) {
            auto *rec = dynamic_cast<const SCEVAddRecExpr *>(
                SE.getSCEV(gep->get_operand(i)));
            if (rec && rec->loop() == &loop) { variesWithIV = true; break; }
        }
        if (!variesWithIV && isPrivatizableScratch(base, loop.blocks)) {
            // 私有 scratch 放 worker 栈（静态 1MB），总量限 64KB 防溢出。
            long long bytes = scratchBytes(base);
            if (bytes < 0) return fail("unknown privatized scratch size");
            if (!privatize->count(base)) {
                privBytes += bytes;
                if (privBytes > 64 * 1024)
                    return fail("privatized scratch exceeds stack budget");
            }
            privatize->insert(base);
            continue;
        }
        if (!variesWithIV)
            return fail("store address does not vary with loop IV");
    }

    // 依赖：每个 (store, access) 对需证明独立或仅同迭代依赖

    LoopInfo &LI = AM->getLoopInfo(func);
    AffineAnalysis AA(LI);
    DependenceAnalysis DA(LI, AA);
    DA.setArgAlias(&argAA);
    auto basePriv = [&](Instruction *acc) {
        Value *ptr = acc->is_store() ? acc->get_operand(1) : acc->get_operand(0);
        return privatize->count(gepRootBase(ptr)) != 0;
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
            if (idx < 0 || idx >= (int)r.direction.size())
                return fail("dependence direction missing for loop");
            if (r.direction[idx] != DependenceAnalysis::DIR_EQ) {
                // 归约：store→load 跨迭代依赖（DIR_LT）是可结合的
                bool isReduction = false;
                for (auto &red : localReductions) {
                    if ((red.store == s && red.load == a) ||
                        (red.load == s && red.store == a))
                        { isReduction = true; break; }
                }
                if (!isReduction)
                    return fail("loop carries memory dependence");
            }
        }
    }

    // 编译期可知的小 trip count 不值得
    auto *ci = dynamic_cast<ConstantInt *>(shape.init);
    auto *cb = dynamic_cast<ConstantInt *>(shape.bound);
    if (ci && cb && cb->value_ - ci->value_ < 64)
        return fail("constant trip count below parallel threshold");

    if (reductions)
        *reductions = std::move(localReductions);

    return true;
}

void ParallelizeLoops::transform(Loop &loop, const LoopShape &shape,
                                 Function *func, Module *module,
                                 const std::set<Value *> &privatize,
                                 const std::vector<Reduction> &reductions) {
    (void)reductions;  // IV-varying reductions need no privatization
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
                    dynamic_cast<Function *>(op) ||
                    dynamic_cast<BasicBlock *>(op))
                    continue;
                if (definedInLoop(op, loop.blocks)) continue;
                if (inst == shape.ivPhi && op == shape.init) continue;
                if (inst == shape.exitCmp && op == shape.bound) continue;
                if (privatize.count(op)) continue;
                if (std::find(liveIns.begin(), liveIns.end(), op) ==
                    liveIns.end())
                    liveIns.push_back(op);
            }
        }
    }
    std::vector<GlobalVariable *> ctxSlots;
    builder->set_insert_point(entry);
    // scratch 私有化：外提体内用栈分配替换原 entry alloca（每线程一份）。
    for (auto *scratch : privatize) {
        Type *allocTy = scratchAllocaType(scratch);
        if (!allocTy) continue;
        auto *priv = builder->create_alloca(allocTy);
        std::vector<std::pair<Instruction *, unsigned>> fixes;
        for (auto &use : scratch->use_list_) {
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
            module, liveIns[k]->type_, false,
            new ConstantZero(liveIns[k]->type_));
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

    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);

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
                LoopShape shape;
                std::string shapeReason;
                if (!matchShape(*loop, shape, &shapeReason)) {
                    debugPar("reject func=" + func->name_ + " loop=" +
                             loopName(*loop) + ": " + shapeReason);
                    continue;
                }
                bool isNested = (loop->depth != 0);
                std::set<Value *> privatize;
                std::vector<Reduction> reductions;
                if (!isLegalDoall(*loop, shape, func, &AM, argAA, &privatize, &reductions))
                    continue;
                // 仅含归约的嵌套循环才允许并行化（避免递归 worker 产生）
                if (isNested && reductions.empty()) {
                    debugPar("skip func=" + func->name_ + " loop=" +
                             loopName(*loop) + ": nested loop without reduction");
                    continue;
                }
                transform(*loop, shape, func, module, privatize, reductions);
                AM.clear(func);
                changed = true;
                localChanged = true;
                break;
            }
        }
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
