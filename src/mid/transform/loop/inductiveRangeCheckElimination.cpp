/**
 * @file inductiveRangeCheckElimination.cpp
 * @brief 归纳边界检查消除：利用归纳变量范围证明删除循环内恒真或恒假的边界检查分支。
 * @details 从入口保护、单调归纳变量和仿射检查推导有效迭代域，只在整个域上成立时收紧边界或删除检查。
 */

#include "../../../include/mid/opt/inductiveRangeCheckElimination.hpp"
#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include <unordered_set>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

/**
 * @brief 描述由循环头 PHI、回边更新和退出比较组成的规范归纳变量。
 */
struct CanonicalIV {
    PhiInst *phi = nullptr;                             ///< 循环头中的归纳变量 PHI。
    Value *init = nullptr;                              ///< 来自循环预头的初始值。
    Instruction *next = nullptr;                        ///< 回边上生成下一归纳值的指令。
    int step = 0;                                       ///< 每次迭代的常量步长。
    ICmpInst *latchCmp = nullptr;                        ///< 控制回边或退出的比较指令。
    ICmpInst::ICmpOp exitPred = ICmpInst::ICMP_SLT;     ///< 归一到继续迭代方向的比较谓词。
    Value *bound = nullptr;                             ///< 与归纳变量比较的循环边界。
};

/**
 * @brief 描述边界检查分支中执行工作路径与跳过路径的对应关系。
 */
struct BranchShape {
    BasicBlock *work = nullptr;     ///< 检查通过后执行实际工作的基本块。
    BasicBlock *skip = nullptr;     ///< 跳过工作的基本块；头块直达回边时为 nullptr。
    bool workOnTrue = false;        ///< 条件为 true 时是否进入工作路径。
};

/**
 * @brief 表示由循环不变量基值与常量偏移构成的简单仿射地址。
 */
struct AffineExpr {
    Value *base = nullptr;      ///< 不随当前循环变化的基础值。
    int64_t offset = 0;         ///< 相对基础值的有符号常量偏移。
};

/**
 * @brief 表示 `ivCoeff * IV + sum(coeff * value) + constant` 的整数线性式。
 */
struct LinearExpr {
    int ivCoeff = 0;                                  ///< 目标归纳变量的系数。
    std::vector<std::pair<Value *, int>> terms;        ///< 其他循环不变量及其整数系数。
    int64_t constant = 0;                             ///< 线性式中的常数项。
};

/**
 * @brief 描述在循环头先检查条件、回边再更新的旋转循环归纳变量与 CFG。
 */
struct RotatedIV {
    PhiInst *phi = nullptr;             ///< 循环头中的归纳变量 PHI。
    Value *init = nullptr;              ///< 预头提供的初始值。
    Instruction *next = nullptr;        ///< 回边生成的下一迭代值。
    BasicBlock *preheader = nullptr;     ///< 唯一循环预头。
    BasicBlock *header = nullptr;        ///< 执行范围检查的循环头。
    BasicBlock *latch = nullptr;         ///< 更新归纳变量并形成回边的基本块。
    BasicBlock *bodyEntry = nullptr;     ///< 范围检查通过后的循环体入口。
    BasicBlock *exit = nullptr;          ///< 范围检查失败时到达的循环出口。
    ICmpInst *headerCmp = nullptr;        ///< 循环头控制分支所用的比较指令。
    Value *bound = nullptr;              ///< 循环头比较使用的迭代上界。
};

/**
 * @brief 保存由入口保护条件推导出的归纳变量线性下界或上界。
 */
struct GuardBound {
    /** @brief 保护条件提供的边界种类。 */
    enum Kind {
        LowerInclusive,    ///< 闭区间下界：归纳变量不小于该表达式。
        UpperExclusive     ///< 开区间上界：归纳变量小于该表达式。
    } kind;                ///< 当前保护条件的边界种类。
    LinearExpr expr;       ///< 保护条件蕴含的线性边界表达式。
};

/**
 * @brief 描述入口保护分支及其高概率安全方向。
 */
struct GuardBranch {
    BranchInst *branch = nullptr;    ///< 承载入口保护条件的条件分支。
    ICmpInst *cmp = nullptr;         ///< 分支使用的整数比较。
    bool hotOnTrue = false;          ///< true 边是否通向受保护的正常执行路径。
};

/**
 * @brief 判断 isLoopInvariant 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isLoopInvariant(Value *value, const Loop &loop) {
    if (dynamic_cast<Constant *>(value) || dynamic_cast<GlobalVariable *>(value) ||
        dynamic_cast<Argument *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && inst->parent_ && !loop.blocks.count(inst->parent_);
}

/**
 * @brief 收集或查找 getConstInt 所需的信息。
 * @param value 待检查、映射或物化的 IR 值。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool getConstInt(Value *value, int &out) {
    auto *ci = dynamic_cast<ConstantInt *>(value);
    if (!ci)
        return false;
    out = ci->value_;
    return true;
}

/**
 * @brief 实现 phiIncomingIndex 对应的局部分析或变换辅助逻辑。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param pred 前驱基本块。
 * @return 返回计算、分析或构造得到的结果。
 */
int phiIncomingIndex(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return static_cast<int>(i);
    }
    return -1;
}

/**
 * @brief 实现 setPhiIncomingValue 对应的局部分析或变换辅助逻辑。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param pred 前驱基本块。
 * @param val 待检查或映射的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool setPhiIncomingValue(PhiInst *phi, BasicBlock *pred, Value *val) {
    int idx = phiIncomingIndex(phi, pred);
    if (idx < 0)
        return false;
    phi->set_operand(static_cast<unsigned>(idx), val);
    return true;
}

/**
 * @brief 查询 PHI 在指定前驱边上的入值。
 * @param phi 待查询的 PHI 指令。
 * @param pred 指定的前驱基本块。
 * @return 找到时返回对应入值，否则返回 nullptr。
 */
Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

/**
 * @brief 判断 isOnlyIVUpdateAndLatchCmp 所描述的结构、合法性或安全条件是否成立。
 * @param latch 循环回边基本块。
 * @param ivNext 参数 `ivNext`，用于本函数的分析、匹配或 IR 构造。
 * @param latchCmp 参数 `latchCmp`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isOnlyIVUpdateAndLatchCmp(BasicBlock *latch, Instruction *ivNext,
                               ICmpInst *latchCmp) {
    std::vector<Instruction *> body;
    for (auto *inst : latch->instr_list_) {
        if (inst->isTerminator())
            break;
        body.push_back(inst);
    }
    return body.size() == 2 && body[0] == ivNext && body[1] == latchCmp;
}

/**
 * @brief 判断 isPureSkipBlock 所描述的结构、合法性或安全条件是否成立。
 * @param block 目标或待检查的基本块。
 * @param latch 循环回边基本块。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isPureSkipBlock(BasicBlock *block, BasicBlock *latch) {
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    if (!term || term->num_ops() != 1 || term->get_operand(0) != latch)
        return false;

    for (auto *inst : block->instr_list_) {
        if (inst == term)
            break;
        if (inst->is_store() || inst->is_call() || inst->is_load() ||
            inst->is_alloca())
            return false;
        for (auto &use : inst->use_list_) {
            auto *user = use.user_;
            if (user && user->parent_ != block)
                return false;
        }
    }
    return true;
}

/**
 * @brief 判断 isWorkBlock 所描述的结构、合法性或安全条件是否成立。
 * @param block 目标或待检查的基本块。
 * @param latch 循环回边基本块。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isWorkBlock(BasicBlock *block, BasicBlock *latch, const Loop &loop) {
    if (!block || block == latch || !loop.isInLoop(block))
        return false;
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    return term && term->num_ops() == 1 && term->get_operand(0) == latch;
}

/**
 * @brief 判断 isUnconditionalTo 所描述的结构、合法性或安全条件是否成立。
 * @param block 目标或待检查的基本块。
 * @param target 参数 `target`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isUnconditionalTo(BasicBlock *block, BasicBlock *target) {
    auto *term = dynamic_cast<BranchInst *>(block->get_terminator());
    return term && term->num_ops() == 1 && term->get_operand(0) == target;
}

/**
 * @brief 判断 isSkipSuccessor 所描述的结构、合法性或安全条件是否成立。
 * @param block 目标或待检查的基本块。
 * @param latch 循环回边基本块。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isSkipSuccessor(BasicBlock *block, BasicBlock *latch, const Loop &loop) {
    if (block == latch)
        return true;
    return block && loop.isInLoop(block) && isPureSkipBlock(block, latch);
}

/**
 * @brief 实现 negateCmp 对应的局部分析或变换辅助逻辑。
 * @param pred 前驱基本块。
 * @return 返回计算、分析或构造得到的结果。
 */
ICmpInst::ICmpOp negateCmp(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:
        return ICmpInst::ICMP_NE;
    case ICmpInst::ICMP_NE:
        return ICmpInst::ICMP_EQ;
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGE;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGT;
    default:
        return pred;
    }
}

/**
 * @brief 实现 swapCmp 对应的局部分析或变换辅助逻辑。
 * @param pred 前驱基本块。
 * @return 返回计算、分析或构造得到的结果。
 */
ICmpInst::ICmpOp swapCmp(ICmpInst::ICmpOp pred) {
    switch (pred) {
    case ICmpInst::ICMP_EQ:
    case ICmpInst::ICMP_NE:
        return pred;
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGE;
    default:
        return pred;
    }
}

/**
 * @brief 实现 addLinearTerm 对应的局部分析或变换辅助逻辑。
 * @param expr 参数 `expr`，用于本函数的分析、匹配或 IR 构造。
 * @param value 待检查、映射或物化的 IR 值。
 * @param coeff 参数 `coeff`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void addLinearTerm(LinearExpr &expr, Value *value, int coeff) {
    if (coeff == 0)
        return;
    for (auto it = expr.terms.begin(); it != expr.terms.end(); ++it) {
        if (it->first == value) {
            it->second += coeff;
            if (it->second == 0)
                expr.terms.erase(it);
            return;
        }
    }
    expr.terms.push_back({value, coeff});
}

/**
 * @brief 实现 parseLinearExpr 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param iv 参数 `iv`，用于本函数的分析、匹配或 IR 构造。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @param depth 递归分析深度，用于限制搜索开销。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool parseLinearExpr(Value *value, const Loop &loop, Value *iv, LinearExpr &out,
                     int depth = 0) {
    if (!value || depth > 8 || value->type_->tid_ != Type::IntegerTyID)
        return false;

    int c = 0;
    if (getConstInt(value, c)) {
        out.constant += c;
        return true;
    }
    if (value == iv) {
        out.ivCoeff += 1;
        return out.ivCoeff <= 1;
    }
    if (isLoopInvariant(value, loop)) {
        addLinearTerm(out, value, 1);
        return true;
    }

    auto *bin = dynamic_cast<BinaryInst *>(value);
    if (!bin || (!bin->is_add() && !bin->is_sub()))
        return false;

    LinearExpr lhs;
    if (!parseLinearExpr(bin->get_operand(0), loop, iv, lhs, depth + 1))
        return false;
    LinearExpr rhs;
    if (!parseLinearExpr(bin->get_operand(1), loop, iv, rhs, depth + 1))
        return false;

    out.ivCoeff += lhs.ivCoeff;
    out.constant += lhs.constant;
    for (auto &term : lhs.terms)
        addLinearTerm(out, term.first, term.second);

    const int sign = bin->is_add() ? 1 : -1;
    out.ivCoeff += sign * rhs.ivCoeff;
    out.constant += sign * rhs.constant;
    for (auto &term : rhs.terms)
        addLinearTerm(out, term.first, sign * term.second);

    return out.ivCoeff >= -1 && out.ivCoeff <= 1;
}

/**
 * @brief 实现 subLinearExpr 对应的局部分析或变换辅助逻辑。
 * @param lhs 表达式左操作数。
 * @param rhs 表达式右操作数。
 * @return 返回计算、分析或构造得到的结果。
 */
LinearExpr subLinearExpr(const LinearExpr &lhs, const LinearExpr &rhs) {
    LinearExpr out = lhs;
    out.ivCoeff -= rhs.ivCoeff;
    out.constant -= rhs.constant;
    for (auto &term : rhs.terms)
        addLinearTerm(out, term.first, -term.second);
    return out;
}

/**
 * @brief 实现 addLinearConstant 对应的局部分析或变换辅助逻辑。
 * @param expr 参数 `expr`，用于本函数的分析、匹配或 IR 构造。
 * @param delta 参数 `delta`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
LinearExpr addLinearConstant(LinearExpr expr, int64_t delta) {
    expr.constant += delta;
    return expr;
}

/**
 * @brief 在指定基本块中物化线性表达式的常量项和各 SSA 项。
 * @param expr 待生成的线性表达式。
 * @param ty 生成结果的整数类型。
 * @param bb 指令插入基本块。
 * @param module 所属模块，用于构造常量。
 * @return 物化后的 IR 值。
 */
Value *materializeLinearExpr(const LinearExpr &expr, Type *ty, BasicBlock *bb,
                             Instruction *insertBefore) {
    if (expr.ivCoeff != 0)
        return nullptr;

    Value *current = nullptr;
    if (expr.terms.empty()) {
        current = new ConstantInt(ty, static_cast<int>(expr.constant));
        return current;
    }

    for (auto &term : expr.terms) {
        Value *termVal = term.first;
        if (term.second == -1) {
            auto *zero = new ConstantInt(ty, 0);
            auto *neg = new BinaryInst(ty, Instruction::Sub, zero, termVal, bb, true);
            if (!bb->add_instruction_before_inst(neg, insertBefore))
                return nullptr;
            termVal = neg;
        } else if (term.second != 1) {
            return nullptr;
        }

        if (!current) {
            current = termVal;
        } else {
            auto *sum = new BinaryInst(ty, Instruction::Add, current, termVal, bb, true);
            if (!bb->add_instruction_before_inst(sum, insertBefore))
                return nullptr;
            current = sum;
        }
    }

    if (expr.constant != 0) {
        auto *c = new ConstantInt(ty, static_cast<int>(expr.constant > 0 ? expr.constant
                                                                         : -expr.constant));
        auto op = expr.constant > 0 ? Instruction::Add : Instruction::Sub;
        auto *adj = new BinaryInst(ty, op, current, c, bb, true);
        if (!bb->add_instruction_before_inst(adj, insertBefore))
            return nullptr;
        current = adj;
    }
    return current;
}

/**
 * @brief 计算 decomposeInvariantAffine 所描述的派生信息，供合法性或收益判断使用。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool decomposeInvariantAffine(Value *value, const Loop &loop, AffineExpr &out) {
    if (!value || value->type_->tid_ != Type::IntegerTyID ||
        !isLoopInvariant(value, loop))
        return false;

    int c = 0;
    if (getConstInt(value, c)) {
        out.base = nullptr;
        out.offset = c;
        return true;
    }

    auto *bin = dynamic_cast<BinaryInst *>(value);
    if (bin && (bin->is_add() || bin->is_sub())) {
        int rhsConst = 0;
        if (getConstInt(bin->get_operand(1), rhsConst)) {
            AffineExpr inner;
            if (!decomposeInvariantAffine(bin->get_operand(0), loop, inner))
                return false;
            out = inner;
            out.offset += bin->is_add() ? rhsConst : -rhsConst;
            return true;
        }
        int lhsConst = 0;
        if (bin->is_add() && getConstInt(bin->get_operand(0), lhsConst)) {
            AffineExpr inner;
            if (!decomposeInvariantAffine(bin->get_operand(1), loop, inner))
                return false;
            out = inner;
            out.offset += lhsConst;
            return true;
        }
    }

    out.base = value;
    out.offset = 0;
    return true;
}

/**
 * @brief 物化循环不变仿射表达式，并附加一个常量偏移量。
 * @param expr 待生成的仿射表达式。
 * @param extraDelta 额外加入的常量偏移。
 * @param ty 结果整数类型。
 * @param block 指令插入基本块。
 * @param module 所属模块。
 * @return 生成的仿射 IR 值。
 */
Value *materializeAffine(const AffineExpr &expr, int64_t extraDelta, Type *ty,
                         BasicBlock *bb, Instruction *insertBefore) {
    const int64_t total = expr.offset + extraDelta;
    if (!expr.base)
        return new ConstantInt(ty, static_cast<int>(total));
    if (total == 0)
        return expr.base;

    auto *delta = new ConstantInt(ty, static_cast<int>(total > 0 ? total : -total));
    auto op = total > 0 ? Instruction::Add : Instruction::Sub;
    auto *adj = new BinaryInst(ty, op, expr.base, delta, bb, true);
    if (!bb->add_instruction_before_inst(adj, insertBefore))
        return nullptr;
    return adj;
}

/**
 * @brief 匹配 CanonicalIV 所描述的 IR 结构并提取结果。
 * @param header 循环头基本块。
 * @param preheader 循环预头基本块。
 * @param latch 循环回边基本块。
 * @param loop 待检查或变换的循环。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchCanonicalIV(BasicBlock *header, BasicBlock *preheader, BasicBlock *latch,
                      const Loop &loop, CanonicalIV &out) {
    auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
    if (!latchTerm || latchTerm->num_ops() != 3)
        return false;

    auto *latchTrue = dynamic_cast<BasicBlock *>(latchTerm->get_operand(1));
    auto *latchFalse = dynamic_cast<BasicBlock *>(latchTerm->get_operand(2));
    if (latchTrue != header || !latchFalse || loop.isInLoop(latchFalse))
        return false;

    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID || phi->num_ops() != 4)
            continue;

        Value *fromPre = incomingFrom(phi, preheader);
        Value *fromLatch = incomingFrom(phi, latch);
        auto *update = dynamic_cast<BinaryInst *>(fromLatch);
        if (!fromPre || !update || update->type_ != phi->type_)
            continue;
        if (!isLoopInvariant(fromPre, loop))
            continue;

        int step = 0;
        int c = 0;
        if (update->is_add()) {
            if (update->get_operand(0) == phi && getConstInt(update->get_operand(1), c))
                step = c;
            else if (update->get_operand(1) == phi &&
                     getConstInt(update->get_operand(0), c))
                step = c;
        } else if (update->is_sub() && update->get_operand(0) == phi &&
                   getConstInt(update->get_operand(1), c)) {
            step = -c;
        }
        if (step != 1 && step != -1)
            continue;

        auto *latchCmp = dynamic_cast<ICmpInst *>(latchTerm->get_operand(0));
        if (!latchCmp)
            continue;

        ICmpInst::ICmpOp pred = latchCmp->icmp_op_;
        Value *bound = nullptr;
        if (latchCmp->get_operand(0) == update) {
            bound = latchCmp->get_operand(1);
        } else if (latchCmp->get_operand(1) == update) {
            pred = swapCmp(pred);
            bound = latchCmp->get_operand(0);
        } else {
            continue;
        }

        if (!bound || bound->type_ != phi->type_ || !isLoopInvariant(bound, loop))
            continue;
        if (step == 1 && pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE)
            continue;
        if (step == -1 && pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
            continue;

        out.phi = phi;
        out.init = fromPre;
        out.next = update;
        out.step = step;
        out.latchCmp = latchCmp;
        out.exitPred = pred;
        out.bound = bound;
        return true;
    }
    return false;
}

/**
 * @brief 匹配 BranchShape 所描述的 IR 结构并提取结果。
 * @param header 循环头基本块。
 * @param latch 循环回边基本块。
 * @param loop 待检查或变换的循环。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchBranchShape(BasicBlock *header, BasicBlock *latch, const Loop &loop,
                      BranchShape &out) {
    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!headerTerm || headerTerm->num_ops() != 3)
        return false;

    auto *trueSucc = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *falseSucc = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!trueSucc || !falseSucc || trueSucc == falseSucc)
        return false;

    const bool trueIsSkip = isSkipSuccessor(trueSucc, latch, loop);
    const bool falseIsSkip = isSkipSuccessor(falseSucc, latch, loop);
    const bool trueIsWork = isWorkBlock(trueSucc, latch, loop);
    const bool falseIsWork = isWorkBlock(falseSucc, latch, loop);

    if (trueIsSkip && falseIsWork && !falseIsSkip) {
        out.work = falseSucc;
        out.skip = trueSucc == latch ? nullptr : trueSucc;
        out.workOnTrue = false;
        return true;
    }
    if (falseIsSkip && trueIsWork && !trueIsSkip) {
        out.work = trueSucc;
        out.skip = falseSucc == latch ? nullptr : falseSucc;
        out.workOnTrue = true;
        return true;
    }
    return false;
}

/**
 * @brief 根据循环内范围保护条件计算规范 IV 的收紧边界。
 * @param iv 规范归纳变量描述。
 * @param guardCmp 待移出循环的保护比较。
 * @param workOnTrue 比较为真时是否进入有效工作路径。
 * @param loop 当前循环，用于判断表达式不变性。
 * @param module 所属模块。
 * @param insertionBlock 收紧边界的插入基本块。
 * @param insertBefore 新指令必须插入到该指令之前。
 * @param DT 支配树，用于验证边界操作数在插入点可用。
 * @return 能够推导时返回新边界，否则返回 nullptr。
 */
Value *buildTightenedBound(const CanonicalIV &iv, ICmpInst *guardCmp,
                           bool workOnTrue, const Loop &loop, Module *module,
                           BasicBlock *insertionBlock, Instruction *insertBefore,
                           const DominatorTreeAnalysis &DT) {
    ICmpInst::ICmpOp pred =
        workOnTrue ? guardCmp->icmp_op_ : negateCmp(guardCmp->icmp_op_);

    Value *limitExpr = nullptr;
    if (guardCmp->get_operand(0) == iv.phi) {
        limitExpr = guardCmp->get_operand(1);
    } else if (guardCmp->get_operand(1) == iv.phi) {
        pred = swapCmp(pred);
        limitExpr = guardCmp->get_operand(0);
    } else {
        return nullptr;
    }

    auto dominatesInsertion = [&](Value *value) {
        auto *inst = dynamic_cast<Instruction *>(value);
        if (!inst) return true;
        if (!inst->parent_) return false;
        return inst->parent_ == insertionBlock ||
               DT.dominates(inst->parent_, insertionBlock);
    };
    // 专用 preheader 位于零次迭代保护之后。“循环不变量”只说明值定义在循环外，
    // 并不保证它支配更早的保护块，因此移动检查前必须显式验证支配关系。
    if (!dominatesInsertion(limitExpr) || !dominatesInsertion(iv.bound) ||
        !dominatesInsertion(iv.init))
        return nullptr;

    AffineExpr limit;
    if (!decomposeInvariantAffine(limitExpr, loop, limit))
        return nullptr;

    int64_t delta = 0;
    if (iv.step == 1) {
        if (pred != ICmpInst::ICMP_SLT && pred != ICmpInst::ICMP_SLE)
            return nullptr;
        if (iv.exitPred == ICmpInst::ICMP_SLT)
            delta = pred == ICmpInst::ICMP_SLE ? 1 : 0;
        else
            delta = pred == ICmpInst::ICMP_SLT ? -1 : 0;
    } else {
        if (pred != ICmpInst::ICMP_SGT && pred != ICmpInst::ICMP_SGE)
            return nullptr;
        if (iv.exitPred == ICmpInst::ICMP_SGT)
            delta = pred == ICmpInst::ICMP_SGE ? -1 : 0;
        else
            delta = pred == ICmpInst::ICMP_SGT ? 1 : 0;
    }

    Value *candidate =
        materializeAffine(limit, delta, iv.phi->type_, insertionBlock, insertBefore);
    if (!candidate || candidate == iv.bound)
        return nullptr;

    ICmpInst::ICmpOp choosePred =
        iv.step == 1 ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_SGT;
    auto *chooseCmp =
        new ICmpInst(choosePred, candidate, iv.bound, insertionBlock, true);
    if (!insertionBlock->add_instruction_before_inst(chooseCmp, insertBefore))
        return nullptr;

    Value *tightened = nullptr;
    if (iv.step == 1)
        tightened = new SelectInst(chooseCmp, candidate, iv.bound, iv.phi->type_);
    else
        tightened = new SelectInst(chooseCmp, candidate, iv.bound, iv.phi->type_);
    if (!insertionBlock->add_instruction_before_inst(
            static_cast<Instruction *>(tightened), insertBefore))
        return nullptr;
    return tightened;
}

/**
 * @brief 匹配 RotatedIV 所描述的 IR 结构并提取结果。
 * @param loop 待检查或变换的循环。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchRotatedIV(Loop &loop, RotatedIV &out) {
    BasicBlock *header = loop.header;
    BasicBlock *preheader = loop.preheader;
    if (!header || !preheader)
        return false;

    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!headerTerm || headerTerm->num_ops() != 3)
        return false;

    auto *headerCmp = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    if (!headerCmp)
        return false;

    auto *bodyEntry = dynamic_cast<BasicBlock *>(headerTerm->get_operand(1));
    auto *loopExit = dynamic_cast<BasicBlock *>(headerTerm->get_operand(2));
    if (!bodyEntry || !loopExit || !loop.isInLoop(bodyEntry) || loop.isInLoop(loopExit))
        return false;

    for (auto *inst : header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi->type_->tid_ != Type::IntegerTyID || phi->num_ops() != 4)
            continue;

        Value *fromPre = incomingFrom(phi, preheader);
        if (!fromPre || !isLoopInvariant(fromPre, loop))
            continue;

        BasicBlock *latch = nullptr;
        Value *fromLatch = nullptr;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (pred && pred != preheader && loop.isInLoop(pred)) {
                latch = pred;
                fromLatch = phi->get_operand(i);
                break;
            }
        }
        if (!latch || !fromLatch)
            continue;

        auto *latchTerm = dynamic_cast<BranchInst *>(latch->get_terminator());
        if (!latchTerm || latchTerm->num_ops() != 1 || latchTerm->get_operand(0) != header)
            continue;

        auto *update = dynamic_cast<BinaryInst *>(fromLatch);
        if (!update || !update->is_add() || update->type_ != phi->type_)
            continue;
        int step = 0;
        if (update->get_operand(0) == phi && getConstInt(update->get_operand(1), step)) {
        } else if (update->get_operand(1) == phi &&
                   getConstInt(update->get_operand(0), step)) {
        } else {
            continue;
        }
        if (step != 1)
            continue;

        ICmpInst::ICmpOp pred = headerCmp->icmp_op_;
        Value *bound = nullptr;
        if (headerCmp->get_operand(0) == phi) {
            bound = headerCmp->get_operand(1);
        } else if (headerCmp->get_operand(1) == phi) {
            pred = swapCmp(pred);
            bound = headerCmp->get_operand(0);
        } else {
            continue;
        }
        if (pred != ICmpInst::ICMP_SLT || !bound || !isLoopInvariant(bound, loop))
            continue;

        out.phi = phi;
        out.init = fromPre;
        out.next = update;
        out.preheader = preheader;
        out.header = header;
        out.latch = latch;
        out.bodyEntry = bodyEntry;
        out.exit = loopExit;
        out.headerCmp = headerCmp;
        out.bound = bound;
        return true;
    }
    return false;
}

/**
 * @brief 匹配 GuardChain 所描述的 IR 结构并提取结果。
 * @param entry 参数 `entry`，用于本函数的分析、匹配或 IR 构造。
 * @param latch 循环回边基本块。
 * @param loop 待检查或变换的循环。
 * @param guards 参数 `guards`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchGuardChain(BasicBlock *entry, BasicBlock *latch, const Loop &loop,
                     std::vector<GuardBranch> &guards) {
    guards.clear();
    BasicBlock *cursor = entry;
    for (int depth = 0; depth < 8; ++depth) {
        auto *term = dynamic_cast<BranchInst *>(cursor->get_terminator());
        if (!term)
            return false;
        if (term->num_ops() == 1)
            return term->get_operand(0) == latch;
        if (term->num_ops() != 3)
            return false;

        auto *cmp = dynamic_cast<ICmpInst *>(term->get_operand(0));
        auto *trueSucc = dynamic_cast<BasicBlock *>(term->get_operand(1));
        auto *falseSucc = dynamic_cast<BasicBlock *>(term->get_operand(2));
        if (!cmp || !trueSucc || !falseSucc || trueSucc == falseSucc)
            return false;

        if (trueSucc == latch && falseSucc != latch) {
            guards.push_back({term, cmp, false});
            cursor = falseSucc;
            continue;
        }
        if (falseSucc == latch && trueSucc != latch) {
            guards.push_back({term, cmp, true});
            cursor = trueSucc;
            continue;
        }
        return false;
    }
    return false;
}

/**
 * @brief 判断 isHeaderPhi 所描述的结构、合法性或安全条件是否成立。
 * @param shape 参数 `shape`，用于本函数的分析、匹配或 IR 构造。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isHeaderPhi(const RotatedIV &shape, Value *value) {
    auto *phi = dynamic_cast<PhiInst *>(value);
    return phi && phi->parent_ == shape.header;
}

/**
 * @brief 判断 isTrivialRotatedLatch 所描述的结构、合法性或安全条件是否成立。
 * @param shape 参数 `shape`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isTrivialRotatedLatch(const RotatedIV &shape) {
    std::unordered_set<Instruction *> incomingUpdates;
    for (auto *inst : shape.header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        auto *incoming = dynamic_cast<Instruction *>(incomingFrom(phi, shape.latch));
        if (incoming && incoming->parent_ == shape.latch)
            incomingUpdates.insert(incoming);
    }

    for (auto *inst : shape.latch->instr_list_) {
        if (inst->is_phi() || inst == shape.latch->get_terminator())
            continue;
        if (!incomingUpdates.count(inst))
            return false;

        if (auto *bin = dynamic_cast<BinaryInst *>(inst)) {
            int step = 0;
            bool ok = false;
            if (bin->is_add()) {
                ok = (isHeaderPhi(shape, bin->get_operand(0)) &&
                      getConstInt(bin->get_operand(1), step)) ||
                     (isHeaderPhi(shape, bin->get_operand(1)) &&
                      getConstInt(bin->get_operand(0), step));
            } else if (bin->is_sub()) {
                ok = isHeaderPhi(shape, bin->get_operand(0)) &&
                     getConstInt(bin->get_operand(1), step);
            }
            if (!ok || step == 0)
                return false;
            continue;
        }

        if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            int step = 0;
            if (gep->num_ops() != 2 || !isHeaderPhi(shape, gep->get_operand(0)) ||
                !getConstInt(gep->get_operand(1), step) || step == 0)
                return false;
            continue;
        }

        return false;
    }
    return true;
}

/**
 * @brief 实现 deriveGuardBound 对应的局部分析或变换辅助逻辑。
 * @param guard 参数 `guard`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param iv 参数 `iv`，用于本函数的分析、匹配或 IR 构造。
 * @param out 参数 `out`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool deriveGuardBound(const GuardBranch &guard, const Loop &loop, PhiInst *iv,
                      GuardBound &out) {
    ICmpInst::ICmpOp pred =
        guard.hotOnTrue ? guard.cmp->icmp_op_ : negateCmp(guard.cmp->icmp_op_);

    LinearExpr lhs;
    if (!parseLinearExpr(guard.cmp->get_operand(0), loop, iv, lhs))
        return false;
    LinearExpr rhs;
    if (!parseLinearExpr(guard.cmp->get_operand(1), loop, iv, rhs))
        return false;

    if (rhs.ivCoeff != 0 && lhs.ivCoeff == 0) {
        std::swap(lhs, rhs);
        pred = swapCmp(pred);
    }
    if (lhs.ivCoeff != 1 || rhs.ivCoeff != 0)
        return false;

    LinearExpr rest = lhs;
    rest.ivCoeff = 0;
    LinearExpr bound = subLinearExpr(rhs, rest);

    switch (pred) {
    case ICmpInst::ICMP_SGE:
        out.kind = GuardBound::LowerInclusive;
        out.expr = bound;
        return true;
    case ICmpInst::ICMP_SGT:
        out.kind = GuardBound::LowerInclusive;
        out.expr = addLinearConstant(bound, 1);
        return true;
    case ICmpInst::ICMP_SLT:
        out.kind = GuardBound::UpperExclusive;
        out.expr = bound;
        return true;
    case ICmpInst::ICMP_SLE:
        out.kind = GuardBound::UpperExclusive;
        out.expr = addLinearConstant(bound, 1);
        return true;
    default:
        return false;
    }
}

/**
 * @brief 使用 min 或 max 组合当前边界与新的保护边界。
 * @param current 已累计的边界；为空时直接采用 candidate。
 * @param candidate 新推导出的候选边界。
 * @param kind 指定取下界最大值还是上界最小值。
 * @param ty 合并结果的整数类型。
 * @param bb 新 compare/select 指令的插入基本块。
 * @param insertBefore 新指令必须插入到该指令之前。
 * @return 合并后的边界值。
 */
Value *combineBound(Value *current, Value *candidate, GuardBound::Kind kind,
                    Type *ty, BasicBlock *bb, Instruction *insertBefore) {
    if (!current)
        return candidate;
    ICmpInst::ICmpOp cmpPred =
        kind == GuardBound::LowerInclusive ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_SLT;
    auto *cmp = new ICmpInst(cmpPred, candidate, current, bb, true);
    if (!bb->add_instruction_before_inst(cmp, insertBefore))
        return nullptr;
    auto *sel = new SelectInst(cmp, candidate, current, ty);
    if (!bb->add_instruction_before_inst(sel, insertBefore))
        return nullptr;
    return sel;
}

/**
 * @brief 构造收紧后初值相对原初值的差值。
 * @param newInit 收紧后的循环初值。
 * @param origInit 原循环初值。
 * @param ty 差值的整数类型。
 * @param bb 指令插入基本块。
 * @param insertBefore 新指令必须插入到该指令之前。
 * @return 表示 newInit-origInit 的 IR 值。
 */
Value *buildDelta(Value *newInit, Value *origInit, Type *ty, BasicBlock *bb,
                  Instruction *insertBefore) {
    if (newInit == origInit)
        return nullptr;
    int c = 0;
    if (getConstInt(origInit, c) && c == 0)
        return newInit;
    auto *delta = new BinaryInst(ty, Instruction::Sub, newInit, origInit, bb, true);
    if (!bb->add_instruction_before_inst(delta, insertBefore))
        return nullptr;
    return delta;
}

/**
 * @brief 物化初值移动量乘以伴随递推步长后的补偿值。
 * @param delta 控制 IV 的初值移动量。
 * @param step 伴随 PHI 每轮的常量步长。
 * @param ty 结果整数类型。
 * @param bb 指令插入基本块。
 * @param insertBefore 新指令必须插入到该指令之前。
 * @return 缩放后的补偿值。
 */
Value *materializeScaledDelta(Value *delta, int64_t step, Type *ty, BasicBlock *bb,
                              Instruction *insertBefore) {
    if (!delta || step == 0)
        return new ConstantInt(ty, 0);
    if (step == 1)
        return delta;
    auto *scale = new ConstantInt(ty, static_cast<int>(step));
    auto *mul = new BinaryInst(ty, Instruction::Mul, delta, scale, bb, true);
    if (!bb->add_instruction_before_inst(mul, insertBefore))
        return nullptr;
    return mul;
}

/**
 * @brief 实现 rebaseCompanionPhis 对应的局部分析或变换辅助逻辑。
 * @param loopShape 参数 `loopShape`，用于本函数的分析、匹配或 IR 构造。
 * @param delta 参数 `delta`，用于本函数的分析、匹配或 IR 构造。
 * @param insertBefore 参数 `insertBefore`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool rebaseCompanionPhis(const RotatedIV &loopShape, Value *delta,
                         Instruction *insertBefore) {
    if (!delta)
        return true;

    for (auto *inst : loopShape.header->instr_list_) {
        if (!inst->is_phi())
            break;
        auto *phi = static_cast<PhiInst *>(inst);
        if (phi == loopShape.phi)
            continue;

        Value *fromPre = incomingFrom(phi, loopShape.preheader);
        Value *fromLatch = incomingFrom(phi, loopShape.latch);
        if (!fromPre || !fromLatch)
            continue;

        Value *rebased = nullptr;
        if (auto *bin = dynamic_cast<BinaryInst *>(fromLatch)) {
            int step = 0;
            if (bin->is_add()) {
                if (bin->get_operand(0) == phi && getConstInt(bin->get_operand(1), step)) {
                } else if (bin->get_operand(1) == phi &&
                           getConstInt(bin->get_operand(0), step)) {
                } else {
                    step = 0;
                }
            } else if (bin->is_sub() && bin->get_operand(0) == phi &&
                       getConstInt(bin->get_operand(1), step)) {
                step = -step;
            }
            if (step != 0) {
                Value *scaled = materializeScaledDelta(
                    delta, step > 0 ? step : -step, phi->type_, loopShape.preheader,
                    insertBefore);
                if (!scaled)
                    return false;
                if (step > 0)
                    rebased = new BinaryInst(phi->type_, Instruction::Add, fromPre, scaled,
                                             loopShape.preheader, true);
                else
                    rebased = new BinaryInst(phi->type_, Instruction::Sub, fromPre, scaled,
                                             loopShape.preheader, true);
                if (!loopShape.preheader->add_instruction_before_inst(
                        static_cast<Instruction *>(rebased), insertBefore))
                    return false;
            }
        } else if (auto *gep = dynamic_cast<GetElementPtrInst *>(fromLatch)) {
            if (gep->get_operand(0) != phi || gep->num_ops() != 2)
                continue;
            int step = 0;
            if (!getConstInt(gep->get_operand(1), step) || step <= 0)
                continue;
            Value *scaled = materializeScaledDelta(
                delta, step, loopShape.phi->type_, loopShape.preheader, insertBefore);
            if (!scaled)
                return false;
            std::vector<Value *> idxs{scaled};
            auto *newGep = new GetElementPtrInst(fromPre, idxs, loopShape.preheader, true);
            if (!loopShape.preheader->add_instruction_before_inst(newGep, insertBefore))
                return false;
            rebased = newGep;
        }

        if (rebased && !setPhiIncomingValue(phi, loopShape.preheader, rebased))
            return false;
    }
    return true;
}

/**
 * @brief 尝试执行 TightenMonotoneGuardLoop 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool tryTightenMonotoneGuardLoop(Loop &loop, Module *module) {
    RotatedIV shape;
    if (!matchRotatedIV(loop, shape))
        return false;
    if (!isTrivialRotatedLatch(shape))
        return false;

    std::vector<GuardBranch> guards;
    if (!matchGuardChain(shape.bodyEntry, shape.latch, loop, guards) || guards.empty())
        return false;

    std::vector<GuardBound> bounds;
    for (auto &guard : guards) {
        GuardBound bound;
        if (!deriveGuardBound(guard, loop, shape.phi, bound))
            return false;
        bounds.push_back(bound);
    }

    Instruction *insertBefore = shape.preheader->get_terminator();
    Value *newInit = shape.init;
    Value *newBound = shape.bound;
    bool changed = false;

    for (auto &bound : bounds) {
        Value *candidate =
            materializeLinearExpr(bound.expr, shape.phi->type_, shape.preheader, insertBefore);
        if (!candidate)
            return false;
        Value *combined =
            combineBound(bound.kind == GuardBound::LowerInclusive ? newInit : newBound,
                         candidate, bound.kind, shape.phi->type_, shape.preheader,
                         insertBefore);
        if (!combined)
            return false;
        if (bound.kind == GuardBound::LowerInclusive) {
            if (combined != newInit) {
                newInit = combined;
                changed = true;
            }
        } else {
            if (combined != newBound) {
                newBound = combined;
                changed = true;
            }
        }
    }

    if (!changed || (newInit == shape.init && newBound == shape.bound))
        return false;

    Value *delta =
        buildDelta(newInit, shape.init, shape.phi->type_, shape.preheader, insertBefore);
    if (newInit != shape.init) {
        if (!setPhiIncomingValue(shape.phi, shape.preheader, newInit))
            return false;
        if (!rebaseCompanionPhis(shape, delta, insertBefore))
            return false;
    }

    if (shape.headerCmp->get_operand(0) == shape.phi)
        shape.headerCmp->set_operand(1, newBound);
    else
        shape.headerCmp->set_operand(0, newBound);

    for (auto &guard : guards) {
        guard.branch->set_operand(
            0, new ConstantInt(module->int1_ty_, guard.hotOnTrue ? 1 : 0));
    }
    return true;
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void inductiveRangeCheckElimination::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses inductiveRangeCheckElimination::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AM);
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool inductiveRangeCheckElimination::runOnFunction(Function *func,
                                                    AnalysisManager &AM) {
    if (func->basic_blocks_.empty())
        return false;

    LoopInfo &LI = AM.getLoopInfo(func);
    DominatorTreeAnalysis &DT = AM.getDominatorTree(func);

    std::vector<Loop *> loops;
    for (auto &loop : LI.allLoops())
        loops.push_back(loop.get());
    std::sort(loops.begin(), loops.end(),
              [](Loop *a, Loop *b) { return a->depth > b->depth; });

    bool changed = false;
    for (auto *loop : loops)
        changed |= tryTightenLoop(*loop, func->parent_, LI, DT);

    if (changed)
        func->set_instr_name();
    return changed;
}

/**
 * @brief 尝试执行 TightenLoop 匹配或变换；前置条件不满足时不提交改写。
 * @param loop 待检查或变换的循环。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param LI 参数 `LI`，用于本函数的分析、匹配或 IR 构造。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool inductiveRangeCheckElimination::tryTightenLoop(
    Loop &loop, Module *module, const LoopInfo &LI,
    const DominatorTreeAnalysis &DT) {
    // 先尝试单调保护链的专用快速路径，再匹配旋转循环的入口保护和循环内检查。
    // 只有入口条件、归纳步长与仿射边界共同覆盖完整迭代域时才收紧循环范围。
    const bool debug = std::getenv("DEBUG_INDUCTIVE_RANGE") != nullptr;
    auto reject = [&](const char *reason) {
        if (debug)
            std::cerr << "[InductiveRange] reject header="
                      << (loop.header ? loop.header->name_ : "<null>")
                      << " reason=" << reason << "\n";
        return false;
    };
    if (debug)
        std::cerr << "[InductiveRange] inspect header="
                  << (loop.header ? loop.header->name_ : "<null>")
                  << " blocks=" << loop.blocks.size() << "\n";
    BasicBlock *preheader = loop.preheader;
    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    if (!preheader || !header || !latch)
        return reject("missing-loop-structure");

    if (tryTightenMonotoneGuardLoop(loop, module)) {
        if (debug)
            std::cerr << "[InductiveRange] tightened-monotone header="
                      << header->name_ << "\n";
        return true;
    }

    auto *preTerm = dynamic_cast<BranchInst *>(preheader->get_terminator());
    auto *headerTerm = dynamic_cast<BranchInst *>(header->get_terminator());
    if (!preTerm || !headerTerm || headerTerm->num_ops() != 3)
        return reject("non-conditional-entry-or-header");

    // LoopRotate 会保留专用 preheader，并把零次迭代保护留在其唯一环外前驱。
    // 同时接受直接保护和旋转后保护，使本分析不依赖 Pass 调度先后。
    BasicBlock *guardBlock = preheader;
    BranchInst *guardTerm = preTerm;
    BasicBlock *guardContinue = header;
    if (preTerm->num_ops() == 1 && preTerm->get_operand(0) == header) {
        if (preheader->pre_bbs_.size() != 1)
            return reject("entry-guard-predecessor");
        guardBlock = preheader->pre_bbs_[0];
        if (loop.isInLoop(guardBlock))
            return reject("entry-guard-inside-loop");
        guardTerm = dynamic_cast<BranchInst *>(guardBlock->get_terminator());
        guardContinue = preheader;
    }
    if (!guardTerm || guardTerm->num_ops() != 3)
        return reject("entry-guard-terminator");

    auto *guardTrue = dynamic_cast<BasicBlock *>(guardTerm->get_operand(1));
    auto *guardFalse = dynamic_cast<BasicBlock *>(guardTerm->get_operand(2));
    if (guardTrue != guardContinue || !guardFalse || loop.isInLoop(guardFalse))
        return reject("entry-guard-shape");

    CanonicalIV iv;
    if (!matchCanonicalIV(header, preheader, latch, loop, iv))
        return reject("canonical-iv");
    if (!isOnlyIVUpdateAndLatchCmp(latch, iv.next, iv.latchCmp))
        return reject("nontrivial-latch");

    BranchShape shape;
    if (!matchBranchShape(header, latch, loop, shape))
        return reject("branch-shape");

    auto *guardCmp = dynamic_cast<ICmpInst *>(headerTerm->get_operand(0));
    if (!guardCmp || guardCmp->parent_ != header)
        return reject("header-guard");

    Instruction *insertBefore = guardBlock->get_terminator();
    Value *tightened = buildTightenedBound(iv, guardCmp, shape.workOnTrue, loop,
                                           module, guardBlock, insertBefore, DT);
    if (!tightened)
        return reject("bound-construction");

    auto *entryCmp =
        new ICmpInst(iv.exitPred, iv.init, tightened, guardBlock, true);
    if (!guardBlock->add_instruction_before_inst(entryCmp, insertBefore))
        return reject("entry-compare-insertion");

    guardTerm->set_operand(0, entryCmp);
    if (iv.latchCmp->get_operand(0) == iv.next)
        iv.latchCmp->set_operand(1, tightened);
    else
        iv.latchCmp->set_operand(0, tightened);
    // 收紧后的迭代域恰好等于原 work 分支成立的范围，故可把 header 条件
    // 固定到 work 边。这样 CFGSimplify 会删除 skip 路径，也避免重复调度
    // 不断在同一边界外包裹等价的 min/max select。
    headerTerm->set_operand(
        0, new ConstantInt(module->int1_ty_, shape.workOnTrue ? 1 : 0));
    if (debug)
        std::cerr << "[InductiveRange] tightened-branch header="
                  << header->name_ << "\n";
    return true;
}
