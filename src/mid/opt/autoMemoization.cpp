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
                Value *calleeV = inst->get_operand(inst->num_ops_ - 1);
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
        if (!isCandidate(func, baa, selfCalls, externalCalls)) continue;
        if (selfCalls < MIN_SELF_CALLS) continue;

        // 推导每个形参的 bound；推不出时用 DEFAULT_BOUND 兜底（仅作为单参数
        // 本地默认，不会让 BSS 失控——最终是否走数组仍由总乘积阈值决定）
        std::vector<unsigned> bounds;
        bounds.reserve(func->arguments_.size());
        uint64_t product = 1;
        for (auto arg : func->arguments_) {
            unsigned b = deriveArgBound(func, arg);
            if (b == 0) b = DEFAULT_BOUND;
            else b = std::min<unsigned>(b + BOUND_MARGIN, MAX_BOUND);
            product *= b;
            bounds.push_back(b);
        }

        // 数组路径：总元素数 ≤ ARRAY_PRODUCT_LIMIT，BSS 严格可控
        // 否则切换到固定大小哈希，避免静态数组爆炸
        if (product <= ARRAY_PRODUCT_LIMIT) {
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
    // 决策变动（实测会影响 huffman 之类用例的 codegen 质量）
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
//   5. 调用点要求：
//      - 函数体含 load（依赖全局/堆内存）：externalCallCount 必须 == 1
//        理由：调用之间外部代码可能修改函数读取的全局，多调用点缓存会
//        返回旧值。举例：int g; int f(int n){return n+g;}
//                      g=10; f(5);  // 缓存 f(5)=15
//                      g=20; f(5);  // 期望 25，但命中缓存返回 15 → WA
//      - 函数体无 load（真 readnone）：externalCallCount >= 1 即可
//        理由：返回值仅依赖形参，跨调用点缓存语义安全。

bool AutoMemoization::isCandidate(Function *f, BasicAliasAnalysis &baa,
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
                call->get_operand(call->num_ops_ - 1));
            if (!callee) return false;
            if (callee == f) { selfCallCount++; continue; }
            if (baa.mayHaveSideEffect(callee)) return false;
        }
    }

    externalCallCount = 0;
    for (auto &use : f->use_list_) {
        auto user = dynamic_cast<Instruction *>(use.val_);
        if (!user) return false;
        if (user->parent_ && user->parent_->parent_ != f) externalCallCount++;
    }

    if (externalCallCount == 0) return false;

    // 读全局的函数：限制单调用点；纯输入函数：允许多调用点
    bool readsMem = functionReadsMemory(f);
    if (readsMem && externalCallCount != 1) return false;

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
                    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
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
            for (unsigned i = 2; i < gep->num_ops_; ++i) {
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

// ── 变换：插入入口查表 + 出口写回 ────────────────────────────────────────
//
// CFG 结构（TRE 之后）：
//   oldEntry (preheader)  → tailrec_header → ... → retBB
//
// 变换后：
//   newEntry → (bounds OK) → checkBB → (flag != 0) → hitBB
//             (bounds bad) ────────────────┐                ↓ ret cached
//             (flag == 0) ─────────────────┴→ oldEntry → ... → retBB
//                                                            (改写)
//                                              boundsOK? → storeBB → finalRetBB → ret
//                                                       │              ↑
//                                                       └──────────────┘
//
// 关键不变量：oldEntry 的 PHI 操作数无需修改——我们没有改写从 oldEntry
// 出发的边，也没有改写 tailrec_header 的前驱表中 “oldEntry” 这一项。

void AutoMemoization::transform(Function *f,
                                 const std::vector<unsigned> &bounds) {
    Module *m = f->parent_;
    auto i32 = m->int32_ty_;
    auto i1 = m->int1_ty_;

    // 构造缓存表的类型：bounds = [b0, b1] → [b0 x [b1 x i32]]
    Type *valTy = i32;
    for (auto it = bounds.rbegin(); it != bounds.rend(); ++it)
        valTy = m->get_array_type(valTy, *it);
    Type *flagTy = valTy;

    auto *flagGV = new GlobalVariable("__memo_flag_" + f->name_, m, flagTy,
                                       false, new ConstantZero(flagTy));
    auto *valGV  = new GlobalVariable("__memo_val_"  + f->name_, m, valTy,
                                       false, new ConstantZero(valTy));

    BasicBlock *oldEntry = f->basic_blocks_.front();
    // 避免命名冲突：让 newEntry 占用 label_entry，old 改名
    if (oldEntry->name_ == "label_entry") oldEntry->name_ = "label_entry_uncached";

    auto *newEntry = new BasicBlock(m, "label_entry", f);
    auto *checkBB  = new BasicBlock(m, "memo_check", f);
    auto *hitBB    = new BasicBlock(m, "memo_hit",   f);

    // 把 newEntry / checkBB / hitBB 调整到 basic_blocks_ 最前（new 时默认 push_back）
    auto &bbs = f->basic_blocks_;
    auto pull = [&](BasicBlock *bb) {
        bbs.erase(std::remove(bbs.begin(), bbs.end(), bb), bbs.end());
    };
    pull(newEntry); pull(checkBB); pull(hitBB);
    bbs.insert(bbs.begin(), {newEntry, checkBB, hitBB});

    auto *builder = new IRStmtBuilder(newEntry, m);

    // ── newEntry: 边界检查 ──
    Value *inBounds = nullptr;
    for (size_t i = 0; i < f->arguments_.size(); ++i) {
        auto arg = f->arguments_[i];
        auto boundC = new ConstantInt(i32, static_cast<int>(bounds[i]));
        auto cmp = new ICmpInst(ICmpInst::ICMP_ULT, arg, boundC, newEntry);
        if (!inBounds) inBounds = cmp;
        else inBounds = new BinaryInst(i1, Instruction::And, inBounds, cmp,
                                         newEntry);
    }
    builder->create_cond_br(inBounds, checkBB, oldEntry);

    auto makeIdxs = [&](BasicBlock *) {
        std::vector<Value *> v;
        v.reserve(1 + f->arguments_.size());
        v.push_back(new ConstantInt(i32, 0));
        for (auto a : f->arguments_) v.push_back(a);
        return v;
    };

    // ── checkBB: 查表 ──
    builder->set_insert_point(checkBB);
    auto flagPtr = builder->create_gep(flagGV, makeIdxs(checkBB));
    auto flag = builder->create_load(flagPtr);
    auto hit = builder->create_icmp_ne(flag, new ConstantInt(i32, 0));
    builder->create_cond_br(hit, hitBB, oldEntry);

    // ── hitBB: 命中返回 ──
    builder->set_insert_point(hitBB);
    auto valPtr = builder->create_gep(valGV, makeIdxs(hitBB));
    auto cached = builder->create_load(valPtr);
    builder->create_ret(cached);

    // ── 改写所有返回点 ──
    std::vector<std::pair<BasicBlock *, ReturnInst *>> retSites;
    for (auto bb : f->basic_blocks_) {
        if (bb == newEntry || bb == checkBB || bb == hitBB) continue;
        auto term = bb->get_terminator();
        if (term && term->is_ret() && term->num_ops_ > 0)
            retSites.push_back({bb, static_cast<ReturnInst *>(term)});
    }

    for (auto &site : retSites) {
        BasicBlock *retBB = site.first;
        ReturnInst *retInst = site.second;
        Value *retVal = retInst->get_operand(0);

        retBB->delete_instr(retInst);

        auto *storeBB    = new BasicBlock(m, "memo_store", f);
        auto *finalRetBB = new BasicBlock(m, "memo_ret",   f);

        // 重新计算边界（避免把 newEntry 的 i1 跨过整个函数体）
        Value *okExit = nullptr;
        for (size_t i = 0; i < f->arguments_.size(); ++i) {
            auto arg = f->arguments_[i];
            auto boundC = new ConstantInt(i32, static_cast<int>(bounds[i]));
            auto cmp = new ICmpInst(ICmpInst::ICMP_ULT, arg, boundC, retBB);
            if (!okExit) okExit = cmp;
            else okExit = new BinaryInst(i1, Instruction::And, okExit, cmp,
                                          retBB);
        }
        builder->set_insert_point(retBB);
        builder->create_cond_br(okExit, storeBB, finalRetBB);

        // storeBB: 写 flag=1 + val=retVal，跳到 finalRetBB
        builder->set_insert_point(storeBB);
        auto flagS = builder->create_gep(flagGV, makeIdxs(storeBB));
        builder->create_store(new ConstantInt(i32, 1), flagS);
        auto valS = builder->create_gep(valGV, makeIdxs(storeBB));
        builder->create_store(retVal, valS);
        builder->create_br(finalRetBB);

        // finalRetBB: retVal 由 retBB 支配，可直接复用
        builder->set_insert_point(finalRetBB);
        builder->create_ret(retVal);
    }

    f->set_instr_name();
    f->invalidateDominatorInfo();
    delete builder;
}

// ── 变换（哈希路径）：固定大小直接映射哈希缓存 ────────────────────────────
//
// 适用于"bound 推不出"或"bound 乘积过大"的场景，避免静态数组方案的 BSS
// 爆炸 / 兜底 DEFAULT_BOUND 半推半就的问题。
//
// 槽布局（flat [HASH_SLOTS * slotFields x i32]）：
//   slot k 起始偏移 = k * slotFields
//   [base + 0]:           valid (0 = 空)
//   [base + 1 ... +nArgs]: key0, key1, ... （用于碰撞检测）
//   [base + 1 + nArgs]:    val
//
// 哈希式：
//   1 arg : idx = (arg0 * P1) & MASK
//   2 args: idx = (arg0 * P1) ^ (arg1 * P2) & MASK
//   P1=0x45D9F3B, P2=0x119DE1F3 为正向 i32 素数，避免 C++ 端 UB
//
// CFG 结构（TRE 之后）：
//   newEntry → checkKey → hit → ret cached
//             ↓          ↓
//           oldEntry ← (valid==0 或任一 key 不匹配)
//                  ... → retBB → store{keys, val, valid=1} → ret retVal

void AutoMemoization::transformHash(Function *f) {
    Module *m = f->parent_;
    auto i32 = m->int32_ty_;
    unsigned nArgs = f->arguments_.size();
    unsigned slotFields = 1 + nArgs + 1;           // valid + keys + val
    unsigned totalI32 = HASH_SLOTS * slotFields;

    // 哈希乘子：正向 i32 素数；IR 层 Mul 的溢出是 well-defined 模 2^32 行为
    static constexpr int P1 = 0x45D9F3B;
    static constexpr int P2 = 0x119DE1F3;

    Type *arrTy = m->get_array_type(i32, totalI32);
    auto *hashGV = new GlobalVariable("__memo_hash_" + f->name_, m, arrTy,
                                       false, new ConstantZero(arrTy));

    BasicBlock *oldEntry = f->basic_blocks_.front();
    if (oldEntry->name_ == "label_entry") oldEntry->name_ = "label_entry_uncached";

    auto *newEntry  = new BasicBlock(m, "label_entry", f);
    auto *checkKey  = new BasicBlock(m, "memo_check_key", f);
    auto *hitBB     = new BasicBlock(m, "memo_hit", f);

    auto &bbs = f->basic_blocks_;
    auto pull = [&](BasicBlock *bb) {
        bbs.erase(std::remove(bbs.begin(), bbs.end(), bb), bbs.end());
    };
    pull(newEntry); pull(checkKey); pull(hitBB);
    bbs.insert(bbs.begin(), {newEntry, checkKey, hitBB});

    auto *builder = new IRStmtBuilder(newEntry, m);

    // 计算 idx 与 base：可在多个 BB 重用，封装成 lambda
    auto computeBase = [&](BasicBlock *bb) -> Value * {
        Value *hashVal = nullptr;
        for (unsigned i = 0; i < nArgs; ++i) {
            int p = (i == 0) ? P1 : P2;
            auto *mul = new BinaryInst(i32, Instruction::Mul,
                                       f->arguments_[i],
                                       new ConstantInt(i32, p), bb);
            if (!hashVal) hashVal = mul;
            else hashVal = new BinaryInst(i32, Instruction::Xor, hashVal, mul, bb);
        }
        auto *idx = new BinaryInst(i32, Instruction::And, hashVal,
                                    new ConstantInt(i32, static_cast<int>(HASH_MASK)), bb);
        // slot base = idx * slotFields
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

    // ── newEntry: 计算 base，load valid，分流 ──
    Value *base = computeBase(newEntry);
    Value *vPtr = gepSlot(base, 0, newEntry);
    builder->set_insert_point(newEntry);
    auto validVal = builder->create_load(vPtr);
    auto isValid = builder->create_icmp_ne(validVal, new ConstantInt(i32, 0));
    builder->create_cond_br(isValid, checkKey, oldEntry);

    // ── checkKey: 比较每个 key，全相等才命中 ──
    Value *baseCK = computeBase(checkKey);
    Value *allEq = nullptr;
    for (unsigned i = 0; i < nArgs; ++i) {
        Value *kPtr = gepSlot(baseCK, 1 + i, checkKey);
        auto *load = new LoadInst(kPtr, checkKey);
        auto *eq = new ICmpInst(ICmpInst::ICMP_EQ, load, f->arguments_[i], checkKey);
        if (!allEq) allEq = eq;
        else allEq = new BinaryInst(m->int1_ty_, Instruction::And, allEq, eq, checkKey);
    }
    builder->set_insert_point(checkKey);
    builder->create_cond_br(allEq, hitBB, oldEntry);

    // ── hitBB: load val 返回 ──
    Value *baseHit = computeBase(hitBB);
    Value *valPtrHit = gepSlot(baseHit, 1 + nArgs, hitBB);
    builder->set_insert_point(hitBB);
    auto cached = builder->create_load(valPtrHit);
    builder->create_ret(cached);

    // ── 改写所有返回点：store keys + val + valid=1，再 ret ──
    std::vector<std::pair<BasicBlock *, ReturnInst *>> retSites;
    for (auto bb : f->basic_blocks_) {
        if (bb == newEntry || bb == checkKey || bb == hitBB) continue;
        auto term = bb->get_terminator();
        if (term && term->is_ret() && term->num_ops_ > 0)
            retSites.push_back({bb, static_cast<ReturnInst *>(term)});
    }

    for (auto &site : retSites) {
        BasicBlock *retBB = site.first;
        ReturnInst *retInst = site.second;
        Value *retVal = retInst->get_operand(0);

        retBB->delete_instr(retInst);

        Value *baseRet = computeBase(retBB);

        builder->set_insert_point(retBB);
        // 先写 keys 和 val，最后才置 valid（后置写顺序仅风格选择，单线程无影响）
        for (unsigned i = 0; i < nArgs; ++i) {
            Value *kPtr = gepSlot(baseRet, 1 + i, retBB);
            builder->create_store(f->arguments_[i], kPtr);
        }
        Value *vPtrRet = gepSlot(baseRet, 1 + nArgs, retBB);
        builder->create_store(retVal, vPtrRet);
        Value *validPtrRet = gepSlot(baseRet, 0, retBB);
        builder->create_store(new ConstantInt(i32, 1), validPtrRet);

        builder->create_ret(retVal);
    }

    f->set_instr_name();
    f->invalidateDominatorInfo();
    delete builder;
}
