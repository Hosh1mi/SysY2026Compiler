// 典型示例：
//   优化前：递归函数 f(20) 在多条递归路径上反复计算相同实参。
//   优化后：f(20) 先查询缓存，未命中时计算一次并保存结果。
// 缓存键覆盖全部整数参数，只有满足纯度和参数范围约束的函数才会改写。

#include "../../include/mid/opt/autoMemoization.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../include/mid/ir/ir.hpp"
#include "../../include/mid/ir/irBuilder.hpp"

#include <algorithm>
#include <set>
#include <vector>

// 自动记忆化把满足约束的递归纯函数拆成“缓存包装器 + 原函数体”。包装器按
// 整数实参索引缓存结果，原函数体仍承载递归计算。候选筛选会检查参数范围、
// 副作用和全局内存依赖，避免缓存键无法覆盖完整可观察状态。
//
// 最终会生成三类符号：
//   f              对外名称保持不变，函数体改成查缓存的包装器；
//   f_memo_body    保存 f 原来的基本块，负责真正计算；
//   f_memo_fill    仅稠密表路径使用，负责计算未命中项并写入缓存。
// 原函数体中的递归调用仍然调用 f，因此每一层递归都会先经过缓存包装器。

// 兼容旧式调用入口：创建临时分析管理器后转入统一实现。
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
    // BAA 用于候选纯度检查。整个模块只创建一个分析结果，避免逐函数重复分析。
    BasicAliasAnalysis &baa = AM.getBasicAA(module);

    // 先收集候选，再统一变换；避免在遍历 function_list_ 时插入全局变量影响遍历
    std::vector<Function *> arrayFuncs;
    std::vector<std::vector<unsigned>> arrayBoundsList;
    std::vector<Function *> hashFuncs;

    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        // selfCalls 用于判断重复子问题的潜在收益；externalCalls 保证生成的
        // 包装器确实有模块内调用者，避免改写完全未使用的函数。
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
            // raw==0 表示没有从数组访问中推导出可信上界。
            unsigned raw = deriveArgBound(func, arg);
            unsigned b;
            if (raw == 0) {
                // 只要其它参数有可靠上界，未推导参数仍可用默认维度参与稠密表。
                // 运行时范围检查会让超出默认维度的实参绕过缓存。
                b = DEFAULT_BOUND;
            } else {
                anyDerived = true;
                // 留出少量余量，覆盖 n+1 等轻微偏移访问；再用 MAX_BOUND
                // 限制单个维度，防止生成体积失控的全局数组。
                b = std::min<unsigned>(raw + BOUND_MARGIN, MAX_BOUND);
            }
            product *= b;
            bounds.push_back(b);
        }

        // 稠密表可直接用参数做多维下标，命中成本低；参数域未知或表太大时
        // 使用固定大小的直接映射哈希表，将内存占用限制在常量范围内。
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

    // 记忆化要求相同实参始终得到相同结果。store 会修改可观察状态，未知
    // 间接调用也无法证明纯度，因此看到这些情况立即拒绝。
    selfCallCount = 0;
    for (auto bb : f->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            auto call = dynamic_cast<CallInst *>(inst);
            if (!call) continue;
            auto callee = dynamic_cast<Function *>(
                call->get_operand(call->num_ops() - 1));
            if (!callee) return false;
            // 自调用是目标结构的一部分；其它调用必须由 BAA 证明无副作用。
            if (callee == f) { selfCallCount++; continue; }
            if (baa.mayHaveSideEffect(callee)) return false;
        }
    }

    // use_list_ 同时包含函数体内的自调用和函数外的调用。这里只统计后者。
    externalCallCount = 0;
    for (auto &use : f->use_list_) {
        auto user = use.user_;
        if (!user) return false;
        if (user->parent_ && user->parent_->parent_ != f) externalCallCount++;
    }

    if (externalCallCount == 0) return false;

    // 读取全局量的函数仍有机会记忆化，但必须证明这些全局在调用期间保持稳定。
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
    // 第一步只收集 f 实际读取的全局。其它全局的写入与缓存结果无关。
    std::set<GlobalVariable *> readGlobals;
    for (auto bb : f->basic_blocks_)
        for (auto inst : bb->instr_list_)
            if (inst->is_load())
                if (auto gv = baseGlobal(inst->get_operand(0)))
                    readGlobals.insert(gv);
    if (readGlobals.empty()) return false;

    // 第二步收集 f 之外的所有使用点。递归调用发生在 f 自身，跳过即可。
    std::vector<Instruction *> callSites;
    for (auto &use : f->use_list_) {
        auto user = use.user_;
        if (!user || !user->parent_) continue;
        if (user->parent_->parent_ != f)
            callSites.push_back(user);
    }
    if (callSites.empty()) return false;

    // 第三步扫描整个模块中对这些全局的 store。实现采用基本块级支配关系：
    // 写入与调用不在同一函数，或写入块无法支配调用块，都会拒绝记忆化。
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

    // derived 集合稳定后，检查每个全局数组 GEP。arg 若参与某一维下标，
    // 该维的数组长度就是一个可用上界候选。
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
//
// 此处移动现有 BasicBlock 对象，不克隆指令。这样原函数体内部的 CFG、PHI 和
// 指令 use-def 关系都保持原样，只需修改基本块所属函数和形参引用。
Function *AutoMemoization::outlineBody(Function *f) {
    Module *m = f->parent_;
    auto *fty = static_cast<FunctionType *>(f->type_);
    auto *body = new Function(fty, f->name_ + "_memo_body", m);

    // 先复制指针列表再清空 f。完成后 f 是一个没有基本块的空壳，后续会在
    // 同一 Function 对象上构造包装器，因此模块中的旧调用无需改目标符号。
    std::vector<BasicBlock *> moved = f->basic_blocks_;
    f->basic_blocks_.clear();
    for (auto *bb : moved) {
        bb->parent_ = body;
        body->basic_blocks_.push_back(bb);
    }
    // 被移动指令原先引用 f 的 Argument。新函数拥有一组新的 Argument，
    // 必须逐个替换，否则 body 会跨函数引用包装器形参。
    for (size_t i = 0; i < f->arguments_.size(); ++i)
        f->arguments_[i]->replace_all_use_with(body->arguments_[i]);

    return body;
}

// ── 数组路径：打包表 + 薄包装 ────────────────────────────────────────────
//
// 表元素为 [2 x i32]：[0]=flag，[1]=val，命中时两次 load 落在同一缓存行。
// 参数直接作为多维数组下标。例如 bounds={100, 50} 时，表的逻辑形状为
// table[100][50][2]，table[a][b][0] 是有效标记，[1] 是 f(a,b) 的结果。
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

    // 先保存原函数体。此行之后 f 已经没有基本块，可以直接用于构造包装 CFG。
    Function *bodyFn = outlineBody(f);

    // 未命中冷路径：call body + 写回。独立成函数，避免包装入口为 miss
    // 路径分配 callee-saved，从而让命中路径接近叶子（只动调用者保存寄存器）。
    auto *fty = static_cast<FunctionType *>(f->type_);
    auto *fillFn = new Function(fty, f->name_ + "_memo_fill", m);

    // 从最内层 [2 x i32] 开始，逆序包裹每个参数维度：
    // bounds={b0,b1} 会得到 [b0 x [b1 x [2 x i32]]]。
    Type *pairTy = m->get_array_type(i32, 2);
    Type *tableTy = pairTy;
    for (auto it = bounds.rbegin(); it != bounds.rend(); ++it)
        tableTy = m->get_array_type(tableTy, *it);

    // 全零初始化同时表示“全部槽位尚未计算”。结果本身可以为 0，因此必须
    // 单独保存 flag，不能用结果是否为 0 判断命中。
    auto *tableGV = new GlobalVariable("__memo_" + f->name_, m, tableTy,
                                       false, new ConstantZero(tableTy));

    // 为 table[arg0][arg1]...[field] 生成 GEP 下标。
    // 第一个 0 用于进入全局数组对象，随后依次是全部函数参数，最后选择
    // flag(0) 或 value(1)。owner 可以是包装器 f，也可以是 fillFn。
    auto makePairIdxs = [&](Function *owner, int field) {
        std::vector<Value *> v;
        v.reserve(2 + owner->arguments_.size());
        v.push_back(new ConstantInt(i32, 0));
        for (auto *a : owner->arguments_)
            v.push_back(a);
        v.push_back(new ConstantInt(i32, field));
        return v;
    };

    // 生成所有参数的范围检查：0 <= arg[i] < bounds[i]。
    // ICMP_ULT 使用无符号比较，负数会被解释为很大的无符号数，自然判为越界。
    // 多个比较用 and 串联；候选至少有一个参数，因此 ok 最终一定有值。
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

    // ── fillFn：计算一次，参数在表范围内时写回 ──
    // fillFn 总会调用 bodyFn。越界参数只返回计算结果，不访问缓存数组；这样
    // 上界推导偏小时仍保持原函数语义。
    {
        auto *fEntry = new BasicBlock(m, "entry", fillFn);
        auto *fStore = new BasicBlock(m, "memo_store", fillFn);
        auto *fRet = new BasicBlock(m, "memo_ret", fillFn);
        auto *fb = new IRStmtBuilder(fEntry);
        std::vector<Value *> args(fillFn->arguments_.begin(),
                                  fillFn->arguments_.end());
        // bodyFn 内部的递归调用仍指向包装器 f，所以子问题可以继续命中缓存。
        auto *result = fb->create_call(bodyFn, args);
        Value *ok = emitInBounds(fillFn, fEntry);
        fb->create_cond_br(ok, fStore, fRet);

        fb->set_insert_point(fStore);
        // 同一槽位依次写有效标记和结果。当前编译器执行模型为单线程调用，
        // 不需要额外的原子发布协议。
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

    // ── wrapper f：只负责范围检查、查表和分派 ──
    // 命中路径不进入原函数体。未命中和越界路径都调用 fillFn；fillFn 会自行
    // 决定是否写表。把冷路径拆开可让频繁命中的包装器保持短小。
    {
        auto *entry = new BasicBlock(m, "entry", f);
        auto *checkBB = new BasicBlock(m, "memo_check", f);
        auto *hitBB = new BasicBlock(m, "memo_hit", f);
        auto *missBB = new BasicBlock(m, "memo_miss", f);
        auto *builder = new IRStmtBuilder(entry);

        // 越界参数不能形成合法 GEP，必须在任何表访问之前分流到 missBB。
        Value *inBounds = emitInBounds(f, entry);
        builder->create_cond_br(inBounds, checkBB, missBB);

        builder->set_insert_point(checkBB);
        // 参数在范围内后，先读取 flag。flag==0 表示该实参组合尚未计算。
        auto *flag = builder->create_load(
            builder->create_gep(tableGV, makePairIdxs(f, 0)));
        auto *hit = builder->create_icmp_ne(flag, new ConstantInt(i32, 0));
        builder->create_cond_br(hit, hitBB, missBB);

        builder->set_insert_point(hitBB);
        // 只有 flag 非零才读取 value 字段并立即返回。
        auto *cached = builder->create_load(
            builder->create_gep(tableGV, makePairIdxs(f, 1)));
        builder->create_ret(cached);

        builder->set_insert_point(missBB);
        std::vector<Value *> args(f->arguments_.begin(), f->arguments_.end());
        // fillFn 同时覆盖“表内未命中”和“参数越界”两种情况。
        auto *result = builder->create_call(fillFn, args);
        builder->create_ret(result);

        f->set_instr_name();
        delete builder;
    }

    bodyFn->set_instr_name();
}

// ── 哈希路径：固定槽数的直接映射缓存 ─────────────────────────────────────
//
// 每个槽位平铺为 [valid, arg0, arg1, ..., result]。不同参数可能映射到同一槽，
// 因此命中时必须逐个比较完整参数；冲突时新结果直接覆盖旧槽。该策略没有探测
// 链，查找成本固定，缓存占用也不随参数范围增长。
/* static */ void AutoMemoization::transformHash(Function *f) {
    Module *m = f->parent_;
    auto i32 = m->int32_ty_;
    unsigned nArgs = f->arguments_.size();
    // 一个 valid 字段、nArgs 个键字段、一个 result 字段。
    unsigned slotFields = 1 + nArgs + 1;
    unsigned totalI32 = HASH_SLOTS * slotFields;

    // 每个参数乘不同的奇数常量，再用 xor 混合，降低简单相关参数集中到
    // 同一槽位的概率。候选参数最多三个，因此准备三个乘数即可。
    static constexpr int P1 = 0x45D9F3B;
    static constexpr int P2 = 0x119DE1F3;
    static constexpr int P3 = 0x1AC1337;

    // 哈希路径 miss 率/写回更重：不再拆 fill，避免多层 call 放大开销。
    Function *bodyFn = outlineBody(f);

    // 使用一维 i32 数组保存全部槽，便于通过“槽基址 + 字段下标”寻址。
    Type *arrTy = m->get_array_type(i32, totalI32);
    auto *hashGV = new GlobalVariable("__memo_hash_" + f->name_, m, arrTy,
                                       false, new ConstantZero(arrTy));

    // entry 检查 valid；checkKey 核对完整键；hitBB 返回缓存；missBB 计算并覆盖。
    auto *entry = new BasicBlock(m, "entry", f);
    auto *checkKey = new BasicBlock(m, "memo_check_key", f);
    auto *hitBB = new BasicBlock(m, "memo_hit", f);
    auto *missBB = new BasicBlock(m, "memo_miss", f);
    auto *builder = new IRStmtBuilder(entry);

    // 计算槽位在平铺数组中的起始下标：
    //   slot = hash(args) & (HASH_SLOTS - 1)
    //   base = slot * slotFields
    // HASH_SLOTS 是 2 的幂，所以按位与可以替代取模。
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

    // 将“槽基址 + 字段号”转换为 hashGV 中具体 i32 字段的地址。
    auto gepSlot = [&](Value *base, unsigned fieldIdx, BasicBlock *bb) -> Value * {
        Value *off = base;
        if (fieldIdx != 0) {
            off = new BinaryInst(i32, Instruction::Add, base,
                                  new ConstantInt(i32, static_cast<int>(fieldIdx)), bb);
        }
        std::vector<Value *> idxs = { new ConstantInt(i32, 0), off };
        return new GetElementPtrInst(hashGV, idxs, bb);
    };

    // 空槽一定未命中，可以跳过参数比较直接进入 missBB。
    Value *base = computeBase(entry);
    auto *validVal = builder->create_load(gepSlot(base, 0, entry));
    auto *isValid = builder->create_icmp_ne(validVal, new ConstantInt(i32, 0));
    builder->create_cond_br(isValid, checkKey, missBB);

    // valid 只说明槽内有数据。哈希冲突仍可能让该槽属于另一组实参，
    // 所有键字段都相等时才能进入 hitBB。
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

    // 完整键匹配成功，读取槽位最后一个字段作为缓存结果。
    Value *baseHit = computeBase(hitBB);
    builder->set_insert_point(hitBB);
    auto *cached = builder->create_load(gepSlot(baseHit, 1 + nArgs, hitBB));
    builder->create_ret(cached);

    // 未命中时调用原函数体。递归子调用仍经过包装器，可命中当前表中的其它槽。
    builder->set_insert_point(missBB);
    std::vector<Value *> callArgs(f->arguments_.begin(), f->arguments_.end());
    auto *result = builder->create_call(bodyFn, callArgs);
    Value *baseRet = computeBase(missBB);
    // 写入顺序为完整键、结果、valid。valid 最后更新，使槽位布局的含义清晰：
    // valid 非零时，键和结果字段都已由本次未命中路径填写。
    for (unsigned i = 0; i < nArgs; ++i)
        builder->create_store(f->arguments_[i], gepSlot(baseRet, 1 + i, missBB));
    builder->create_store(result, gepSlot(baseRet, 1 + nArgs, missBB));
    builder->create_store(new ConstantInt(i32, 1), gepSlot(baseRet, 0, missBB));
    builder->create_ret(result);

    f->set_instr_name();
    bodyFn->set_instr_name();
    delete builder;
}
