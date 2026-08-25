/**
 * @file indVarStrengthReduce.cpp
 * @brief 归纳变量强度削减：把依赖归纳变量的乘法、地址计算等高代价表达式改写为循环递推更新。
 * @details 从基本归纳变量建立线性 SCEV，把循环内乘法/GEP 改为预头初始化加回边增量，并在每轮改写后刷新分析。
 */

#include "../../../include/mid/opt/indVarStrengthReduce.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void IndVarStrengthReduce::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses IndVarStrengthReduce::execute(Module *module, AnalysisManager &AM) {
    for (auto func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func, AM);
    }
    return PreservedAnalyses::none();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void IndVarStrengthReduce::runOnFunction(Function *func, AnalysisManager &AM) {
    if (func->basic_blocks_.empty()) return;

    // 每个循环处理完会全量失效 AM（含 LoopInfo），Loop* 不能跨失效持有
    // ——先收集稳定 header 列表，每轮按 header 重查（与 LICM 同策略）。
    // 顺序：由内向外（旧实现按回边在块布局中的出现序，嵌套时即内层先）。
    std::vector<BasicBlock *> headers;
    {
        LoopInfo &LI = AM.getLoopInfo(func);
        std::vector<Loop *> loops;
        for (auto &l : LI.allLoops())
            loops.push_back(l.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *a, Loop *b) { return a->depth > b->depth; });
        for (auto *l : loops)
            headers.push_back(l->header);
    }

    Module *module = func->parent_;
    for (auto *header : headers) {
        LoopInfo &LI = AM.getLoopInfo(func);
        Loop *loop = nullptr;
        for (auto &l : LI.allLoops()) {
            if (l->header == header) {
                loop = l.get();
                break;
            }
        }
        if (!loop) continue;
        // 自环 header 在语义上可以做 IVSR，但若它还是外层嵌套，会制造大量跨子循环
        // 存活的指针 PHI 并显著增加寄存器压力，因此只处理最内层自环。
        if (loop->singleLatch() == loop->header) {
            if (loop->blocks.size() != 1) continue;
            bool hasNestedLoop = false;
            for (auto &other : LI.allLoops()) {
                if (other.get() != loop && other->depth > loop->depth &&
                    loop->blocks.count(other->header)) {
                    hasNestedLoop = true;
                    break;
                }
            }
            if (hasNestedLoop) continue;
        }
        ScalarEvolution &SE = AM.getScalarEvolution(func);
        processLoop(*loop, func, module, SE);
        AM.invalidateFunction(func, PreservedAnalyses::none());
    }
}

// -----------------------------------------------------------------------
// 判断值是否在循环中不变
// -----------------------------------------------------------------------
/**
 * @brief 判断 isLoopInvariant 所描述的结构、合法性或安全条件是否成立。
 * @param val 待检查或映射的 IR 值。
 * @param loopBlocks 循环所包含的基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool IndVarStrengthReduce::isLoopInvariant(Value *val, const std::set<BasicBlock *> &loopBlocks) {
    if (dynamic_cast<Constant *>(val)) return true;
    if (dynamic_cast<Argument *>(val)) return true;
    if (dynamic_cast<GlobalVariable *>(val)) return true;

    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst) return false;

    return !loopBlocks.count(inst->parent_);
}

// -----------------------------------------------------------------------
// 检查 value 通过 phi 链是否最终归结为 target
// -----------------------------------------------------------------------
/**
 * @brief 实现 resolvesTo 对应的局部分析或变换辅助逻辑。
 * @param val 待检查或映射的 IR 值。
 * @param target 参数 `target`，用于本函数的分析、匹配或 IR 构造。
 * @param visited 递归遍历使用的已访问集合，用于避免环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool resolvesTo(Value *val, Value *target, std::set<Value *> &visited) {
    if (val == target) return true;
    if (visited.count(val)) return false;

    auto *phi = dynamic_cast<PhiInst *>(val);
    if (!phi) return false;

    visited.insert(val);
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        auto subVisited = visited;
        if (!resolvesTo(phi->get_operand(i), target, subVisited))
            return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// 获取 value 在 preheader 中对应的可用值
// 若 value 是循环中的 phi，返回来自 preheader 的入边值；否则返回自身
// -----------------------------------------------------------------------
/**
 * @brief 追溯循环内 PHI 在进入循环前对应的循环外初值。
 * @param val 待追溯的值；循环外定义会直接返回。
 * @param loopBlocks 当前循环包含的基本块集合。
 * @param visited 已访问值集合，用于终止 PHI 环递归。
 * @return 找到时返回循环外可用值；无法追溯时返回 nullptr 或原 PHI。
 */
static Value *getEntryValue(Value *val, const std::set<BasicBlock *> &loopBlocks,
                             std::set<Value *> &visited) {
    auto *inst = dynamic_cast<Instruction *>(val);
    if (!inst || !loopBlocks.count(inst->parent_)) return val;

    auto *phi = dynamic_cast<PhiInst *>(val);
    if (!phi) return nullptr;
    if (!visited.insert(val).second) return val;

    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        Value *incoming = phi->get_operand(i);
        auto *inInst = dynamic_cast<Instruction *>(incoming);
        if (!inInst || !loopBlocks.count(inInst->parent_))
            return incoming;
    }
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        Value *result = getEntryValue(phi->get_operand(i), loopBlocks, visited);
        auto *rInst = dynamic_cast<Instruction *>(result);
        if (!rInst || !loopBlocks.count(rInst->parent_))
            return result;
    }
    return val;
}

// -----------------------------------------------------------------------
// 识别 SSA 基本归纳变量：phi(init, add/sub(X, stride)) 其中 X 归结为 phi
// -----------------------------------------------------------------------
/**
 * @brief 收集或查找 findBasicIVs 所需的信息。
 * @param loop 待检查或变换的循环。
 * @return 返回计算、分析或构造得到的结果。
 */
std::vector<IndVarStrengthReduce::BasicIV> IndVarStrengthReduce::findBasicIVs(Loop &loop) {
    std::vector<BasicIV> ivs;

    // 1) SSA 基本归纳变量：phi(init, add/sub(X, stride)) 其中 X 归结为 phi
    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);

        if (phi->type_->tid_ != Type::IntegerTyID) continue;

        Value *insideVal = nullptr;
        Value *outsideVal = nullptr;
        BasicBlock *insideBB = nullptr;

        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (loop.blocks.count(pred)) {
                if (insideVal) { insideVal = nullptr; break; }
                insideVal = phi->get_operand(i);
                insideBB = pred;
            } else {
                if (outsideVal) { outsideVal = nullptr; break; }
                outsideVal = phi->get_operand(i);
            }
        }

        if (!insideVal || !outsideVal || !insideBB) continue;

        auto *insideInst = dynamic_cast<Instruction *>(insideVal);
        if (!insideInst) continue;
        if (!insideInst->is_add() && !insideInst->is_sub()) continue;

        Value *op0 = insideInst->get_operand(0);
        Value *op1 = insideInst->get_operand(1);

        Value *stride = nullptr;
        bool isAdd = insideInst->is_add();

        {
            std::set<Value *> vis;
            if (resolvesTo(op0, phi, vis) && isLoopInvariant(op1, loop.blocks))
                stride = op1;
        }
        if (!stride && isAdd) {
            std::set<Value *> vis;
            if (resolvesTo(op1, phi, vis) && isLoopInvariant(op0, loop.blocks))
                stride = op0;
        }

        if (!stride || !dynamic_cast<ConstantInt *>(stride)) continue;

        BasicIV iv;
        iv.phi = phi;
        iv.initVal = outsideVal;
        iv.stride = stride;
        iv.isAdd = isAdd;
        iv.latch = insideBB;
        iv.updateInst = insideInst;
        ivs.push_back(iv);
    }

    return ivs;
}

// -----------------------------------------------------------------------
// 确保循环有唯一 preheader
// -----------------------------------------------------------------------
/**
 * @brief 返回循环现有 preheader，或创建一个专用 preheader 并修复 PHI 入边。
 * @param loop 需要规范化入口的循环。
 * @param func 循环所属函数。
 * @param module 所属模块，用于创建基本块。
 * @return 循环最终使用的专用 preheader。
 */
BasicBlock *IndVarStrengthReduce::ensurePreheader(Loop &loop, Function *func, Module *module) {
    if (loop.preheader) return loop.preheader;

    auto *preheader = new BasicBlock(module, "label_ivsr_ph", func);

    std::vector<BasicBlock *> externalPreds;
    for (auto pred : loop.header->pre_bbs_) {
        if (!loop.blocks.count(pred))
            externalPreds.push_back(pred);
    }

    for (auto pred : externalPreds) {
        auto *term = pred->get_terminator();
        for (unsigned i = 0; i < term->num_ops(); i++) {
            if (term->get_operand(i) == loop.header)
                term->set_operand(i, preheader);
        }
        pred->remove_succ_basic_block(loop.header);
        pred->add_succ_basic_block(preheader);
        preheader->add_pre_basic_block(pred);
        loop.header->remove_pre_basic_block(pred);
    }

    auto *builder = new IRStmtBuilder(preheader);
    builder->create_br(loop.header);
    delete builder;

    preheader->add_succ_basic_block(loop.header);
    loop.header->add_pre_basic_block(preheader);

    for (auto inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        auto *phi = static_cast<PhiInst *>(inst);
        for (auto extPred : externalPreds) {
            for (unsigned i = 0; i < phi->num_ops(); i += 2) {
                if (phi->get_operand(i + 1) == extPred) {
                    Value *val = phi->get_operand(i);
                    phi->add_phi_pair_operand(val, preheader);
                    phi->remove_operands(i, i + 1);
                    break;
                }
            }
        }
    }

    return preheader;
}

// -----------------------------------------------------------------------
// 计算 GEP 中 IV 推进 1 时，地址需要增加多少元素
// 例如 A[N][M] 中 IV 在索引 1 (i) 时步长为 M，在索引 2 (j) 时步长为 1
// -----------------------------------------------------------------------
/**
 * @brief 计算 computeElementStride 所描述的派生信息，供合法性或收益判断使用。
 * @param gep 参数 `gep`，用于本函数的分析、匹配或 IR 构造。
 * @param ivOpIdx 参数 `ivOpIdx`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
static int computeElementStride(GetElementPtrInst *gep, unsigned ivOpIdx) {
    Type *ty = static_cast<PointerType *>(gep->get_operand(0)->type_)->contained_;
    for (unsigned i = 1; i < ivOpIdx; i++) {
        if (auto *arrTy = dynamic_cast<ArrayType *>(ty))
            ty = arrTy->contained_;
        else
            return 1;
    }
    int stride = 0;
    if (auto *arrTy = dynamic_cast<ArrayType *>(ty)) {
        stride = arrTy->num_elements_;
        Type *inner = arrTy->contained_;
        while (auto *ia = dynamic_cast<ArrayType *>(inner)) {
            stride *= ia->num_elements_;
            inner = ia->contained_;
        }
    }
    return stride > 0 ? stride : 1;
}

/**
 * @brief 判断 fitsInt 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool fitsInt(long long value) {
    return value >= std::numeric_limits<int>::min() &&
           value <= std::numeric_limits<int>::max();
}

/**
 * @brief 判断 isI32 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isI32(Value *value) {
    auto *intTy = value ? dynamic_cast<IntegerType *>(value->type_) : nullptr;
    return intTy && intTy->num_bits_ == 32;
}

/**
 * @brief 判断 isI32SCEV 所描述的结构、合法性或安全条件是否成立。
 * @param s 参数 `s`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isI32SCEV(const SCEV *s) {
    auto *intTy = s ? dynamic_cast<IntegerType *>(s->type()) : nullptr;
    return intTy && intTy->num_bits_ == 32;
}

/**
 * @brief 判断 isIVSRSCEVDebugEnabled 所描述的结构、合法性或安全条件是否成立。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
static bool isIVSRSCEVDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_IVSR_SCEV") != nullptr;
    return enabled;
}

/**
 * @brief 生成 debugLinearizedGEP 对应的调试诊断，不参与程序语义。
 * @param func 待分析或改写的函数。
 * @param loopHeader 参数 `loopHeader`，用于本函数的分析、匹配或 IR 构造。
 * @param gep 参数 `gep`，用于本函数的分析、匹配或 IR 构造。
 * @param info 参数 `info`，用于本函数的分析、匹配或 IR 构造。
 * @param coeff 参数 `coeff`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
static void debugLinearizedGEP(Function *func, BasicBlock *loopHeader,
                               GetElementPtrInst *gep, const SCEVGEPInfo &info,
                               int coeff) {
    if (!isIVSRSCEVDebugEnabled()) return;

    std::cerr << "[IVSR:SCEV] function=" << (func ? func->name_ : "<null>")
              << " loop_header=" << (loopHeader ? loopHeader->name_ : "<null>")
              << " gep=%" << gep->name_
              << " coeff=" << coeff
              << " offset=" << (info.elementOffset ? info.elementOffset->print() : "<null>")
              << " shape=[";
    for (size_t i = 0; i < info.shape.size(); i++) {
        if (i) std::cerr << ",";
        std::cerr << info.shape[i];
    }
    std::cerr << "]\n";
}

/**
 * @brief 判断 isSCEVLoopInvariant 所描述的结构、合法性或安全条件是否成立。
 * @param s 参数 `s`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param SE 参数 `SE`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool IndVarStrengthReduce::isSCEVLoopInvariant(const SCEV *s, const Loop &loop,
                                               ScalarEvolution &SE) {
    (void)SE;
    if (!s) return false;

    switch (s->kind()) {
    case SCEVKind::Constant:
        return true;
    case SCEVKind::CouldNotCompute:
        return false;
    case SCEVKind::Unknown: {
        auto *unknown = static_cast<const SCEVUnknown *>(s);
        return isLoopInvariant(unknown->value(), loop.blocks);
    }
    case SCEVKind::AddExpr:
    case SCEVKind::MulExpr: {
        auto *nary = static_cast<const SCEVNAryExpr *>(s);
        for (auto *op : nary->operands()) {
            if (!isSCEVLoopInvariant(op, loop, SE)) return false;
        }
        return true;
    }
    case SCEVKind::AddRecExpr: {
        auto *addrec = static_cast<const SCEVAddRecExpr *>(s);
        if (addrec->loop() && addrec->loop()->header &&
            loop.blocks.count(addrec->loop()->header))
            return false;
        return isSCEVLoopInvariant(addrec->start(), loop, SE) &&
               isSCEVLoopInvariant(addrec->step(), loop, SE);
    }
    }
    return false;
}

/**
 * @brief 计算 linearizeSCEVForIV 所描述的派生信息，供合法性或收益判断使用。
 * @param s 参数 `s`，用于本函数的分析、匹配或 IR 构造。
 * @param iv 参数 `iv`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param SE 参数 `SE`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
IndVarStrengthReduce::LinearIVExpr
IndVarStrengthReduce::linearizeSCEVForIV(const SCEV *s, const BasicIV &iv,
                                         const Loop &loop, ScalarEvolution &SE) {
    LinearIVExpr invalid;
    if (!s || !isI32SCEV(s)) return invalid;

    if (auto *c = dynamic_cast<const SCEVConstant *>(s)) {
        if (!fitsInt(c->value())) return invalid;
        LinearIVExpr result;
        result.valid = true;
        result.constOffset = c->value();
        return result;
    }

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s)) {
        LinearIVExpr result;
        if (unknown->value() == iv.phi) {
            result.valid = true;
            result.coeff = 1;
            return result;
        }
        if (!isSCEVLoopInvariant(s, loop, SE)) return invalid;
        result.valid = true;
        result.offset = s;
        return result;
    }

    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(s)) {
        LinearIVExpr result;
        if (addrec->phi() == iv.phi) {
            result.valid = true;
            result.coeff = 1;
            return result;
        }
        if (!isSCEVLoopInvariant(s, loop, SE)) return invalid;
        result.valid = true;
        result.offset = s;
        return result;
    }

    if (s->kind() == SCEVKind::CouldNotCompute)
        return invalid;

    if (isSCEVLoopInvariant(s, loop, SE)) {
        LinearIVExpr result;
        result.valid = true;
        result.offset = s;
        return result;
    }

    if (auto *add = dynamic_cast<const SCEVAddExpr *>(s)) {
        LinearIVExpr result;
        result.valid = true;

        for (auto *op : add->operands()) {
            LinearIVExpr term = linearizeSCEVForIV(op, iv, loop, SE);
            if (!term.valid) return invalid;

            // 带运行时系数的 IV 项不能再与其他 IV 项相加。例如 `j*colsize+2*j`
            // 需要系数 `colsize+2`，当前单一 (coeff, coeffVal) 表示无法表达；
            // 因此运行时系数项若存在，必须是唯一的 IV 项。
            bool resultHasIV = result.coeff != 0 || result.coeffVal;
            bool termHasIV = term.coeff != 0 || term.coeffVal;
            if ((result.coeffVal || term.coeffVal) && resultHasIV && termHasIV)
                return invalid;
            if (term.coeffVal)
                result.coeffVal = term.coeffVal;

            long long coeff = static_cast<long long>(result.coeff) + term.coeff;
            if (!fitsInt(coeff)) return invalid;
            result.coeff = static_cast<int>(coeff);

            result.constOffset += term.constOffset;
            if (!fitsInt(result.constOffset)) return invalid;

            if (term.offset) {
                if (result.offset) return invalid;
                result.offset = term.offset;
            }
        }
        return result;
    }

    if (auto *mul = dynamic_cast<const SCEVMulExpr *>(s)) {
        bool sawIVTerm = false;
        LinearIVExpr ivTerm;
        long long multiplier = 1;
        Value *multiplierVal = nullptr; // one loop-invariant variable multiplier

        for (auto *op : mul->operands()) {
            LinearIVExpr term = linearizeSCEVForIV(op, iv, loop, SE);
            if (!term.valid) return invalid;

            if (term.coeff == 0 && !term.coeffVal) {
                if (term.offset) {
                    // 循环不变但运行时可变的乘数最多允许一个，且必须是单个 SSA 值
                    // （SCEVUnknown），才能直接作为运行时指针步长；更复杂表达式拒绝。
                    auto *unk = dynamic_cast<const SCEVUnknown *>(term.offset);
                    if (!unk || multiplierVal || term.constOffset != 0)
                        return invalid;
                    multiplierVal = unk->value();
                    continue;
                }
                multiplier *= term.constOffset;
                if (!fitsInt(multiplier)) return invalid;
                continue;
            }

            if (sawIVTerm || term.offset || term.coeffVal) return invalid;
            sawIVTerm = true;
            ivTerm = term;
        }

        if (!sawIVTerm) return invalid;

        long long coeff = static_cast<long long>(ivTerm.coeff) * multiplier;
        long long constOffset = ivTerm.constOffset * multiplier;
        if (!fitsInt(coeff) || !fitsInt(constOffset)) return invalid;

        LinearIVExpr result;
        result.valid = true;
        result.coeff = static_cast<int>(coeff);
        result.constOffset = constOffset;
        result.coeffVal = multiplierVal;
        return result;
    }

    return invalid;
}

/**
 * @brief 在 preheader 中物化线性 IV 表达式的循环不变偏移部分。
 * @param expr 已线性化的 IV 表达式。
 * @param preheader 新指令的插入基本块。
 * @param loop 当前循环，用于验证偏移操作数是否循环不变。
 * @param builder IR 指令构造器。
 * @param module 所属模块，用于构造 i32 常量。
 * @return 成功时返回偏移值，表达式不可安全物化时返回 nullptr。
 */
Value *IndVarStrengthReduce::materializeOffsetInPreheader(const LinearIVExpr &expr,
                                                          BasicBlock *preheader,
                                                          const Loop &loop,
                                                          IRStmtBuilder *builder,
                                                          Module *module) {
    if (!fitsInt(expr.constOffset)) return nullptr;

    Value *offsetVal = nullptr;
    long long constOffset = expr.constOffset;

    if (expr.offset) {
        if (auto *c = dynamic_cast<const SCEVConstant *>(expr.offset)) {
            constOffset += c->value();
            if (!fitsInt(constOffset)) return nullptr;
        } else if (auto *unknown = dynamic_cast<const SCEVUnknown *>(expr.offset)) {
            offsetVal = unknown->value();
            if (!isI32(offsetVal) || !isLoopInvariant(offsetVal, loop.blocks))
                return nullptr;
        } else if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(expr.offset)) {
            offsetVal = addrec->phi();
            if (!isI32(offsetVal) || !isLoopInvariant(offsetVal, loop.blocks))
                return nullptr;
        } else {
            offsetVal = materializeInvariantSCEV(expr.offset, preheader, loop,
                                                builder, module);
            if (!offsetVal) return nullptr;
        }
    }

    if (!offsetVal)
        return new ConstantInt(module->int32_ty_, static_cast<int>(constOffset));

    if (auto *ci = dynamic_cast<ConstantInt *>(offsetVal)) {
        long long folded = static_cast<long long>(ci->value_) + constOffset;
        if (!fitsInt(folded)) return nullptr;
        return new ConstantInt(module->int32_ty_, static_cast<int>(folded));
    }

    if (constOffset == 0)
        return offsetVal;

    builder->set_insert_point(preheader);
    auto *add = builder->create_iadd(offsetVal,
                                     new ConstantInt(module->int32_ty_,
                                                     static_cast<int>(constOffset)));
    preheader->remove_instr(add);
    preheader->add_instruction_before_terminator(add);
    return add;
}

/**
 * @brief 判断 canMaterializeInvariantSCEV 所描述的结构、合法性或安全条件是否成立。
 * @param s 参数 `s`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool IndVarStrengthReduce::canMaterializeInvariantSCEV(const SCEV *s,
                                                       const Loop &loop) {
    if (!s || !isI32SCEV(s)) return false;

    if (dynamic_cast<const SCEVConstant *>(s)) return true;

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s)) {
        Value *value = unknown->value();
        return isI32(value) && isLoopInvariant(value, loop.blocks);
    }

    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(s)) {
        Value *value = addrec->phi();
        return isI32(value) && isLoopInvariant(value, loop.blocks);
    }

    if (auto *nary = dynamic_cast<const SCEVNAryExpr *>(s)) {
        if (s->kind() != SCEVKind::AddExpr && s->kind() != SCEVKind::MulExpr)
            return false;
        for (auto *op : nary->operands()) {
            if (!canMaterializeInvariantSCEV(op, loop)) return false;
        }
        return true;
    }

    return false;
}

/**
 * @brief 把由常量、循环外值及加乘节点组成的不变 SCEV 生成为 IR。
 * @param s 待物化的 ScalarEvolution 表达式。
 * @param preheader 新指令的插入基本块。
 * @param loop 当前循环，用于检查不变性。
 * @param builder IR 指令构造器。
 * @param module 所属模块，用于取得类型和构造常量。
 * @return 成功时返回物化值，不支持该 SCEV 形态时返回 nullptr。
 */
Value *IndVarStrengthReduce::materializeInvariantSCEV(
    const SCEV *s, BasicBlock *preheader, const Loop &loop,
    IRStmtBuilder *builder, Module *module) {
    if (!canMaterializeInvariantSCEV(s, loop)) return nullptr;

    if (auto *c = dynamic_cast<const SCEVConstant *>(s))
        return new ConstantInt(module->int32_ty_, static_cast<int>(c->value()));

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(s))
        return unknown->value();

    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(s))
        return addrec->phi();

    auto *nary = dynamic_cast<const SCEVNAryExpr *>(s);
    if (!nary || nary->operands().empty()) return nullptr;

    Value *result = materializeInvariantSCEV(nary->operands()[0], preheader,
                                             loop, builder, module);
    if (!result) return nullptr;

    for (size_t i = 1; i < nary->operands().size(); i++) {
        Value *rhs = materializeInvariantSCEV(nary->operands()[i], preheader,
                                              loop, builder, module);
        if (!rhs) return nullptr;

        builder->set_insert_point(preheader);
        Instruction *inst = nullptr;
        if (s->kind() == SCEVKind::AddExpr)
            inst = builder->create_iadd(result, rhs);
        else if (s->kind() == SCEVKind::MulExpr)
            inst = builder->create_imul(result, rhs);
        else
            return nullptr;
        preheader->remove_instr(inst);
        preheader->add_instruction_before_terminator(inst);
        result = inst;
    }

    return result;
}

/**
 * @brief 判断 canMaterializeOffsetInPreheader 所描述的结构、合法性或安全条件是否成立。
 * @param expr 参数 `expr`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool IndVarStrengthReduce::canMaterializeOffsetInPreheader(const LinearIVExpr &expr,
                                                           const Loop &loop) {
    if (!fitsInt(expr.constOffset)) return false;
    if (!expr.offset) return true;

    if (auto *c = dynamic_cast<const SCEVConstant *>(expr.offset))
        return fitsInt(expr.constOffset + c->value());

    if (auto *unknown = dynamic_cast<const SCEVUnknown *>(expr.offset)) {
        Value *value = unknown->value();
        return isI32(value) && isLoopInvariant(value, loop.blocks);
    }

    if (auto *addrec = dynamic_cast<const SCEVAddRecExpr *>(expr.offset)) {
        Value *value = addrec->phi();
        return isI32(value) && isLoopInvariant(value, loop.blocks);
    }

    return canMaterializeInvariantSCEV(expr.offset, loop);
}

// -----------------------------------------------------------------------
// 主体：对循环中的派生 IV 进行强度削弱
// -----------------------------------------------------------------------
/**
 * @brief 实现 processLoop 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param func 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param SE 参数 `SE`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void IndVarStrengthReduce::processLoop(Loop &loop, Function *func, Module *module,
                                       ScalarEvolution &SE) {
    // 先收集基本归纳变量及其线性 GEP 候选，再在预头物化初值、在回边累加步长。
    // 候选收集与 IR 提交分离，避免某个地址表达式失败时污染其他递推链。
    // 某些前序 Pass 已物化面向目标的地址递推链；再次削弱其派生 GEP 会生成平行
    // 指针 PHI 并破坏原链，因此带该语义标记的循环直接跳过。
    if (loop.header->hasSemFlag(SemFlag::TargetPointerRecurrenceLoop))
        return;

    int inLoopHeaderPreds = 0;
    for (auto *pred : loop.header->pre_bbs_) {
        if (loop.blocks.count(pred))
            inLoopHeaderPreds++;
    }
    if (inLoopHeaderPreds != 1)
        return;

    // 向量循环与标量循环共享仿射递推证明，仍可作为候选；下面仍要求起始地址可物化，
    // 且常量指针步长有界。
    auto ivs = findBasicIVs(loop);
    if (ivs.empty()) return;

    BasicBlock *preheader = ensurePreheader(loop, func, module);
    auto *builder = new IRStmtBuilder(preheader);

    for (auto &iv : ivs) {
        auto *initCI = dynamic_cast<ConstantInt *>(iv.initVal);
        if (!initCI && (!isI32(iv.initVal) || !loop.children.empty())) continue;

        /**
         * @brief 记录可由显式 GEP 改写为循环指针递推的地址计算候选。
         */
        struct GEPCandidate {
            GetElementPtrInst *gep;                 ///< 待强度削弱的原始 GEP 指令。
            unsigned ivOpIdx;                       ///< 含归纳变量的 GEP 操作数下标。
            int coeff;                              ///< 归纳变量在索引中的常量系数。
            Value *coeffVal;                        ///< 运行期可变系数；常量步长时为 nullptr。
            std::vector<LinearIVExpr> indexExprs;   ///< 各级 GEP 索引的线性表达式。
            bool useLinearizedGEP;                  ///< 是否先将多维索引线性化再构造递推。
        };
        std::vector<GEPCandidate> candidates;

        // 确定序遍历：候选顺序决定 preheader 中 initGEP 的产出顺序
        for (auto bb : loop.blocksOrdered) {
            for (auto inst : bb->instr_list_) {
                if (!inst->is_gep()) continue;
                auto *gep = static_cast<GetElementPtrInst *>(inst);

                // Stack-derived pointer recurrences currently perturb address
                // register allocation across surrounding loops.  Keep their
                // explicit GEPs until the backend can model stack bases as
                // stable recurrence roots without changing unrelated streams.
                {
                    Value *root = gep->get_operand(0);
                    while (auto *g2 = dynamic_cast<GetElementPtrInst *>(root))
                        root = g2->get_operand(0);
                    if (dynamic_cast<AllocaInst *>(root)) continue;
                }

                auto initExprCanMaterialize = [&](LinearIVExpr expr) -> bool {
                    if (expr.coeffVal) {
                        // Variable IV coefficient: the IV-start `coeffVal*coeff*init`
                        // is computed at runtime in the preheader (always possible
                        // for an i32 init). Drop the IV part, check the remainder.
                        expr.coeff = 0;
                        expr.coeffVal = nullptr;
                    } else if (expr.coeff != 0) {
                        if (initCI) {
                            long long ivStart = static_cast<long long>(expr.coeff) * initCI->value_;
                            if (!fitsInt(ivStart)) return false;
                            expr.constOffset += ivStart;
                            if (!fitsInt(expr.constOffset)) return false;
                        }
                        // 非常量初值：coeff*init 运行期计算，余下部分照常检查
                        expr.coeff = 0;
                    }
                    return canMaterializeOffsetInPreheader(expr, loop);
                };

                auto *gepPtrTy = dynamic_cast<PointerType *>(gep->type_);
                bool gepYieldsArray =
                    gepPtrTy && gepPtrTy->contained_->tid_ == Type::ArrayTyID;
                SCEVGEPInfo gepInfo = gepYieldsArray ? SCEVGEPInfo{} : SE.getLinearizedGEP(gep);
                if (gepInfo.valid) {
                    LinearIVExpr flatExpr =
                        linearizeSCEVForIV(gepInfo.elementOffset, iv, loop, SE);
                    // Variable-stride (coeffVal) GEPs are handled by the per-index
                    // flat path below, which captures the runtime coefficient.
                    if (flatExpr.valid && flatExpr.coeff != 0 && !flatExpr.coeffVal) {
                        bool ok = true;
                        std::vector<LinearIVExpr> indexExprs;
                        indexExprs.reserve(gep->num_ops() - 1);

                        for (unsigned i = 1; i < gep->num_ops(); i++) {
                            Value *idx = gep->get_operand(i);
                            if (!isI32(idx)) {
                                ok = false;
                                break;
                            }

                            LinearIVExpr expr =
                                linearizeSCEVForIV(SE.getSCEV(idx), iv, loop, SE);
                            if (!expr.valid || !initExprCanMaterialize(expr)) {
                                ok = false;
                                break;
                            }
                            indexExprs.push_back(expr);
                        }

                        if (ok) {
                            debugLinearizedGEP(func, loop.header, gep, gepInfo,
                                               flatExpr.coeff);
                            candidates.push_back(
                                {gep, 0, flatExpr.coeff, nullptr, indexExprs, true});
                            continue;
                        }
                    }
                }

                unsigned ivOpIdx = 0;
                int ivDependentIndexes = 0;
                int coeff = 0;
                Value *coeffVal = nullptr;
                std::vector<LinearIVExpr> indexExprs;
                indexExprs.reserve(gep->num_ops() - 1);

                for (unsigned i = 1; i < gep->num_ops(); i++) {
                    Value *idx = gep->get_operand(i);
                    if (!isI32(idx)) {
                        ivDependentIndexes = 2;
                        break;
                    }

                    LinearIVExpr expr = linearizeSCEVForIV(SE.getSCEV(idx), iv, loop, SE);
                    if (!expr.valid) {
                        ivDependentIndexes = 2;
                        break;
                    }

                    if (expr.coeff != 0 || expr.coeffVal) {
                        ivDependentIndexes++;
                        ivOpIdx = i;
                        coeff = expr.coeff;
                        coeffVal = expr.coeffVal;
                    }
                    indexExprs.push_back(expr);
                }

                if (ivDependentIndexes != 1) continue;

                bool ok = true;
                for (auto expr : indexExprs) {
                    if (!initExprCanMaterialize(expr)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
                candidates.push_back({gep, ivOpIdx, coeff, coeffVal, indexExprs, false});
            }
        }

        // 多条控制流路径可能为同一 IV 计算出相同仿射地址。它们必须共享一套
        // 指针递推，否则独立 PHI 会遮蔽等价性并保留重复更新。比较时覆盖全部
        // 线性化下标项，只合并每次迭代都指向同一地址的 GEP。
        auto sameRecurrence = [](const GEPCandidate &a,
                                 const GEPCandidate &b) {
            if (a.gep->type_ != b.gep->type_ ||
                a.gep->get_operand(0) != b.gep->get_operand(0) ||
                a.ivOpIdx != b.ivOpIdx || a.coeff != b.coeff ||
                a.coeffVal != b.coeffVal ||
                a.useLinearizedGEP != b.useLinearizedGEP ||
                a.indexExprs.size() != b.indexExprs.size())
                return false;
            for (size_t i = 0; i < a.indexExprs.size(); ++i) {
                const auto &lhs = a.indexExprs[i];
                const auto &rhs = b.indexExprs[i];
                if (lhs.coeff != rhs.coeff ||
                    lhs.constOffset != rhs.constOffset ||
                    lhs.offset != rhs.offset || lhs.coeffVal != rhs.coeffVal)
                    return false;
            }
            return true;
        };

        // 对每个候选 GEP 执行强度削弱替换
        for (auto &c : candidates) {
            auto *gep = c.gep;
            if (!gep->parent_) continue;

            int elemStride = 1;
            if (!c.useLinearizedGEP) {
                elemStride = computeElementStride(gep, c.ivOpIdx);
                Type *addrTy = static_cast<PointerType*>(gep->type_)->contained_;
                while (auto *arrTy = dynamic_cast<ArrayType*>(addrTy)) {
                    elemStride /= arrTy->num_elements_;
                    addrTy = arrTy->contained_;
                }
                if (elemStride <= 0) elemStride = 1;
            }

            int ivStrideVal = 1;
            if (auto *ci = dynamic_cast<ConstantInt*>(iv.stride))
                ivStrideVal = ci->value_;
            // 先计算每轮元素步长的常量部分；若含运行时系数 coeffVal，
            // 真正步长在 preheader 中物化为 effectiveStride*coeffVal。
            long long effectiveStride64 = static_cast<long long>(ivStrideVal) *
                                          elemStride * c.coeff;
            if (!iv.isAdd) effectiveStride64 = -effectiveStride64;
            if (effectiveStride64 == 0 || !fitsInt(effectiveStride64)) continue;
            int effectiveStride = static_cast<int>(effectiveStride64);
            // 大常量步长在最内层可省掉完整仿射地址重建；在外层则会让指针跨整个
            // 嵌套循环存活、增加全局寄存器压力，所以外层仍只接受小步长。
            if (!c.coeffVal && std::abs(effectiveStride) > 16 &&
                !loop.children.empty())
                continue;

            if (std::getenv("DEBUG_IVSR"))
                std::cerr << "[IVSR] func=" << func->name_
                          << " header=" << loop.header->name_
                          << " gep=%" << gep->name_
                          << " stride=" << effectiveStride
                          << (c.coeffVal ? "*<var>" : "") << "\n";

            std::vector<Value *> initIndices;
            bool failed = false;
            for (unsigned i = 1; i < gep->num_ops(); i++) {
                LinearIVExpr expr = c.indexExprs[i - 1];
                Value *ivStartVal = nullptr; // 非常量初值时的运行期 coeff*init
                if (expr.coeffVal) {
                    // IV-start = coeffVal * coeff * init, evaluated at runtime.
                    // For a zero init it contributes nothing.
                    auto *initZero = dynamic_cast<ConstantInt *>(iv.initVal);
                    if (!initZero || initZero->value_ != 0) {
                        builder->set_insert_point(preheader);
                        Value *m = expr.coeffVal;
                        if (expr.coeff != 1) {
                            auto *mc = builder->create_imul(
                                expr.coeffVal,
                                new ConstantInt(module->int32_ty_, expr.coeff));
                            preheader->remove_instr(mc);
                            preheader->add_instruction_before_terminator(mc);
                            m = mc;
                        }
                        auto *mul = builder->create_imul(m, iv.initVal);
                        preheader->remove_instr(mul);
                        preheader->add_instruction_before_terminator(mul);
                        ivStartVal = mul;
                    }
                    expr.coeff = 0;
                    expr.coeffVal = nullptr;
                } else if (expr.coeff != 0) {
                    if (initCI) {
                        long long ivStart = static_cast<long long>(expr.coeff) * initCI->value_;
                        if (!fitsInt(ivStart)) {
                            failed = true;
                            break;
                        }
                        expr.constOffset += ivStart;
                        if (!fitsInt(expr.constOffset)) {
                            failed = true;
                            break;
                        }
                    } else if (expr.coeff == 1) {
                        ivStartVal = iv.initVal;
                    } else {
                        builder->set_insert_point(preheader);
                        auto *mul = builder->create_imul(
                            iv.initVal,
                            new ConstantInt(module->int32_ty_, expr.coeff));
                        preheader->remove_instr(mul);
                        preheader->add_instruction_before_terminator(mul);
                        ivStartVal = mul;
                    }
                    expr.coeff = 0;
                }

                Value *idxVal = materializeOffsetInPreheader(expr, preheader, loop, builder, module);
                if (!idxVal) {
                    failed = true;
                    break;
                }
                if (ivStartVal) {
                    auto *baseCI = dynamic_cast<ConstantInt *>(idxVal);
                    if (baseCI && baseCI->value_ == 0) {
                        idxVal = ivStartVal;
                    } else {
                        builder->set_insert_point(preheader);
                        auto *add = builder->create_iadd(idxVal, ivStartVal);
                        preheader->remove_instr(add);
                        preheader->add_instruction_before_terminator(add);
                        idxVal = add;
                    }
                }
                initIndices.push_back(idxVal);
            }
            if (failed) continue;
            // 在 preheader 中创建初始 GEP（先构造后移入，保证位置在 br 之前）
            builder->set_insert_point(preheader);
            auto *initGEP = builder->create_gep(gep->get_operand(0), initIndices);
            preheader->remove_instr(initGEP);
            preheader->add_instruction_before_terminator(initGEP);

            // 创建 phi 并插入循环头前端
            auto *addrPhi = PhiInst::create_phi(gep->type_, loop.header);
            loop.header->remove_instr(addrPhi);
            loop.header->add_instruction_front(addrPhi);

            addrPhi->addIncoming(initGEP, preheader);

            // 步进量：常量步长直接用立即数；变量步长 = effectiveStride * coeffVal，
            // 在 preheader 中一次性算好（循环不变），latch 内仅做指针步进。
            Value *stepIdx = nullptr;
            if (c.coeffVal) {
                stepIdx = c.coeffVal;
                if (effectiveStride != 1) {
                    builder->set_insert_point(preheader);
                    auto *mc = builder->create_imul(
                        c.coeffVal,
                        new ConstantInt(module->int32_ty_, effectiveStride));
                    preheader->remove_instr(mc);
                    preheader->add_instruction_before_terminator(mc);
                    stepIdx = mc;
                }
            } else {
                stepIdx = new ConstantInt(module->int32_ty_, effectiveStride);
            }

            // 在 latch 中创建步进指令（在 br 之前）
            builder->set_insert_point(iv.latch);
            auto *incrGEP = builder->create_gep(addrPhi, {stepIdx});
            iv.latch->remove_instr(incrGEP);
            iv.latch->add_instruction_before_terminator(incrGEP);

            addrPhi->addIncoming(incrGEP, iv.latch);

            // 多 latch 场景（如 continue）：其他 latch 的前驱用 phi 自身
            for (auto pred : loop.header->pre_bbs_) {
                if (pred == preheader || pred == iv.latch) continue;
                if (loop.blocks.count(pred)) addrPhi->addIncoming(addrPhi, pred);
            }

            gep->replace_all_use_with(addrPhi);
            gep->parent_->delete_instr(gep);

            for (auto &other : candidates) {
                if (&other == &c || !other.gep->parent_ ||
                    !sameRecurrence(c, other))
                    continue;
                other.gep->replace_all_use_with(addrPhi);
                other.gep->parent_->delete_instr(other.gep);
            }
        }

    }

    delete builder;
}
