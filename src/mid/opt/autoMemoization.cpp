#include "../../include/mid/opt/autoMemoization.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../include/mid/ir/ir.hpp"
#include "../../include/mid/ir/irBuilder.hpp"

#include <algorithm>
#include <set>
#include <vector>

void AutoMemoization::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

// 仅做最廉价的结构性筛查：返回 i32、1~MAX_ARGS 个 i32 参数、且静态自
// 调用数 >= MIN_SELF_CALLS。比 isCandidate 宽松（允许有 store、call 不
// pure），但足够把绝大多数无关模块排除掉。
bool AutoMemoization::moduleHasAnyCandidate(Module *m) {
    if (!m) return false;
    for (auto func : m->function_list_) {
        if (func->is_declaration()) continue;
        auto retTy = func->get_return_type();
        if (!retTy || retTy->tid_ != Type::IntegerTyID) continue;
        if (static_cast<IntegerType *>(retTy)->num_bits_ != 32) continue;
        if (func->arguments_.empty() || func->arguments_.size() > MAX_ARGS) continue;
        bool argsOK = true;
        for (auto arg : func->arguments_) {
            if (arg->type_->tid_ != Type::IntegerTyID) { argsOK = false; break; }
            if (static_cast<IntegerType *>(arg->type_)->num_bits_ != 32) { argsOK = false; break; }
        }
        if (!argsOK) continue;

        unsigned selfCalls = 0;
        for (auto bb : func->basic_blocks_) {
            for (auto inst : bb->instr_list_) {
                if (inst->op_id_ != Instruction::Call) continue;
                Value *calleeV = inst->get_operand(inst->num_ops() - 1);
                if (calleeV == static_cast<Value *>(func)) {
                    selfCalls++;
                    if (selfCalls >= MIN_SELF_CALLS) return true;
                }
            }
        }
    }
    return false;
}

PreservedAnalyses AutoMemoization::execute(Module *module,
                                           AnalysisManager &AM) {
    BasicAliasAnalysis &baa = AM.getBasicAA(module);

    // 先收集候选，再统一变换；避免在遍历 function_list_ 时插入全局变量影响遍历
    std::vector<Function *> arrayFuncs;
    std::vector<std::vector<unsigned>> arrayBoundsList;
    std::vector<Function *> hashFuncs;

    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        unsigned selfCalls = 0, externalCalls = 0;
        if (!isCandidate(func, baa, AM, selfCalls, externalCalls)) continue;
        if (selfCalls < MIN_SELF_CALLS) continue;

        // 推导每个形参的 bound。
        // - 至少一个参数可推导，且乘积 ≤ 上限 → 数组路径（其余用 DEFAULT 填）
        // - 全部推不出，或乘积过大 → 哈希路径（避免无界域上的盲猜稠密表）
        std::vector<unsigned> bounds;
        bounds.reserve(func->arguments_.size());
        uint64_t product = 1;
        bool anyDerived = false;
        for (auto arg : func->arguments_) {
            unsigned raw = deriveArgBound(func, arg);
            unsigned b;
            if (raw == 0) {
                b = DEFAULT_BOUND;
            } else {
                anyDerived = true;
                b = std::min<unsigned>(raw + BOUND_MARGIN, MAX_BOUND);
            }
            product *= b;
            bounds.push_back(b);
        }

        if (anyDerived && product <= ARRAY_PRODUCT_LIMIT) {
            arrayFuncs.push_back(func);
            arrayBoundsList.push_back(std::move(bounds));
        } else {
            hashFuncs.push_back(func);
        }
    }

    for (size_t i = 0; i < arrayFuncs.size(); ++i) {
        transform(arrayFuncs[i], arrayBoundsList[i]);
    }
    for (auto func : hashFuncs) {
        transformHash(func);
    }

    // 没有命中任何候选时，IR 完全未变；保留分析结果避免下游 pass 重算导致
    // 非候选模块的 codegen 决策发生无关变化。
    if (arrayFuncs.empty() && hashFuncs.empty()) return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}

// ── 候选检测 ─────────────────────────────────────────────────────────────
//
// 通用结构特征：
//   1. 返回 i32
//   2. 1 ~ MAX_ARGS 个 i32 形参
//   3. 无 store（自身无副作用），且所有非自调用 callee 都是 pure
//   4. 至少 MIN_SELF_CALLS 个自调用（保证记忆化收益）
//   5. 至少 1 个外部调用点
//   6. 若函数体含 load：所读全局必须在全部外部调用点之前已冻结
//      （每个相关 store 的基本块支配每一个外部调用点）。否则跨调用点
//      缓存可能读到过期值。无 load（readnone）则允许多调用点。

bool AutoMemoization::isCandidate(Function *f, BasicAliasAnalysis &baa,
                                  AnalysisManager &AM,
                                   unsigned &selfCallCount,
                                   unsigned &externalCallCount) {
    auto retTy = f->get_return_type();
    if (!retTy || retTy->tid_ != Type::IntegerTyID) return false;
    if (static_cast<IntegerType *>(retTy)->num_bits_ != 32) return false;

    if (f->arguments_.empty() || f->arguments_.size() > MAX_ARGS) return false;
    for (auto arg : f->arguments_) {
        if (arg->type_->tid_ != Type::IntegerTyID) return false;
        if (static_cast<IntegerType *>(arg->type_)->num_bits_ != 32) return false;
    }

    selfCallCount = 0;
    for (auto bb : f->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            auto call = dynamic_cast<CallInst *>(inst);
            if (!call) continue;
            auto callee = dynamic_cast<Function *>(
                call->get_operand(call->num_ops() - 1));
            if (!callee) return false;
            if (callee == f) { selfCallCount++; continue; }
            if (baa.mayHaveSideEffect(callee)) return false;
        }
    }

    externalCallCount = 0;
    for (auto &use : f->use_list_) {
        auto user = use.user_;
        if (!user) return false;
        if (user->parent_ && user->parent_->parent_ != f) externalCallCount++;
    }

    if (externalCallCount == 0) return false;

    if (functionReadsMemory(f) && readsUnfrozenGlobal(f, AM)) return false;

    return true;
}

// 函数是否含任何 LoadInst。
//
// 经过 mem2reg 后，标量 alloca 已被提升为 SSA；剩余 load 几乎都是从全局
// 数组（GEP from GlobalVariable）读取。由于候选函数只允许 i32 形参（无指
// 针参数），函数内的 load 不会通过形参间接读到调用方传入的内存。
//
// 因此 "any load present" 是 "函数返回值依赖全局内存状态" 的稳健保守近似。
bool AutoMemoization::functionReadsMemory(Function *f) {
    for (auto bb : f->basic_blocks_)
        for (auto inst : bb->instr_list_)
            if (inst->is_load()) return true;
    return false;
}

// 把指针回溯到其基址全局变量（穿透 GEP 链）；非全局返回 nullptr。
static GlobalVariable *baseGlobal(Value *ptr) {
    while (auto gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->get_operand(0);
    return dynamic_cast<GlobalVariable *>(ptr);
}

// f 读取的全局是否在全部外部调用点之前未冻结。
//
// 安全条件：对每个被 f load 的全局 G，模块内每个写 G 的 store 都必须
// 与全部外部调用点位于同一函数，且 store 所在基本块支配每一个调用点
// 基本块。这样所有写都发生在首次调用之前，调用之间不会再改 G。
// 跨函数的 store / 调用关系无法用单函数支配树刻画 → 保守视为未冻结。
bool AutoMemoization::readsUnfrozenGlobal(Function *f, AnalysisManager &AM) {
    std::set<GlobalVariable *> readGlobals;
    for (auto bb : f->basic_blocks_)
        for (auto inst : bb->instr_list_)
            if (inst->is_load())
                if (auto gv = baseGlobal(inst->get_operand(0)))
                    readGlobals.insert(gv);
    if (readGlobals.empty()) return false;

    std::vector<Instruction *> callSites;
    for (auto &use : f->use_list_) {
        auto user = use.user_;
        if (!user || !user->parent_) continue;
        if (user->parent_->parent_ != f)
            callSites.push_back(user);
    }
    if (callSites.empty()) return false;

    for (auto func : f->parent_->function_list_) {
        for (auto bb : func->basic_blocks_) {
            for (auto inst : bb->instr_list_) {
                if (!inst->is_store()) continue;
                auto gv = baseGlobal(inst->get_operand(1));
                if (!gv || !readGlobals.count(gv)) continue;

                for (auto *cs : callSites) {
                    Function *caller = cs->parent_->parent_;
                    if (caller != func)
                        return true;
                    if (!AM.getDominatorTree(func).dominates(bb, cs->parent_))
                        return true;
                }
            }
        }
    }
    return false;
}

// ── 参数上界推导 ─────────────────────────────────────────────────────────
//
// 在函数内寻找以 `arg` 为索引（允许穿透一层 add/sub）访问全局数组的 GEP，
// 取其所在维度长度作为上界。多个 GEP 取最大。
// 注：超出推导出的上界时运行时会走 bypass 路径，仍然正确，只是没有加速。

unsigned AutoMemoization::deriveArgBound(Function *f, Argument *arg) {
    // 先构造 “由 arg 派生” 的值集合：包括 arg 自身，以及（递归地）
    // 任何 phi/add/sub 指令其操作数中存在已派生值的，都视为 arg 的派生。
    // TRE 之后 arg 会被 phi 替换，必须沿 phi 反向追踪。
    std::set<Value *> derived;
    derived.insert(arg);
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : f->basic_blocks_) {
            for (auto inst : bb->instr_list_) {
                if (derived.count(inst)) continue;
                if (auto phi = dynamic_cast<PhiInst *>(inst)) {
                    bool any = false;
                    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                        if (derived.count(phi->get_operand(i))) { any = true; break; }
                    }
                    if (any) { derived.insert(phi); changed = true; }
                } else if (auto bin = dynamic_cast<BinaryInst *>(inst)) {
                    if (bin->op_id_ == Instruction::Add ||
                        bin->op_id_ == Instruction::Sub) {
                        if (derived.count(bin->get_operand(0)) ||
                            derived.count(bin->get_operand(1))) {
                            derived.insert(bin);
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    unsigned bound = 0;
    for (auto bb : f->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            auto gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep) continue;
            auto gv = dynamic_cast<GlobalVariable *>(gep->get_operand(0));
            if (!gv) continue;
            auto ptrTy = dynamic_cast<PointerType *>(gv->type_);
            if (!ptrTy) continue;
            // operand 1（idx0）只是 “选指针指向的对象”，不消耗维度；
            // 真正的下标从 operand 2 起。
            Type *cur = ptrTy->contained_;
            for (unsigned i = 2; i < gep->num_ops(); ++i) {
                if (cur->tid_ != Type::ArrayTyID) break;
                auto arrTy = static_cast<ArrayType *>(cur);
                unsigned dim = arrTy->num_elements_;
                Value *idx = gep->get_operand(i);
                if (derived.count(idx) && dim > bound) bound = dim;
                cur = arrTy->contained_;
            }
        }
    }
    return bound;
}

// ── outline：原函数体迁到 *_memo_body，自调用仍指向包装符号 f ──
Function *AutoMemoization::outlineBody(Function *f) {
    Module *m = f->parent_;
    auto *fty = static_cast<FunctionType *>(f->type_);
    auto *body = new Function(fty, f->name_ + "_memo_body", m);

    std::vector<BasicBlock *> moved = f->basic_blocks_;
    f->basic_blocks_.clear();
    for (auto *bb : moved) {
        bb->parent_ = body;
        body->basic_blocks_.push_back(bb);
    }
    for (size_t i = 0; i < f->arguments_.size(); ++i)
        f->arguments_[i]->replace_all_use_with(body->arguments_[i]);

    return body;
}

// ── 数组路径：打包表 + 薄包装 ────────────────────────────────────────────
//
// 表元素为 [2 x i32]：[0]=flag，[1]=val，命中时两次 load 落在同一缓存行。
// 包装 CFG：
//   entry → (in bounds) → check → (flag!=0) → hit → ret cached
//                        ↓ miss              ↓
//         (oob) ─────────┴───────────────────┴→ missBB
//                                               call body → (in bounds) store → ret

void AutoMemoization::transform(Function *f,
                                 const std::vector<unsigned> &bounds) {
    Module *m = f->parent_;
    auto i32 = m->int32_ty_;
    auto i1 = m->int1_ty_;

    Function *bodyFn = outlineBody(f);

    // 未命中冷路径：call body + 写回。独立成函数，避免包装入口为 miss
    // 路径分配 callee-saved，从而让命中路径接近叶子（只动调用者保存寄存器）。
    auto *fty = static_cast<FunctionType *>(f->type_);
    auto *fillFn = new Function(fty, f->name_ + "_memo_fill", m);

    // [b0 x [b1 x ... [2 x i32]]]
    Type *pairTy = m->get_array_type(i32, 2);
    Type *tableTy = pairTy;
    for (auto it = bounds.rbegin(); it != bounds.rend(); ++it)
        tableTy = m->get_array_type(tableTy, *it);

    auto *tableGV = new GlobalVariable("__memo_" + f->name_, m, tableTy,
                                       false, new ConstantZero(tableTy));

    auto makePairIdxs = [&](Function *owner, int field) {
        std::vector<Value *> v;
        v.reserve(2 + owner->arguments_.size());
        v.push_back(new ConstantInt(i32, 0));
        for (auto *a : owner->arguments_)
            v.push_back(a);
        v.push_back(new ConstantInt(i32, field));
        return v;
    };

    auto emitInBounds = [&](Function *owner, BasicBlock *bb) -> Value * {
        Value *ok = nullptr;
        for (size_t i = 0; i < owner->arguments_.size(); ++i) {
            auto *boundC = new ConstantInt(i32, static_cast<int>(bounds[i]));
            auto *cmp = new ICmpInst(ICmpInst::ICMP_ULT, owner->arguments_[i],
                                     boundC, bb);
            if (!ok) ok = cmp;
            else ok = new BinaryInst(i1, Instruction::And, ok, cmp, bb);
        }
        return ok;
    };

    // ── fillFn: body + optional store ──
    {
        auto *fEntry = new BasicBlock(m, "entry", fillFn);
        auto *fStore = new BasicBlock(m, "memo_store", fillFn);
        auto *fRet = new BasicBlock(m, "memo_ret", fillFn);
        auto *fb = new IRStmtBuilder(fEntry);
        std::vector<Value *> args(fillFn->arguments_.begin(),
                                  fillFn->arguments_.end());
        auto *result = fb->create_call(bodyFn, args);
        Value *ok = emitInBounds(fillFn, fEntry);
        fb->create_cond_br(ok, fStore, fRet);

        fb->set_insert_point(fStore);
        fb->create_store(new ConstantInt(i32, 1),
                         fb->create_gep(tableGV, makePairIdxs(fillFn, 0)));
        fb->create_store(result,
                         fb->create_gep(tableGV, makePairIdxs(fillFn, 1)));
        fb->create_br(fRet);

        fb->set_insert_point(fRet);
        fb->create_ret(result);
        fillFn->set_instr_name();
        delete fb;
    }

    // ── wrapper f: 薄查表；miss 只 tail-call 式 call fill ──
    {
        auto *entry = new BasicBlock(m, "entry", f);
        auto *checkBB = new BasicBlock(m, "memo_check", f);
        auto *hitBB = new BasicBlock(m, "memo_hit", f);
        auto *missBB = new BasicBlock(m, "memo_miss", f);
        auto *builder = new IRStmtBuilder(entry);

        Value *inBounds = emitInBounds(f, entry);
        builder->create_cond_br(inBounds, checkBB, missBB);

        builder->set_insert_point(checkBB);
        auto *flag = builder->create_load(
            builder->create_gep(tableGV, makePairIdxs(f, 0)));
        auto *hit = builder->create_icmp_ne(flag, new ConstantInt(i32, 0));
        builder->create_cond_br(hit, hitBB, missBB);

        builder->set_insert_point(hitBB);
        auto *cached = builder->create_load(
            builder->create_gep(tableGV, makePairIdxs(f, 1)));
        builder->create_ret(cached);

        builder->set_insert_point(missBB);
        std::vector<Value *> args(f->arguments_.begin(), f->arguments_.end());
        auto *result = builder->create_call(fillFn, args);
        builder->create_ret(result);

        f->set_instr_name();
        delete builder;
    }

    bodyFn->set_instr_name();
}

/* static */ void AutoMemoization::transformHash(Function *f) {
    Module *m = f->parent_;
    auto i32 = m->int32_ty_;
    unsigned nArgs = f->arguments_.size();
    unsigned slotFields = 1 + nArgs + 1;
    unsigned totalI32 = HASH_SLOTS * slotFields;

    static constexpr int P1 = 0x45D9F3B;
    static constexpr int P2 = 0x119DE1F3;
    static constexpr int P3 = 0x1AC1337;

    // 哈希路径 miss 率/写回更重：不再拆 fill，避免多层 call 放大开销。
    Function *bodyFn = outlineBody(f);

    Type *arrTy = m->get_array_type(i32, totalI32);
    auto *hashGV = new GlobalVariable("__memo_hash_" + f->name_, m, arrTy,
                                       false, new ConstantZero(arrTy));

    auto *entry = new BasicBlock(m, "entry", f);
    auto *checkKey = new BasicBlock(m, "memo_check_key", f);
    auto *hitBB = new BasicBlock(m, "memo_hit", f);
    auto *missBB = new BasicBlock(m, "memo_miss", f);
    auto *builder = new IRStmtBuilder(entry);

    auto computeBase = [&](BasicBlock *bb) -> Value * {
        Value *hashVal = nullptr;
        for (unsigned i = 0; i < nArgs; ++i) {
            int p = (i == 0) ? P1 : (i == 1) ? P2 : P3;
            auto *mul = new BinaryInst(i32, Instruction::Mul,
                                       f->arguments_[i],
                                       new ConstantInt(i32, p), bb);
            if (!hashVal) hashVal = mul;
            else hashVal = new BinaryInst(i32, Instruction::Xor, hashVal, mul, bb);
        }
        auto *idx = new BinaryInst(i32, Instruction::And, hashVal,
                                    new ConstantInt(i32, static_cast<int>(HASH_MASK)), bb);
        return new BinaryInst(i32, Instruction::Mul, idx,
                              new ConstantInt(i32, static_cast<int>(slotFields)), bb);
    };

    auto gepSlot = [&](Value *base, unsigned fieldIdx, BasicBlock *bb) -> Value * {
        Value *off = base;
        if (fieldIdx != 0) {
            off = new BinaryInst(i32, Instruction::Add, base,
                                  new ConstantInt(i32, static_cast<int>(fieldIdx)), bb);
        }
        std::vector<Value *> idxs = { new ConstantInt(i32, 0), off };
        return new GetElementPtrInst(hashGV, idxs, bb);
    };

    Value *base = computeBase(entry);
    auto *validVal = builder->create_load(gepSlot(base, 0, entry));
    auto *isValid = builder->create_icmp_ne(validVal, new ConstantInt(i32, 0));
    builder->create_cond_br(isValid, checkKey, missBB);

    Value *baseCK = computeBase(checkKey);
    Value *allEq = nullptr;
    for (unsigned i = 0; i < nArgs; ++i) {
        auto *load = new LoadInst(gepSlot(baseCK, 1 + i, checkKey), checkKey);
        auto *eq = new ICmpInst(ICmpInst::ICMP_EQ, load, f->arguments_[i], checkKey);
        if (!allEq) allEq = eq;
        else allEq = new BinaryInst(m->int1_ty_, Instruction::And, allEq, eq, checkKey);
    }
    builder->set_insert_point(checkKey);
    builder->create_cond_br(allEq, hitBB, missBB);

    Value *baseHit = computeBase(hitBB);
    builder->set_insert_point(hitBB);
    auto *cached = builder->create_load(gepSlot(baseHit, 1 + nArgs, hitBB));
    builder->create_ret(cached);

    builder->set_insert_point(missBB);
    std::vector<Value *> callArgs(f->arguments_.begin(), f->arguments_.end());
    auto *result = builder->create_call(bodyFn, callArgs);
    Value *baseRet = computeBase(missBB);
    for (unsigned i = 0; i < nArgs; ++i)
        builder->create_store(f->arguments_[i], gepSlot(baseRet, 1 + i, missBB));
    builder->create_store(result, gepSlot(baseRet, 1 + nArgs, missBB));
    builder->create_store(new ConstantInt(i32, 1), gepSlot(baseRet, 0, missBB));
    builder->create_ret(result);

    f->set_instr_name();
    bodyFn->set_instr_name();
    delete builder;
}
