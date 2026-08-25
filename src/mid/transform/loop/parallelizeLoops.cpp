/**
 * @file parallelizeLoops.cpp
 * @brief 循环并行化：依据别名和依赖分析把独立循环或波前迭代改写为并行运行时调用。
 * @details 依赖分析证明迭代独立后才生成 doall；有规则距离依赖时使用波前调度，并保留串行回退条件。
 */

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

/**
 * @brief 判断 isParDebugEnabled 所描述的结构、合法性或安全条件是否成立。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isParDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_PARALLEL") != nullptr;
    return enabled;
}

/**
 * @brief 生成 debugPar 对应的调试诊断，不参与程序语义。
 * @param msg 参数 `msg`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void debugPar(const std::string &msg) {
    if (isParDebugEnabled())
        std::cerr << "[Parallelize] " << msg << "\n";
}

/**
 * @brief 生成 debugWavefront 对应的调试诊断，不参与程序语义。
 * @param msg 参数 `msg`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void debugWavefront(const std::string &msg) {
    if (std::getenv("DEBUG_WAVEFRONT"))
        std::cerr << "[WavefrontParallelize] " << msg << "\n";
}

/**
 * @brief 穿过 GEP、bitcast 等派生关系取得内存访问的根对象。
 * @param ptr 待追溯的指针值。
 * @return 别名分析识别出的底层对象。
 */
Value *gepRootBase(Value *ptr) {
    return ArgumentAliasAnalysis::underlyingObject(ptr);
}

/**
 * @brief 判断 isAcceptedMemoryRoot 所描述的结构、合法性或安全条件是否成立。
 * @param root 参数 `root`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isAcceptedMemoryRoot(Value *root) {
    if (dynamic_cast<GlobalVariable *>(root)) return true;
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    if (alloca && alloca->isLoopExpansionScratch())
        return true;
    auto *arg = dynamic_cast<Argument *>(root);
    return arg && dynamic_cast<PointerType *>(arg->type_);
}

/**
 * @brief 实现 valueName 对应的局部分析或变换辅助逻辑。
 * @param v 待检查或映射的 IR 值。
 * @return 返回计算、分析或构造得到的结果。
 */
std::string valueName(Value *v) {
    return v && !v->name_.empty() ? v->name_ : "<unnamed>";
}

/**
 * @brief 实现 loopName 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @return 返回计算、分析或构造得到的结果。
 */
std::string loopName(const Loop &loop) {
    return loop.header ? loop.header->name_ : "<no-header>";
}

/**
 * @brief 实现 valueDependsOn 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param target 参数 `target`，用于本函数的分析、匹配或 IR 构造。
 * @param visited 递归遍历使用的已访问集合，用于避免环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool valueDependsOn(Value *value, Value *target,
                    std::set<Value *> &visited) {
    if (value == target) return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !visited.insert(value).second) return false;
    for (unsigned i = 0; i < instruction->num_ops(); ++i) {
        if (dynamic_cast<BasicBlock *>(instruction->get_operand(i))) continue;
        if (valueDependsOn(instruction->get_operand(i), target, visited))
            return true;
    }
    return false;
}

/**
 * @brief 判断 isDescendantOrSelf 所描述的结构、合法性或安全条件是否成立。
 * @param candidate 参数 `candidate`，用于本函数的分析、匹配或 IR 构造。
 * @param ancestor 参数 `ancestor`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isDescendantOrSelf(const Loop *candidate, const Loop *ancestor) {
    for (const Loop *cursor = candidate; cursor; cursor = cursor->parent)
        if (cursor == ancestor) return true;
    return false;
}

/**
 * @brief 判断 sameAccessIsInjective 所描述的结构、合法性或安全条件是否成立。
 * @param access 参数 `access`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param loopIV 参数 `loopIV`，用于本函数的分析、匹配或 IR 构造。
 * @param loopStart 参数 `loopStart`，用于本函数的分析、匹配或 IR 构造。
 * @param loopBound 参数 `loopBound`，用于本函数的分析、匹配或 IR 构造。
 * @param LI 参数 `LI`，用于本函数的分析、匹配或 IR 构造。
 * @param affine 参数 `affine`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool sameAccessIsInjective(Instruction *access, Loop &loop,
                           PhiInst *loopIV, Value *loopStart,
                           Value *loopBound, LoopInfo &LI,
                           AffineAnalysis &affine) {
    Value *pointer = access->is_store() ? access->get_operand(1)
                                        : access->get_operand(0);
    auto *gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (!gep) return false;

    std::map<PhiInst *, Loop *> ivLoops;
    for (const auto &owned : LI.allLoops()) {
        const InductionDescriptor *control = owned->getInductionDescriptor();
        if (control) ivLoops[control->phi] = owned.get();
    }
    ivLoops[loopIV] = &loop;

    for (unsigned index = 1; index < gep->num_ops(); ++index) {
        AffineExpr expression = affine.analyze(gep->get_operand(index));
        if (!expression.valid || expression.coeffOf(loopIV) == 0) continue;

        __int128 minimum = expression.constant;
        __int128 maximum = expression.constant;
        std::vector<std::pair<long long, long long>> digits;
        bool valid = true;
        for (const auto &[iv, coefficient] : expression.coeffs) {
            auto found = ivLoops.find(iv);
            if (found == ivLoops.end()) {
                valid = false;
                break;
            }
            const InductionDescriptor *control =
                found->second->getInductionDescriptor();
            auto *start = dynamic_cast<ConstantInt *>(
                iv == loopIV ? loopStart : control ? control->start : nullptr);
            auto *bound = dynamic_cast<ConstantInt *>(
                iv == loopIV ? loopBound : control ? control->bound : nullptr);
            bool unitControl = iv == loopIV ||
                               (control && control->constantStep &&
                                *control->constantStep == 1 &&
                                control->predicate == ICmpInst::ICMP_SLT);
            if (!unitControl || !start || !bound ||
                bound->value_ <= start->value_) {
                valid = false;
                break;
            }
            long long first = start->value_;
            long long last = bound->value_ - 1;
            __int128 low = static_cast<__int128>(coefficient) * first;
            __int128 high = static_cast<__int128>(coefficient) * last;
            minimum += std::min(low, high);
            maximum += std::max(low, high);
            if (isDescendantOrSelf(found->second, &loop))
                digits.push_back(
                    {std::llabs(static_cast<long long>(coefficient)),
                     bound->value_ - start->value_});
        }
        if (!valid || minimum < std::numeric_limits<int>::min() ||
            maximum > std::numeric_limits<int>::max()) {
            continue;
        }
        std::sort(digits.begin(), digits.end());
        __int128 representedSpan = 0;
        for (const auto &[coefficient, range] : digits) {
            if (coefficient <= representedSpan) {
                valid = false;
                break;
            }
            representedSpan +=
                static_cast<__int128>(coefficient) * (range - 1);
        }
        if (valid) return true;
    }
    return false;
}

/**
 * @brief 实现 containsPtr 对应的局部分析或变换辅助逻辑。
 * @param values 参数 `values`，用于本函数的分析、匹配或 IR 构造。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
template <typename T>
bool containsPtr(const std::vector<T *> &values, T *value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

/**
 * @brief 实现 addUniquePtr 对应的局部分析或变换辅助逻辑。
 * @param values 参数 `values`，用于本函数的分析、匹配或 IR 构造。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
template <typename T>
bool addUniquePtr(std::vector<T *> &values, T *value) {
    if (containsPtr(values, value))
        return false;
    values.push_back(value);
    return true;
}

/**
 * @brief 判断 isScalarExpansionScratch 所描述的结构、合法性或安全条件是否成立。
 * @param root 参数 `root`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isScalarExpansionScratch(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    return alloca && alloca->isLoopExpansionScratch();
}

/**
 * @brief 实现 rootsNoAlias 对应的局部分析或变换辅助逻辑。
 * @param a 参数 `a`，用于本函数的分析、匹配或 IR 构造。
 * @param b 参数 `b`，用于本函数的分析、匹配或 IR 构造。
 * @param argAA 参数 `argAA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool rootsNoAlias(Value *a, Value *b, const ArgumentAliasAnalysis &argAA) {
    if (!a || !b || a == b) return false;
    if (dynamic_cast<GlobalVariable *>(a) && dynamic_cast<GlobalVariable *>(b))
        return true;
    if (isScalarExpansionScratch(a) || isScalarExpansionScratch(b))
        return true;
    return argAA.noAlias(a, b);
}

/**
 * @brief 判断 hasProvenSafeMemoryRoots 所描述的结构、合法性或安全条件是否成立。
 * @param stores 参数 `stores`，用于本函数的分析、匹配或 IR 构造。
 * @param accesses 参数 `accesses`，用于本函数的分析、匹配或 IR 构造。
 * @param argAA 参数 `argAA`，用于本函数的分析、匹配或 IR 构造。
 * @param reason 拒绝变换或匹配失败的原因。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
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

/**
 * @brief 合并两个已在有符号模范围内的并行部分和，并恢复标准余数范围。
 * @param builder IR 指令构造器。
 * @param module 所属模块。
 * @param p0 第一部分结果。
 * @param p1 第二部分结果。
 * @param mod 正模数常量。
 * @return 不使用高代价 srem 的等价合并结果。
 */
Value *createModuloPartialMerge(IRStmtBuilder *builder, Module *module,
                                Value *p0, Value *p1, ConstantInt *mod) {
    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *p1NonNegative = builder->create_icmp_ge(p1, zero);

    auto *posThreshold = builder->create_isub(mod, p1);
    auto *posWrapped = builder->create_isub(p0, posThreshold);
    auto *posPlain = builder->create_iadd(p0, p1);
    auto *posNeedsWrap = builder->create_icmp_ge(p0, posThreshold);
    auto *posMerged = new SelectInst(posNeedsWrap, posWrapped, posPlain,
                                     builder->get_insert_block());

    auto *negMod = builder->create_isub(zero, mod);
    auto *negThreshold = builder->create_isub(negMod, p1);
    auto *negWrapped = builder->create_isub(p0, negThreshold);
    auto *negPlain = builder->create_iadd(p0, p1);
    auto *negNeedsWrap = builder->create_icmp_le(p0, negThreshold);
    auto *negMerged = new SelectInst(negNeedsWrap, negWrapped, negPlain,
                                     builder->get_insert_block());

    auto *merged = new SelectInst(p1NonNegative, posMerged, negMerged,
                                  builder->get_insert_block());
    return builder->create_isrem(merged, mod);
}

/**
 * @brief 判断 definedInLoop 所描述的结构、合法性或安全条件是否成立。
 * @param v 待检查或映射的 IR 值。
 * @param blocks 相关基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool definedInLoop(Value *v, const std::set<BasicBlock *> &blocks) {
    auto *inst = dynamic_cast<Instruction *>(v);
    return inst && blocks.count(inst->parent_);
}

/**
 * @brief 原地执行 replaceUsesOutsideOutlinedLoop 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param oldValue 参数 `oldValue`，用于本函数的分析、匹配或 IR 构造。
 * @param newValue 参数 `newValue`，用于本函数的分析、匹配或 IR 构造。
 * @param blocks 相关基本块集合。
 * @param outlinedBody 参数 `outlinedBody`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void replaceUsesOutsideOutlinedLoop(Value *oldValue, Value *newValue,
                                    const std::set<BasicBlock *> &blocks,
                                    Function *outlinedBody) {
    std::vector<std::pair<Instruction *, unsigned>> fixes;
    for (auto &use : oldValue->use_list_) {
        auto *user = use.user_;
        if (user && !blocks.count(user->parent_) &&
            (!outlinedBody || user->parent_->parent_ != outlinedBody))
            fixes.push_back({user, use.operand_index_});
    }
    for (auto &fix : fixes)
        fix.first->set_operand(fix.second, newValue);
}

// ScalarExpansion scratch：每个父循环迭代先清零、后累加、再写回。
// 若某个并行循环内完整使用该 scratch，可在 worker 内改成线程私有 alloca。
/**
 * @brief 判断 isPrivatizableScratch 所描述的结构、合法性或安全条件是否成立。
 * @param root 参数 `root`，用于本函数的分析、匹配或 IR 构造。
 * @param blocks 相关基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isPrivatizableScratch(Value *root, const std::set<BasicBlock *> &blocks) {
    if (!isScalarExpansionScratch(root)) return false;
    for (auto &use : root->use_list_) {
        auto *user = use.user_;
        if (!user || !blocks.count(user->parent_)) return false;
    }
    return true;
}

/**
 * @brief 实现 scratchBytes 对应的局部分析或变换辅助逻辑。
 * @param root 参数 `root`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
long long scratchBytes(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    if (!alloca) return -1;
    return typeStorageBytes(alloca->allocated_type());
}

/**
 * @brief 取得可私有化 scratch 根 alloca 的分配类型。
 * @param root 待检查的内存根对象。
 * @return root 为 alloca 时返回其分配类型，否则返回 nullptr。
 */
Type *scratchAllocaType(Value *root) {
    auto *alloca = dynamic_cast<AllocaInst *>(root);
    return alloca ? alloca->allocated_type() : nullptr;
}

/**
 * @brief 判断 isAllowedReductionTerm 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param accumulator 参数 `accumulator`，用于本函数的分析、匹配或 IR 构造。
 * @param ivPhi 参数 `ivPhi`，用于本函数的分析、匹配或 IR 构造。
 * @param ivNext 参数 `ivNext`，用于本函数的分析、匹配或 IR 构造。
 * @param visiting 参数 `visiting`，用于本函数的分析、匹配或 IR 构造。
 * @param BAA 参数 `BAA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isAllowedReductionTerm(Value *value, const Loop &loop,
                            PhiInst *accumulator, PhiInst *ivPhi,
                            Instruction *ivNext,
                            std::set<Value *> &visiting,
                            const BasicAliasAnalysis &BAA) {
    if (!value || value == accumulator)
        return false;
    if (value == ivPhi || value == ivNext)
        return true;
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<Function *>(value) ||
        dynamic_cast<BasicBlock *>(value))
        return true;

    auto *inst = dynamic_cast<Instruction *>(value);
    if (!inst)
        return false;
    if (!loop.blocks.count(inst->parent_))
        return true;
    if (!visiting.insert(value).second)
        return true;

    auto allOperandsAllowed = [&]() {
        for (unsigned i = 0; i < inst->num_ops(); ++i) {
            Value *op = inst->get_operand(i);
            if (dynamic_cast<BasicBlock *>(op) || dynamic_cast<Function *>(op))
                continue;
            if (!isAllowedReductionTerm(op, loop, accumulator, ivPhi, ivNext,
                                        visiting, BAA))
                return false;
        }
        return true;
    };

    bool ok = false;
    if (auto *bin = dynamic_cast<BinaryInst *>(inst)) {
        switch (bin->op_id_) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::Shl:
            ok = allOperandsAllowed();
            break;
        default:
            ok = false;
            break;
        }
    } else if (dynamic_cast<ICmpInst *>(inst)) {
        ok = allOperandsAllowed();
    } else if (auto *load = dynamic_cast<LoadInst *>(inst)) {
        Value *ptr = load->get_operand(0);
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
            ok = isAcceptedMemoryRoot(gepRootBase(gep));
            for (unsigned i = 1; ok && i < gep->num_ops(); ++i)
                ok = isAllowedReductionTerm(gep->get_operand(i), loop,
                                            accumulator, ivPhi, ivNext,
                                            visiting, BAA);
        } else {
            ok = dynamic_cast<GlobalVariable *>(ptr) != nullptr;
        }
    } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        ok = isAcceptedMemoryRoot(gepRootBase(gep));
        for (unsigned i = 1; ok && i < gep->num_ops(); ++i)
            ok = isAllowedReductionTerm(gep->get_operand(i), loop,
                                        accumulator, ivPhi, ivNext, visiting,
                                        BAA);
    } else if (auto *call = dynamic_cast<CallInst *>(inst)) {
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops() - 1));
        ok = callee && BAA.isPure(callee);
        for (unsigned i = 0; ok && i + 1 < call->num_ops(); ++i)
            ok = isAllowedReductionTerm(call->get_operand(i), loop,
                                        accumulator, ivPhi, ivNext, visiting,
                                        BAA);
    } else if (auto *phi = dynamic_cast<PhiInst *>(inst)) {
        ok = true;
        for (unsigned i = 0; ok && i < phi->num_ops(); i += 2)
            ok = isAllowedReductionTerm(phi->get_operand(i), loop,
                                        accumulator, ivPhi, ivNext, visiting,
                                        BAA);
    } else if (inst->op_id_ == Instruction::Select ||
               inst->op_id_ == Instruction::ZExt) {
        ok = allOperandsAllowed();
    }

    visiting.erase(value);
    return ok;
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void ParallelizeLoops::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

/**
 * @brief 匹配 Shape 所描述的 IR 结构并提取结果。
 * @param loop 待检查或变换的循环。
 * @param shape 参数 `shape`，用于本函数的分析、匹配或 IR 构造。
 * @param reason 拒绝变换或匹配失败的原因。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
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

    auto isOne = [](Value *v) {
        auto *c = dynamic_cast<ConstantInt *>(v);
        return c && c->value_ == 1;
    };

    /**
     * @brief 保存并行化候选循环中一个步长为一的整数归纳变量。
     */
    struct IVCandidate {
        PhiInst *phi = nullptr;         ///< 循环头中的候选归纳变量 PHI。
        Value *init = nullptr;          ///< 预头提供的归纳变量初始值。
        Instruction *next = nullptr;    ///< 回边生成下一归纳值的更新指令。
    };
    std::vector<IVCandidate> ivCandidates;
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        auto *ivTy = dynamic_cast<IntegerType *>(phi->type_);
        if (!ivTy || ivTy->num_bits_ != 32) continue;

        Value *init = nullptr;
        Instruction *next = nullptr;
        bool badPred = false;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == loop.preheader) {
                init = phi->get_operand(i);
            } else if (pred == latch) {
                next = dynamic_cast<Instruction *>(phi->get_operand(i));
            } else {
                badPred = true;
                break;
            }
        }
        if (badPred || !init || !next || !next->is_add()) continue;
        Value *a = next->get_operand(0), *b = next->get_operand(1);
        if (!((a == phi && isOne(b)) || (b == phi && isOne(a)))) continue;
        ivCandidates.push_back({phi, init, next});
    }

    if (ivCandidates.empty()) return fail("missing i32 +1 IV phi");

    // 出口：header（while 形）或 latch（do-while 形）的 slt 条件分支
    for (auto &candidate : ivCandidates) {
        for (BasicBlock *cand : {loop.header, latch}) {
            auto *term = cand->get_terminator();
            if (!term || term->num_ops() != 3) continue; // 非 cond br
            auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
            if (!cmp || cmp->icmp_op_ != ICmpInst::ICMP_SLT) continue;
            Value *lhs = cmp->get_operand(0);
            if (lhs != candidate.phi && lhs != candidate.next) continue;
            auto *tSucc = static_cast<BasicBlock *>(term->get_operand(1));
            auto *fSucc = static_cast<BasicBlock *>(term->get_operand(2));
            if (fSucc != exitBlock || !loop.blocks.count(tSucc)) continue;
            shape.ivPhi = candidate.phi;
            shape.init = candidate.init;
            shape.ivNext = candidate.next;
            shape.exitCmp = cmp;
            shape.bound = cmp->get_operand(1);
            shape.exitingBlock = cand;
            shape.latchComparesIV = cand == latch && lhs == candidate.phi;
            break;
        }
        if (shape.exitCmp) break;
    }
    if (!shape.exitCmp) return fail("missing i < bound exit condition");
    if (definedInLoop(shape.bound, loop.blocks))
        return fail("loop bound is defined in loop");

    // 逃逸边检查：循环内所有后继必须仍在循环内，唯一例外是
    // exitingBlock→exitBlock。dedicated exits（plan 1.1）未实现，
    // break 形循环的多条 exiting 边可能汇入同一 exit 块——若放过，
    // 变换只改写一条出口边，其余会留成跨函数分支。
    for (auto *bb : loop.blocksOrdered) {
        auto *term = bb->get_terminator();
        if (!term) return fail("block without terminator");
        for (unsigned i = 0; i < term->num_ops(); i++) {
            auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ || loop.blocks.count(succ)) continue;
            if (bb == shape.exitingBlock && succ == exitBlock) continue;
            return fail("loop has unsupported escape edge");
        }
    }
    return true;
}

/**
 * @brief 判断 isLegalDoall 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @param shape 参数 `shape`，用于本函数的分析、匹配或 IR 构造。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @param argAA 参数 `argAA`，用于本函数的分析、匹配或 IR 构造。
 * @param privatize 参数 `privatize`，用于本函数的分析、匹配或 IR 构造。
 * @param reductions 参数 `reductions`，用于本函数的分析、匹配或 IR 构造。
 * @param scalarReductions 参数 `scalarReductions`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool ParallelizeLoops::isLegalDoall(Loop &loop, const LoopShape &shape,
                                    Function *func, AnalysisManager *AM,
                                    const ArgumentAliasAnalysis &argAA,
                                    std::set<Value *> *privatize,
                                    std::vector<Reduction> *reductions,
                                    std::vector<ScalarReduction> *scalarReductions) {
    // 合法性分三层：过滤调用/alloca，识别可私有化状态和归约，最后证明跨迭代访存无冲突。
    // 任一内存根、PHI 或依赖无法分类时都拒绝并行化，不能依赖运行时输入碰巧无冲突。
    auto fail = [&](const std::string &why) {
        debugPar("reject func=" + func->name_ + " loop=" + loopName(loop) +
                 ": " + why);
        return false;
    };

    BasicAliasAnalysis &BAA = AM->getBasicAA(func->parent_);
    std::vector<Instruction *> stores, accesses;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            if (auto *call = dynamic_cast<CallInst *>(inst)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops() - 1));
                if (!callee || !BAA.isPure(callee))
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

    std::vector<ScalarReduction> localScalarReductions;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == shape.ivPhi) continue;

        auto *phiTy = dynamic_cast<IntegerType *>(phi->type_);
        if (!phiTy || phiTy->num_bits_ != 32)
            return fail("unsupported non-IV header phi");

        Value *init = nullptr;
        Value *latchVal = nullptr;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred == loop.preheader)
                init = phi->get_operand(i);
            else if (pred == shape.latch)
                latchVal = phi->get_operand(i);
            else
                return fail("non-IV phi has unexpected predecessor");
        }

        auto *identity = dynamic_cast<ConstantInt *>(init);
        if (!identity || identity->value_ != 0)
            return fail("non-IV phi is not zero-init scalar reduction");

        auto *rem = dynamic_cast<BinaryInst *>(latchVal);
        ConstantInt *mod = nullptr;
        Value *updateValue = latchVal;
        ScalarModuloSource moduloSource = ScalarModuloSource::None;
        if (rem && rem->op_id_ == Instruction::SRem) {
            mod = dynamic_cast<ConstantInt *>(rem->get_operand(1));
            if (!mod || mod->value_ <= 0)
                return fail("modulo reduction divisor is not positive constant");
            updateValue = rem->get_operand(0);
            moduloSource = ScalarModuloSource::InlineModulo;
        } else {
            rem = nullptr;
        }

        auto *update = dynamic_cast<BinaryInst *>(updateValue);
        if (!update || !(update->is_add() || update->is_sub()))
            return fail("scalar reduction is not add/sub update");

        Value *term = nullptr;
        bool isSub = update->is_sub();
        if (update->is_add()) {
            if (update->get_operand(0) == phi)
                term = update->get_operand(1);
            else if (update->get_operand(1) == phi)
                term = update->get_operand(0);
        } else if (update->get_operand(0) == phi) {
            term = update->get_operand(1);
        }
        if (!term)
            return fail("scalar reduction update does not use accumulator");

        std::set<Value *> visitingTerm;
        if (!isAllowedReductionTerm(term, loop, phi, shape.ivPhi,
                                    shape.ivNext, visitingTerm, BAA))
            return fail("unsupported scalar reduction term");

        localScalarReductions.push_back(
            {phi, update, rem, {}, {}, {}, term, mod, identity, isSub,
             moduloSource});
    }

    if (localScalarReductions.size() > 1)
        return fail("multiple scalar reductions");
    if (!localScalarReductions.empty()) {
        if (!stores.empty())
            return fail("mixed scalar reduction and memory stores");
    }
    if (stores.empty() && localScalarReductions.empty())
        return fail("no stores or scalar reductions");

    for (auto &red : localScalarReductions) {
        red.liveOutUpdateValues.push_back(red.update);
        red.liveOutFinalValues.push_back(red.phi);
        if (red.rem) {
            red.liveOutRems.push_back(red.rem);
            red.liveOutFinalValues.push_back(red.rem);
        }
        bool sawRawLiveOutUse = false;
        std::string rawLiveOutUser;
        bool grew = true;
        while (grew) {
            grew = false;
            for (auto *bb : func->basic_blocks_) {
                if (loop.blocks.count(bb)) continue;
                for (auto *user : bb->instr_list_) {
                    bool usesLiveOutUpdate = false;
                    bool usesLiveOutFinal = false;
                    bool usesLiveOutModuloInput = false;
                    for (unsigned i = 0; i < user->num_ops(); ++i) {
                        Value *op = user->get_operand(i);
                        usesLiveOutUpdate |=
                            std::find(red.liveOutUpdateValues.begin(),
                                      red.liveOutUpdateValues.end(),
                                      op) !=
                            red.liveOutUpdateValues.end();
                        usesLiveOutFinal |=
                            std::find(red.liveOutFinalValues.begin(),
                                      red.liveOutFinalValues.end(),
                                      op) !=
                            red.liveOutFinalValues.end();
                        auto *opRem = dynamic_cast<BinaryInst *>(op);
                        if (containsPtr(red.liveOutUpdateValues, op) ||
                            (red.moduloSource !=
                                 ScalarModuloSource::InlineModulo &&
                             containsPtr(red.liveOutFinalValues, op) &&
                             !containsPtr(red.liveOutRems, opRem)))
                            usesLiveOutModuloInput = true;
                    }
                    if (!usesLiveOutUpdate && !usesLiveOutFinal) continue;

                    if (auto *phi = dynamic_cast<PhiInst *>(user)) {
                        bool onlyForwardsReductionUpdate = true;
                        bool onlyForwardsReductionFinal = true;
                        bool hasLoopIncoming = false;
                        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                            auto *pred =
                                dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
                            if (!loop.blocks.count(pred))
                                continue;
                            hasLoopIncoming = true;
                            onlyForwardsReductionUpdate &=
                                containsPtr(red.liveOutUpdateValues,
                                            phi->get_operand(i));
                            onlyForwardsReductionFinal &=
                                containsPtr(red.liveOutFinalValues,
                                            phi->get_operand(i));
                        }
                        if (hasLoopIncoming && !onlyForwardsReductionUpdate &&
                            !onlyForwardsReductionFinal)
                            return fail("scalar reduction update has mixed live-out phi");
                        if (hasLoopIncoming && onlyForwardsReductionUpdate &&
                            addUniquePtr(red.liveOutUpdateValues,
                                         static_cast<Value *>(phi))) {
                            grew = true;
                        }
                        if (hasLoopIncoming && onlyForwardsReductionFinal &&
                            addUniquePtr(red.liveOutFinalValues,
                                         static_cast<Value *>(phi))) {
                            grew = true;
                        }
                        continue;
                    }

                    bool finalMayNeedModulo =
                        red.moduloSource != ScalarModuloSource::InlineModulo;
                    if (!usesLiveOutModuloInput)
                        continue;

                    auto *rem = dynamic_cast<BinaryInst *>(user);
                    auto *remMod =
                        rem ? dynamic_cast<ConstantInt *>(rem->get_operand(1))
                            : nullptr;
                    bool remUsesLiveOutValue =
                        rem && (containsPtr(red.liveOutUpdateValues,
                                            rem->get_operand(0)) ||
                                (finalMayNeedModulo &&
                                 containsPtr(red.liveOutFinalValues,
                                             rem->get_operand(0)) &&
                                 !containsPtr(
                                     red.liveOutRems,
                                     dynamic_cast<BinaryInst *>(
                                         rem->get_operand(0)))));
                    bool isPositiveConstRem =
                        rem && rem->op_id_ == Instruction::SRem &&
                        remUsesLiveOutValue && remMod && remMod->value_ > 0;
                    if (isPositiveConstRem) {
                        if (!red.mod) {
                            red.mod = remMod;
                            red.moduloSource = ScalarModuloSource::LiveOutModulo;
                        } else if (remMod->value_ != red.mod->value_) {
                            return fail("scalar modulo reduction has mixed live-out moduli");
                        }
                        addUniquePtr(red.liveOutRems, rem);
                        addUniquePtr(red.liveOutFinalValues,
                                     static_cast<Value *>(rem));
                        continue;
                    }

                    if (!sawRawLiveOutUse) {
                        sawRawLiveOutUse = true;
                        rawLiveOutUser = valueName(user);
                    }
                }
            }
        }
        if (red.mod && sawRawLiveOutUse) {
            return fail("scalar modulo reduction has mixed modulo and naked live-out use by " +
                        rawLiveOutUser);
        }
        if (red.mod) {
            debugPar("scalar modulo reduction source=" +
                     std::string(red.moduloSource ==
                                         ScalarModuloSource::InlineModulo
                                     ? "inline"
                                     : "liveout"));
        }
    }

    std::string aliasReason;
    if (!hasProvenSafeMemoryRoots(stores, accesses, argAA, &aliasReason))
        return fail(aliasReason);

    // 标量 live-out：循环内定义被循环外使用 → bail
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (auto *userBB : func->basic_blocks_) {
                if (loop.blocks.count(userBB)) continue;
                for (auto *user : userBB->instr_list_) {
                    bool usesInst = false;
                    for (unsigned i = 0; i < user->num_ops(); ++i)
                        usesInst |= user->get_operand(i) == inst;
                    if (!usesInst) continue;

                    bool allowedScalarReductionLiveOut = false;
                    for (auto &red : localScalarReductions) {
                        allowedScalarReductionLiveOut |=
                            std::find(red.liveOutFinalValues.begin(),
                                      red.liveOutFinalValues.end(), inst) !=
                            red.liveOutFinalValues.end();
                        if (inst == red.update) {
                            auto *userInst = dynamic_cast<BinaryInst *>(user);
                            if (red.mod) {
                                allowedScalarReductionLiveOut |=
                                    std::find(red.liveOutRems.begin(),
                                              red.liveOutRems.end(), userInst) !=
                                    red.liveOutRems.end();
                            } else {
                                allowedScalarReductionLiveOut = true;
                            }
                            allowedScalarReductionLiveOut |=
                                std::find(red.liveOutUpdateValues.begin(),
                                          red.liveOutUpdateValues.end(), user) !=
                                red.liveOutUpdateValues.end();
                        }
                    }
                    if (!allowedScalarReductionLiveOut)
                        return fail("loop-defined value is used outside loop: " +
                                    valueName(inst) + " by " + valueName(user));
                }
            }
        }
    }

    // live-in 类型限制：i32 和指针可通过 ctx 传递；其它类型仍保守拒绝。
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops(); i++) {
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

    LoopInfo &LI = AM->getLoopInfo(func);
    AffineAnalysis AA(LI);
    DependenceAnalysis DA(LI, AA);
    DA.setArgAlias(&argAA);
    DA.setInductionOverride(&loop, shape.ivPhi);

    // 识别逐元素 read-modify-write。它只用于决定嵌套叶循环是否值得
    // 支付并行调度开销，绝不能作为跨迭代依赖的合法性豁免。
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
            for (unsigned i = 1; i < sGep->num_ops(); i++) {
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
                for (unsigned i = 0; i < bin->num_ops(); i++)
                    if (bin->get_operand(i) == a) { usesLoad = true; break; }
                if (!usesLoad) continue;

                auto dep = DA.test(s, a);
                int loopDirection = -1;
                for (size_t i = 0; i < dep.commonLoops.size(); ++i) {
                    if (dep.commonLoops[i] == &loop) {
                        loopDirection = static_cast<int>(i);
                        break;
                    }
                }
                if (dep.provably_independent || loopDirection < 0 ||
                    loopDirection >= static_cast<int>(dep.direction.size()) ||
                    dep.direction[loopDirection] != DependenceAnalysis::DIR_EQ)
                    continue;
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
    bool wavefrontCoincident =
        loop.header->hasSemFlag(SemFlag::WavefrontCoincident);
    if (wavefrontCoincident)
        debugPar("wavefront coincidence proof accepted for loop=" +
                 loopName(loop));
    if (wavefrontCoincident)
        debugWavefront("accepted coincident loop=" + loopName(loop));
    long long privBytes = 0;
    for (auto *s : stores) {
        auto *gep = dynamic_cast<GetElementPtrInst *>(s->get_operand(1));
        Value *base = gep ? gepRootBase(gep) : nullptr;

        bool variesWithIV = false;
        for (unsigned i = 1; gep && i < gep->num_ops(); i++) {
            auto *rec = dynamic_cast<const SCEVAddRecExpr *>(
                SE.getSCEV(gep->get_operand(i)));
            if (rec && rec->loop() == &loop) { variesWithIV = true; break; }
            std::set<Value *> visited;
            if (valueDependsOn(gep->get_operand(i), shape.ivPhi, visited)) {
                variesWithIV = true;
                break;
            }
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
        if (!variesWithIV && !wavefrontCoincident)
            return fail("store address does not vary with loop IV");
    }

    // TriangleInterchange 已证明同根读取只来自当前单元或更早波次。若在这里重做
    // 方向向量检查，会丢失该证明依赖的嵌套循环不等式，因此仅对带精确标记的循环
    // 复用 coincidence 证书；未标记循环仍执行下面的普通 DOALL 检查。
    // 依赖：每个 (store, access) 对需证明独立或仅同迭代依赖

    auto basePriv = [&](Instruction *acc) {
        Value *ptr = acc->is_store() ? acc->get_operand(1) : acc->get_operand(0);
        return privatize->count(gepRootBase(ptr)) != 0;
    };
    for (auto *s : stores) {
        if (wavefrontCoincident) break;
        if (basePriv(s)) continue;
        for (auto *a : accesses) {
            if (basePriv(a)) continue;
            if (s == a &&
                sameAccessIsInjective(s, loop, shape.ivPhi, shape.init,
                                      shape.bound, LI, AA))
                continue;
            auto r = DA.test(s, a);
            if (r.provably_independent) continue;
            int idx = -1;
            for (size_t i = 0; i < r.commonLoops.size(); i++) {
                if (r.commonLoops[i] == &loop) { idx = (int)i; break; }
            }
            if (idx < 0 || idx >= (int)r.direction.size())
                return fail("dependence direction missing for loop");
            if (r.direction[idx] != DependenceAnalysis::DIR_EQ) {
                if (isParDebugEnabled()) {
                    debugPar("carried pair store=" + valueName(s) +
                             " access=" + valueName(a) +
                             " store-root=" + valueName(gepRootBase(
                                 s->get_operand(1))) +
                             " access-root=" + valueName(gepRootBase(
                                 a->is_store() ? a->get_operand(1)
                                               : a->get_operand(0))) +
                             " access-op=" +
                             (a->is_store() ? "store" : "load") +
                             " direction=" +
                             std::to_string(r.direction[idx]));
                }
                return fail("loop carries memory dependence");
            }
        }
    }

    // 常量规模循环必须能摊销工作线程唤醒、区间切分和最终汇合成本。很小的 DOALL
    // 往往只是边界复制或初始化，即使依赖分析证明安全，并行化也会明显变慢。
    auto *ci = dynamic_cast<ConstantInt *>(shape.init);
    auto *cb = dynamic_cast<ConstantInt *>(shape.bound);
    constexpr long long kMinLeafParallelTripCount = 2048;
    constexpr long long kMinNestedParallelTripCount = 64;
    const long long minimumTripCount = loop.children.empty()
        ? kMinLeafParallelTripCount
        : kMinNestedParallelTripCount;
    if (ci && cb && cb->value_ - ci->value_ < minimumTripCount)
        return fail("constant trip count below parallel threshold");

    if (reductions)
        *reductions = std::move(localReductions);
    if (scalarReductions)
        *scalarReductions = std::move(localScalarReductions);

    return true;
}

/**
 * @brief 原地执行 transform 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param shape 参数 `shape`，用于本函数的分析、匹配或 IR 构造。
 * @param func 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param privatize 参数 `privatize`，用于本函数的分析、匹配或 IR 构造。
 * @param reductions 参数 `reductions`，用于本函数的分析、匹配或 IR 构造。
 * @param scalarReductions 参数 `scalarReductions`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void ParallelizeLoops::transform(Loop &loop, const LoopShape &shape,
                                 Function *func, Module *module,
                                 const std::set<Value *> &privatize,
                                 const std::vector<Reduction> &reductions,
                                 const std::vector<ScalarReduction> &scalarReductions) {
    // 将循环体提取为 worker 后，主函数只负责计算任务区间、调用并行运行时和合并归约。
    // 需要私有化的 scratch/标量状态作为 worker 参数传递，禁止线程共享可写临时对象。
    (void)reductions;  // IV-varying reductions need no privatization
    int id = (int)bodies_.size();
    std::vector<ScalarReduction> scalarReds = scalarReductions;

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
    auto *builder = new IRStmtBuilder(retbb);

    for (auto &red : scalarReds) {
        if (!red.mod || red.moduloSource != ScalarModuloSource::LiveOutModulo)
            continue;
        auto *rem = new BinaryInst(module->int32_ty_, Instruction::SRem,
                                   red.update, red.mod, shape.latch, true);
        shape.latch->add_instruction_before_terminator(rem);
        red.rem = rem;
        for (unsigned i = 0; i < red.phi->num_ops(); i += 2) {
            if (red.phi->get_operand(i + 1) ==
                static_cast<Value *>(shape.latch)) {
                red.phi->set_operand(i, rem);
                break;
            }
        }
    }

    // live-in 收集（与 isLegalDoall 同口径）→ ctx 全局
    std::vector<Value *> liveIns;
    for (auto *bb : loop.blocksOrdered) {
        for (auto inst : bb->instr_list_) {
            for (unsigned i = 0; i < inst->num_ops(); i++) {
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
            auto *user = use.user_;
            if (user && loop.blocks.count(user->parent_))
                fixes.push_back({user, use.operand_index_});
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

    GlobalVariable *scalarStartSlot = nullptr;
    GlobalVariable *scalarBoundSlot = nullptr;
    GlobalVariable *scalarPartial0 = nullptr;
    GlobalVariable *scalarPartial1 = nullptr;
    Value *scalarBodyBound = hi;
    Value *scalarSlotPtr = nullptr;
    if (!scalarReds.empty()) {
        scalarStartSlot = new GlobalVariable(
            "__sysy_par_scalar_start_" + std::to_string(id),
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));
        scalarBoundSlot = new GlobalVariable(
            "__sysy_par_scalar_bound_" + std::to_string(id),
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));
        scalarPartial0 = new GlobalVariable(
            "__sysy_par_scalar_partial_" + std::to_string(id) + "_0",
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));
        scalarPartial1 = new GlobalVariable(
            "__sysy_par_scalar_partial_" + std::to_string(id) + "_1",
            module, module->int32_ty_, false,
            new ConstantZero(module->int32_ty_));

        auto *ctxStart = builder->create_load(scalarStartSlot);
        auto *isFirstChunk = builder->create_icmp_eq(lo, ctxStart);
        if (shape.latchComparesIV) {
            auto *ctxBound = builder->create_load(scalarBoundSlot);
            auto *isWholeRange = builder->create_icmp_eq(hi, ctxBound);
            auto *isSplitFirst = builder->create_icmp_ne(
                isWholeRange, new ConstantInt(module->int1_ty_, 1));
            auto *needsTrim = new BinaryInst(module->int1_ty_, Instruction::And,
                                             isFirstChunk, isSplitFirst, entry);
            auto *trimmedHi = builder->create_isub(
                hi, new ConstantInt(module->int32_ty_, 1));
            scalarBodyBound = new SelectInst(needsTrim, trimmedHi, hi, entry);
        }
        scalarSlotPtr = new SelectInst(isFirstChunk, scalarPartial0,
                                       scalarPartial1, entry);
    }
    builder->create_br(loop.header);
    entry->add_succ_basic_block(loop.header);

    // 环内对 live-in 的引用 → ctx load
    for (size_t k = 0; k < liveIns.size(); k++) {
        Value *v = liveIns[k];
        std::vector<std::pair<Instruction *, unsigned>> fixes;
        for (auto &use : v->use_list_) {
            auto *user = use.user_;
            if (user && loop.blocks.count(user->parent_))
                fixes.push_back({user, use.operand_index_});
        }
        for (auto &f : fixes)
            f.first->set_operand(f.second, ctxLoads[k]);
    }

    // IV init → lo（入边块 preheader → entry）；出口比较 bound → hi
    for (unsigned i = 0; i < shape.ivPhi->num_ops(); i += 2) {
        if (shape.ivPhi->get_operand(i + 1) ==
            static_cast<Value *>(loop.preheader)) {
            shape.ivPhi->set_operand(i, lo);
            shape.ivPhi->set_operand(i + 1, entry);
        }
    }
    for (auto &red : scalarReds) {
        for (unsigned i = 0; i < red.phi->num_ops(); i += 2) {
            if (red.phi->get_operand(i + 1) ==
                static_cast<Value *>(loop.preheader))
                red.phi->set_operand(i + 1, entry);
        }
    }
    shape.exitCmp->set_operand(1, scalarBodyBound);

    builder->set_insert_point(retbb);
    for (auto &red : scalarReds) {
        Value *partial = nullptr;
        if (red.mod) {
            partial = shape.exitingBlock == loop.header
                          ? static_cast<Value *>(red.phi)
                          : static_cast<Value *>(red.rem);
        } else {
            partial = shape.exitingBlock == loop.header
                          ? static_cast<Value *>(red.phi)
                          : static_cast<Value *>(red.update);
        }
        builder->create_store(partial, scalarSlotPtr);
    }
    builder->create_void_ret();

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
    for (unsigned i = 0; i < exitTerm->num_ops(); i++) {
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
    for (auto &red : scalarReds) {
        builder->create_store(shape.init, scalarStartSlot);
        builder->create_store(shape.bound, scalarBoundSlot);
        builder->create_store(red.identity, scalarPartial0);
        builder->create_store(red.identity, scalarPartial1);
    }
    builder->create_call(parallelForDecl_,
                         {new ConstantInt(module->int32_ty_, id), shape.init,
                          shape.bound});
    for (auto &red : scalarReds) {
        auto *p0 = builder->create_load(scalarPartial0);
        auto *p1 = builder->create_load(scalarPartial1);
        Value *merged = red.mod
                            ? createModuloPartialMerge(builder, module, p0, p1,
                                                       red.mod)
                            : static_cast<Value *>(builder->create_iadd(p0, p1));
        std::vector<Value *> exitForwardValues = red.liveOutUpdateValues;
        for (auto *value : red.liveOutFinalValues) {
            if (std::find(exitForwardValues.begin(), exitForwardValues.end(),
                          value) == exitForwardValues.end())
                exitForwardValues.push_back(value);
        }
        for (auto *value : exitForwardValues) {
            auto *phi = dynamic_cast<PhiInst *>(value);
            if (!phi || phi->parent_ != shape.exitBlock) continue;
            for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                if (phi->get_operand(i + 1) !=
                    static_cast<Value *>(shape.exitingBlock))
                    continue;
                phi->set_operand(i, merged);
                phi->set_operand(i + 1, parCall);
            }
        }
        if (red.mod) {
            for (auto *value : red.liveOutFinalValues)
                replaceUsesOutsideOutlinedLoop(value, merged, loop.blocks, bodyFn);
        } else {
            for (auto *value : red.liveOutUpdateValues)
                replaceUsesOutsideOutlinedLoop(value, merged, loop.blocks, bodyFn);
            for (auto *value : red.liveOutFinalValues)
                replaceUsesOutsideOutlinedLoop(value, merged, loop.blocks, bodyFn);
        }
    }
    builder->create_br(shape.exitBlock);
    parCall->add_succ_basic_block(shape.exitBlock);
    shape.exitBlock->add_pre_basic_block(parCall);

    auto *preTerm = loop.preheader->get_terminator();
    for (unsigned i = 0; i < preTerm->num_ops(); i++) {
        if (preTerm->get_operand(i) == static_cast<Value *>(loop.header))
            preTerm->set_operand(i, parCall);
    }
    loop.preheader->remove_succ_basic_block(loop.header);
    loop.preheader->add_succ_basic_block(parCall);
    parCall->add_pre_basic_block(loop.preheader);

    delete builder;
    bodies_.push_back(bodyFn);
    if (loop.header->hasSemFlag(SemFlag::WavefrontCoincident))
        debugWavefront("outlined func=" + func->name_ + " header=" +
                       loop.header->name_ + " id=" + std::to_string(id));
    debugPar("parallelized func=" + func->name_ +
             " header=" + loop.header->name_ + " id=" + std::to_string(id));
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
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
                std::set<Value *> privatize;
                std::vector<Reduction> reductions;
                std::vector<ScalarReduction> scalarReductions;
                if (!isLegalDoall(*loop, shape, func, &AM, argAA, &privatize,
                                   &reductions, &scalarReductions))
                    continue;
                // 并行化嵌套叶子循环会在父循环每轮都触发一次常驻线程协议，频繁发布
                // live-in 上下文和同步会吞没有效工作。归约循环仍保留；其他情况要求
                // 候选自身还含子循环，使一次调度能由整片嵌套区域的工作量摊销。
                if (loop->depth != 0 && reductions.empty() &&
                    scalarReductions.empty() &&
                    loop->children.empty() &&
                    !loop->header->hasSemFlag(
                        SemFlag::WavefrontCoincident)) {
                    debugPar("skip func=" + func->name_ + " loop=" +
                             loopName(*loop) +
                             ": nested leaf loop without reduction");
                    continue;
                }
                transform(*loop, shape, func, module, privatize, reductions,
                          scalarReductions);
                AM.clear(func);
                changed = true;
                localChanged = true;
                break;
            }
        }
    }

    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
