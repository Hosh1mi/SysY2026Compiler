/**
 * @file loopResetPointElimination.cpp
 * @brief 循环重置点消除：识别循环携带内存状态中的确定重置点，删除对最终结果无影响的动态前缀。
 * @details 用结构和别名事实证明重置迭代必达且覆盖旧状态，再把循环入口收紧到最后有效重置点。
 */

// Detect proved multiplicative reset points in loop-carried memory state and
// eliminate the dynamically dead prefix without depending on source layout.

#include "../../../include/mid/opt/loopResetPointElimination.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <unordered_set>
#include <vector>

namespace {

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_RESET_POINT_ELIMINATION") != nullptr;
}

/**
 * @brief 生成 debugLog 对应的调试诊断，不参与程序语义。
 * @param message 参数 `message`，用于本函数的分析、匹配或 IR 构造。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void debugLog(const std::string &message) {
    if (debugEnabled())
        std::cerr << "[LoopResetPointElimination] " << message << "\n";
}

/**
 * @brief 查询 PHI 来自指定前驱边的入值。
 * @param phi 待查询的 PHI 指令。
 * @param predecessor 指定前驱基本块。
 * @return 找到时返回对应值，否则返回 nullptr。
 */
Value *incomingFrom(PhiInst *phi, BasicBlock *predecessor) {
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

/**
 * @brief 原地执行 removePhiIncoming 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param predecessor 前驱基本块。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool removePhiIncoming(PhiInst *phi, BasicBlock *predecessor) {
    for (int i = static_cast<int>(phi->num_ops()) - 1; i >= 1; i -= 2) {
        if (phi->get_operand(static_cast<unsigned>(i)) == predecessor) {
            phi->remove_operands(i - 1, i);
            return true;
        }
    }
    return false;
}

/**
 * @brief 判断 isConstantInt 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param expected 参数 `expected`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isConstantInt(Value *value, int expected) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    return constant && constant->value_ == expected;
}

/**
 * @brief 匹配 AddOne 所描述的 IR 结构并提取结果。
 * @param value 待检查、映射或物化的 IR 值。
 * @param base 参数 `base`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchAddOne(Value *value, Value *base) {
    auto *add = dynamic_cast<BinaryInst *>(value);
    if (!add || !add->is_add())
        return false;
    return (add->get_operand(0) == base &&
            isConstantInt(add->get_operand(1), 1)) ||
           (add->get_operand(1) == base &&
            isConstantInt(add->get_operand(0), 1));
}

/**
 * @brief 判断 isIntegerScalar 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isIntegerScalar(Value *value) {
    return value && value->type_ && value->type_->tid_ == Type::IntegerTyID &&
           static_cast<IntegerType *>(value->type_)->num_bits_ == 32;
}

/**
 * @brief 实现 valueDependsOnImpl 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param needle 参数 `needle`，用于本函数的分析、匹配或 IR 构造。
 * @param visited 递归遍历使用的已访问集合，用于避免环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool valueDependsOnImpl(Value *value, Value *needle,
                        std::unordered_set<Value *> &visited) {
    if (value == needle)
        return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !visited.insert(value).second)
        return false;
    for (unsigned i = 0; i < instruction->num_ops(); ++i) {
        Value *operand = instruction->get_operand(i);
        if (dynamic_cast<BasicBlock *>(operand) ||
            dynamic_cast<Function *>(operand))
            continue;
        if (valueDependsOnImpl(operand, needle, visited))
            return true;
    }
    return false;
}

/**
 * @brief 实现 valueDependsOn 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param needle 参数 `needle`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool valueDependsOn(Value *value, Value *needle) {
    std::unordered_set<Value *> visited;
    return valueDependsOnImpl(value, needle, visited);
}

/**
 * @brief 收集或查找 collectLoadsImpl 所需的信息。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loads 参数 `loads`，用于本函数的分析、匹配或 IR 构造。
 * @param visited 递归遍历使用的已访问集合，用于避免环。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void collectLoadsImpl(Value *value, std::set<LoadInst *> &loads,
                      std::unordered_set<Value *> &visited) {
    if (!value || !visited.insert(value).second)
        return;
    if (auto *load = dynamic_cast<LoadInst *>(value)) {
        loads.insert(load);
        collectLoadsImpl(load->get_operand(0), loads, visited);
        return;
    }
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return;
    for (unsigned i = 0; i < instruction->num_ops(); ++i) {
        Value *operand = instruction->get_operand(i);
        if (!dynamic_cast<BasicBlock *>(operand) &&
            !dynamic_cast<Function *>(operand))
            collectLoadsImpl(operand, loads, visited);
    }
}

/**
 * @brief 收集或查找 collectLoads 所需的信息。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 成功时返回对应对象指针；无法匹配或构造时可能返回 nullptr。
 */
std::set<LoadInst *> collectLoads(Value *value) {
    std::set<LoadInst *> loads;
    std::unordered_set<Value *> visited;
    collectLoadsImpl(value, loads, visited);
    return loads;
}

/**
 * @brief 实现 containsUnexpectedPhiImpl 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param allowed 参数 `allowed`，用于本函数的分析、匹配或 IR 构造。
 * @param visited 递归遍历使用的已访问集合，用于避免环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool containsUnexpectedPhiImpl(Value *value, const Loop &loop,
                               const std::set<Value *> &allowed,
                               std::unordered_set<Value *> &visited) {
    if (!value || allowed.count(value) || !visited.insert(value).second)
        return false;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return false;
    if (dynamic_cast<PhiInst *>(instruction) && loop.isInLoop(instruction))
        return true;
    for (unsigned i = 0; i < instruction->num_ops(); ++i) {
        Value *operand = instruction->get_operand(i);
        if (dynamic_cast<BasicBlock *>(operand) ||
            dynamic_cast<Function *>(operand))
            continue;
        if (containsUnexpectedPhiImpl(operand, loop, allowed, visited))
            return true;
    }
    return false;
}

/**
 * @brief 实现 containsUnexpectedPhi 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param allowed 参数 `allowed`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool containsUnexpectedPhi(Value *value, const Loop &loop,
                           const std::set<Value *> &allowed) {
    std::unordered_set<Value *> visited;
    return containsUnexpectedPhiImpl(value, loop, allowed, visited);
}

/**
 * @brief 判断 hasUnsafeLiveOut 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @param induction 参数 `induction`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasUnsafeLiveOut(const Loop &loop, PhiInst *induction) {
    BasicBlock *latch = loop.singleLatch();
    Value *inductionUpdate = latch ? incomingFrom(induction, latch) : nullptr;
    for (BasicBlock *block : loop.blocksOrdered) {
        for (Instruction *instruction : block->instr_list_) {
            bool safeInductionValue = instruction == induction ||
                                      instruction == inductionUpdate;
            for (const Use &use : instruction->use_list_) {
                auto *user = use.user_;
                if (user && user->parent_ && !loop.isInLoop(user) &&
                    !safeInductionValue)
                    return true;
            }
        }
    }
    return false;
}

/**
 * @brief 穿过 GEP 和 bitcast 查找指针的底层内存对象。
 * @param pointer 待追溯的指针值。
 * @return 最外层不再是 GEP/bitcast 的根对象。
 */
Value *underlyingObject(Value *pointer) {
    return ArgumentAliasAnalysis::underlyingObject(pointer);
}

/**
 * @brief 实现 provenNoAlias 对应的局部分析或变换辅助逻辑。
 * @param first 参数 `first`，用于本函数的分析、匹配或 IR 构造。
 * @param second 参数 `second`，用于本函数的分析、匹配或 IR 构造。
 * @param basicAA 参数 `basicAA`，用于本函数的分析、匹配或 IR 构造。
 * @param argumentAA 参数 `argumentAA`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool provenNoAlias(Value *first, Value *second, BasicAliasAnalysis &basicAA,
                   ArgumentAliasAnalysis &argumentAA) {
    if (basicAA.alias(first, second) == AliasResult::NoAlias)
        return true;
    Value *firstRoot = underlyingObject(first);
    Value *secondRoot = underlyingObject(second);
    return firstRoot && secondRoot &&
           argumentAA.noAlias(firstRoot, secondRoot);
}

/**
 * @brief 匹配 PlusOneIV 所描述的 IR 结构并提取结果。
 * @param loop 待检查或变换的循环。
 * @param phiOut 参数 `phiOut`，用于本函数的分析、匹配或 IR 构造。
 * @param boundOut 参数 `boundOut`，用于本函数的分析、匹配或 IR 构造。
 * @param latchOut 参数 `latchOut`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchPlusOneIV(Loop &loop, PhiInst **phiOut, Value **boundOut,
                    BasicBlock **latchOut) {
    PhiInst *phi = loop.getInductionIV();
    BasicBlock *latch = loop.singleLatch();
    if (!phi || !latch || !loop.preheader || !loop.tripCount ||
        loop.predicate != ICmpInst::ICMP_SLT ||
        loop.controlInduction.guardPosition != InductionGuardPosition::Header)
        return false;
    Value *fromLatch = incomingFrom(phi, latch);
    Value *fromPreheader = incomingFrom(phi, loop.preheader);
    if (!fromLatch || !matchAddOne(fromLatch, phi) ||
        !isConstantInt(fromPreheader, 0))
        return false;
    *phiOut = phi;
    *boundOut = loop.tripCount;
    *latchOut = latch;
    return true;
}

/**
 * @brief 描述一次从旧内存状态递推并写回同一地址的 store 更新。
 */
struct StoreRecurrence {
    StoreInst *store = nullptr;    ///< 将新状态写回递推地址的 store。
    LoadInst *oldLoad = nullptr;   ///< 从同一地址读取旧状态的 load。
    Value *factor = nullptr;       ///< 旧状态在递推表达式中的乘法因子。
    Value *fresh = nullptr;        ///< 与缩放后旧状态合并的新贡献值。
};

/**
 * @brief 匹配 StoreRecurrence 所描述的 IR 结构并提取结果。
 * @param store 参数 `store`，用于本函数的分析、匹配或 IR 构造。
 * @param result 用于写回匹配或计算结果的输出参数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchStoreRecurrence(StoreInst *store, StoreRecurrence &result) {
    Value *stored = store->get_operand(0);
    Value *pointer = store->get_operand(1);
    BinaryInst *multiply = nullptr;
    Value *fresh = nullptr;

    if (auto *add = dynamic_cast<BinaryInst *>(stored); add && add->is_add()) {
        auto *left = dynamic_cast<BinaryInst *>(add->get_operand(0));
        auto *right = dynamic_cast<BinaryInst *>(add->get_operand(1));
        if (left && left->is_mul()) {
            multiply = left;
            fresh = add->get_operand(1);
        } else if (right && right->is_mul()) {
            multiply = right;
            fresh = add->get_operand(0);
        }
    } else if (auto *mul = dynamic_cast<BinaryInst *>(stored);
               mul && mul->is_mul()) {
        multiply = mul;
    }
    if (!multiply)
        return false;

    LoadInst *oldLoad = nullptr;
    Value *factor = nullptr;
    for (unsigned oldIndex = 0; oldIndex != 2; ++oldIndex) {
        auto *candidate =
            dynamic_cast<LoadInst *>(multiply->get_operand(oldIndex));
        if (candidate && candidate->get_operand(0) == pointer) {
            oldLoad = candidate;
            factor = multiply->get_operand(1 - oldIndex);
            break;
        }
    }
    if (!oldLoad || !isIntegerScalar(factor))
        return false;

    result.store = store;
    result.oldLoad = oldLoad;
    result.factor = factor;
    result.fresh = fresh;
    return true;
}

/**
 * @brief 实现 blockEntersLoop 对应的局部分析或变换辅助逻辑。
 * @param block 目标或待检查的基本块。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool blockEntersLoop(BasicBlock *block, const Loop &loop) {
    if (!block)
        return false;
    if (loop.isInLoop(block))
        return true;
    auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
    return branch && branch->num_ops() == 1 &&
           loop.isInLoop(dynamic_cast<BasicBlock *>(branch->get_operand(0)));
}

/**
 * @brief 实现 equivalentIterationValue 对应的局部分析或变换辅助逻辑。
 * @param first 参数 `first`，用于本函数的分析、匹配或 IR 构造。
 * @param second 参数 `second`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool equivalentIterationValue(Value *first, Value *second) {
    if (first == second)
        return true;
    auto *firstLoad = dynamic_cast<LoadInst *>(first);
    auto *secondLoad = dynamic_cast<LoadInst *>(second);
    return firstLoad && secondLoad &&
           firstLoad->get_operand(0) == secondLoad->get_operand(0);
}

/**
 * @brief 计算 evaluateZeroCompare 所描述的派生信息，供合法性或收益判断使用。
 * @param compare 参数 `compare`，用于本函数的分析、匹配或 IR 构造。
 * @param factor 参数 `factor`，用于本函数的分析、匹配或 IR 构造。
 * @param result 用于写回匹配或计算结果的输出参数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool evaluateZeroCompare(ICmpInst *compare, Value *factor, bool &result) {
    Value *other = nullptr;
    bool factorOnLeft = false;
    if (equivalentIterationValue(compare->get_operand(0), factor)) {
        other = compare->get_operand(1);
        factorOnLeft = true;
    } else if (equivalentIterationValue(compare->get_operand(1), factor)) {
        other = compare->get_operand(0);
    } else {
        return false;
    }
    auto *constant = dynamic_cast<ConstantInt *>(other);
    if (!constant)
        return false;
    long long lhs = factorOnLeft ? 0 : constant->value_;
    long long rhs = factorOnLeft ? constant->value_ : 0;
    unsigned long long ulhs = static_cast<unsigned>(lhs);
    unsigned long long urhs = static_cast<unsigned>(rhs);
    switch (compare->icmp_op_) {
    case ICmpInst::ICMP_EQ: result = lhs == rhs; break;
    case ICmpInst::ICMP_NE: result = lhs != rhs; break;
    case ICmpInst::ICMP_SGT: result = lhs > rhs; break;
    case ICmpInst::ICMP_SGE: result = lhs >= rhs; break;
    case ICmpInst::ICMP_SLT: result = lhs < rhs; break;
    case ICmpInst::ICMP_SLE: result = lhs <= rhs; break;
    case ICmpInst::ICMP_UGT: result = ulhs > urhs; break;
    case ICmpInst::ICMP_UGE: result = ulhs >= urhs; break;
    case ICmpInst::ICMP_ULT: result = ulhs < urhs; break;
    case ICmpInst::ICMP_ULE: result = ulhs <= urhs; break;
    }
    return true;
}

/**
 * @brief 实现 resetExecutesStateLoop 对应的局部分析或变换辅助逻辑。
 * @param recurrenceLoop 参数 `recurrenceLoop`，用于本函数的分析、匹配或 IR 构造。
 * @param stateLoop 参数 `stateLoop`，用于本函数的分析、匹配或 IR 构造。
 * @param factor 参数 `factor`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool resetExecutesStateLoop(Loop &recurrenceLoop, Loop &stateLoop,
                            Value *factor) {
    unsigned discriminatingBranches = 0;
    for (BasicBlock *block : recurrenceLoop.blocksOrdered) {
        if (stateLoop.isInLoop(block) || block == recurrenceLoop.header)
            continue;
        auto *branch = dynamic_cast<BranchInst *>(block->get_terminator());
        if (!branch || branch->num_ops() == 1)
            continue;
        if (branch->num_ops() != 3)
            return false;
        auto *trueBlock = dynamic_cast<BasicBlock *>(branch->get_operand(1));
        auto *falseBlock = dynamic_cast<BasicBlock *>(branch->get_operand(2));
        bool trueEnters = blockEntersLoop(trueBlock, stateLoop);
        bool falseEnters = blockEntersLoop(falseBlock, stateLoop);
        if (trueEnters == falseEnters)
            return false;
        auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
        bool compareAtZero = false;
        if (!compare ||
            !evaluateZeroCompare(compare, factor, compareAtZero))
            return false;
        bool entersAtZero = compareAtZero ? trueEnters : falseEnters;
        if (!entersAtZero || ++discriminatingBranches != 1)
            return false;
    }
    return discriminatingBranches <= 1;
}

/**
 * @brief 构造 cloneableAtScan 所描述的新 IR，并返回或记录构造结果。
 * @param value 待检查、映射或物化的 IR 值。
 * @param induction 参数 `induction`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param visiting 参数 `visiting`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool cloneableAtScan(Value *value, Value *induction, const Loop &loop,
                     std::unordered_set<Value *> &visiting) {
    if (value == induction || dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction)
        return false;
    if (!loop.isInLoop(instruction))
        return true;
    if (!visiting.insert(value).second)
        return false;

    bool supported = dynamic_cast<GetElementPtrInst *>(instruction) ||
                     dynamic_cast<BinaryInst *>(instruction) ||
                     dynamic_cast<Bitcast *>(instruction) ||
                     dynamic_cast<ZextInst *>(instruction) ||
                     dynamic_cast<LoadInst *>(instruction);
    if (!supported)
        return false;
    for (unsigned i = 0; i < instruction->num_ops(); ++i)
        if (!cloneableAtScan(instruction->get_operand(i), induction, loop,
                             visiting))
            return false;
    visiting.erase(value);
    return true;
}

/**
 * @brief 汇总可删除重复状态重置点的外层递推循环及其受控状态循环。
 */
struct ResetCandidate {
    Loop *recurrenceLoop = nullptr;    ///< 对状态执行跨迭代递推的外层循环。
    Loop *stateLoop = nullptr;         ///< 初始化或消费状态数组的关联循环。
    PhiInst *induction = nullptr;      ///< 外层递推循环的归纳变量。
    BasicBlock *preheader = nullptr;   ///< 外层递推循环预头。
    BasicBlock *header = nullptr;      ///< 外层递推循环头。
    BasicBlock *latch = nullptr;       ///< 外层递推循环回边块。
    Value *bound = nullptr;            ///< 外层递推循环的迭代上界。
    Value *factor = nullptr;           ///< 每轮作用于旧状态的公共缩放因子。
    bool boundKnownPositive = false;   ///< 是否已证明循环至少执行一次。
};

/**
 * @brief 实现 enclosingLoopProvesPositiveBound 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param bound 参数 `bound`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool enclosingLoopProvesPositiveBound(const Loop &loop, Value *bound) {
    const Loop *parent = loop.parent;
    if (!parent || parent->tripCount != bound || !parent->getInductionIV() ||
        parent->predicate != ICmpInst::ICMP_SLT ||
        parent->controlInduction.guardPosition !=
            InductionGuardPosition::Header)
        return false;
    return isConstantInt(parent->inductionInit, 0);
}

/**
 * @brief 分析 Candidate 的结构、递推或依赖信息。
 * @param recurrenceLoop 参数 `recurrenceLoop`，用于本函数的分析、匹配或 IR 构造。
 * @param loopInfo 参数 `loopInfo`，用于本函数的分析、匹配或 IR 构造。
 * @param DT 参数 `DT`，用于本函数的分析、匹配或 IR 构造。
 * @param basicAA 参数 `basicAA`，用于本函数的分析、匹配或 IR 构造。
 * @param argumentAA 参数 `argumentAA`，用于本函数的分析、匹配或 IR 构造。
 * @param result 用于写回匹配或计算结果的输出参数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool analyzeCandidate(Loop &recurrenceLoop, LoopInfo &loopInfo,
                      const DominatorTreeAnalysis &DT,
                      BasicAliasAnalysis &basicAA,
                      ArgumentAliasAnalysis &argumentAA,
                      ResetCandidate &result) {
    // 候选由外层递推循环、唯一内层状态循环和一组同因子的内存递推组成。
    // 支配、别名和逃逸检查共同证明重置迭代覆盖此前状态，分析阶段不修改 IR。
    if (recurrenceLoop.children.size() != 1)
        return false;
    Loop *stateLoop = recurrenceLoop.children.front();
    if (!stateLoop || !stateLoop->children.empty() ||
        stateLoop->exiting.size() != 1 ||
        stateLoop->exiting.front() != stateLoop->header)
        return false;

    PhiInst *induction = nullptr;
    Value *bound = nullptr;
    BasicBlock *latch = nullptr;
    if (!matchPlusOneIV(recurrenceLoop, &induction, &bound, &latch) ||
        !isIntegerScalar(bound) || valueDependsOn(bound, induction))
        return false;
    PhiInst *stateIV = stateLoop->getInductionIV();
    auto *stateInitInstruction =
        dynamic_cast<Instruction *>(stateLoop->inductionInit);
    auto *stateBoundInstruction =
        dynamic_cast<Instruction *>(stateLoop->tripCount);
    if (!stateIV || !stateLoop->singleLatch() || !stateLoop->tripCount ||
        (stateInitInstruction &&
         recurrenceLoop.isInLoop(stateInitInstruction)) ||
        (stateBoundInstruction &&
         recurrenceLoop.isInLoop(stateBoundInstruction)) ||
        hasUnsafeLiveOut(recurrenceLoop, induction))
        return false;

    // 状态循环中的每个 store 都必须匹配同一类乘法递推；任何位于状态循环外的
    // 写入、调用或返回都会破坏“最后一次重置覆盖此前状态”的证明。
    std::vector<StoreRecurrence> recurrences;
    for (BasicBlock *block : recurrenceLoop.blocksOrdered) {
        for (Instruction *instruction : block->instr_list_) {
            if (instruction->is_call() || instruction->is_ret())
                return false;
            auto *store = dynamic_cast<StoreInst *>(instruction);
            if (!store)
                continue;
            if (!stateLoop->isInLoop(store))
                return false;
            StoreRecurrence recurrence;
            if (!matchStoreRecurrence(store, recurrence) ||
                valueDependsOn(store->get_operand(1), induction) ||
                containsUnexpectedPhi(store->get_operand(1), recurrenceLoop,
                                      {stateIV}) ||
                !DT.dominates(store->parent_, stateLoop->singleLatch()))
                return false;
            recurrences.push_back(recurrence);
        }
    }
    if (recurrences.empty())
        return false;

    Value *factor = recurrences.front().factor;
    if (!valueDependsOn(factor, induction))
        return false;
    for (const StoreRecurrence &recurrence : recurrences)
        if (recurrence.factor != factor)
            return false;

    // 重置因子的所有 load 必须与状态槽互不别名，否则反向扫描因子时可能读到
    // 被递推循环自身改写后的值，扫描结果就不再等价于原执行。
    std::set<LoadInst *> factorLoads = collectLoads(factor);
    if (factorLoads.empty())
        return false;
    for (const StoreRecurrence &recurrence : recurrences) {
        if (recurrence.fresh &&
            containsUnexpectedPhi(recurrence.fresh, recurrenceLoop,
                                  {induction, stateIV}))
            return false;
        for (const StoreRecurrence &stateRecurrence : recurrences) {
            Value *statePointer = stateRecurrence.store->get_operand(1);
            for (LoadInst *load : factorLoads)
                if (!provenNoAlias(load->get_operand(0), statePointer,
                                   basicAA, argumentAA))
                    return false;
            if (recurrence.fresh) {
                for (LoadInst *load : collectLoads(recurrence.fresh))
                    if (!provenNoAlias(load->get_operand(0), statePointer,
                                       basicAA, argumentAA))
                        return false;
            }
        }
    }

    if (!resetExecutesStateLoop(recurrenceLoop, *stateLoop, factor))
        return false;
    std::unordered_set<Value *> visiting;
    if (!cloneableAtScan(factor, induction, recurrenceLoop, visiting))
        return false;

    result.recurrenceLoop = &recurrenceLoop;
    result.stateLoop = stateLoop;
    result.induction = induction;
    result.preheader = recurrenceLoop.preheader;
    result.header = recurrenceLoop.header;
    result.latch = latch;
    result.bound = bound;
    result.factor = factor;
    result.boundKnownPositive =
        enclosingLoopProvesPositiveBound(recurrenceLoop, bound);
    return true;
}

/**
 * @brief 把依赖原归纳变量的纯表达式克隆到反向扫描循环中。
 * @param value 待克隆或复用的原值。
 * @param induction 原递推循环的归纳变量。
 * @param scanIndex 当前反向扫描索引，用于替代 induction。
 * @param loop 原递推循环。
 * @param block 克隆指令的目标基本块。
 * @param clones 原值到克隆值的缓存映射。
 * @return 克隆或重映射后的值；遇到不支持指令时返回 nullptr。
 */
Value *cloneAtScan(Value *value, Value *induction, Value *scanIndex,
                   const Loop &loop, BasicBlock *block,
                   std::map<Value *, Value *> &clones) {
    if (value == induction)
        return scanIndex;
    auto found = clones.find(value);
    if (found != clones.end())
        return found->second;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !loop.isInLoop(instruction))
        return value;

    Value *clone = nullptr;
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(instruction)) {
        Value *base = cloneAtScan(gep->get_operand(0), induction, scanIndex,
                                  loop, block, clones);
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops(); ++i)
            indices.push_back(cloneAtScan(gep->get_operand(i), induction,
                                          scanIndex, loop, block, clones));
        clone = new GetElementPtrInst(base, indices, block);
    } else if (auto *binary = dynamic_cast<BinaryInst *>(instruction)) {
        Value *left = cloneAtScan(binary->get_operand(0), induction,
                                  scanIndex, loop, block, clones);
        Value *right = cloneAtScan(binary->get_operand(1), induction,
                                   scanIndex, loop, block, clones);
        clone = new BinaryInst(binary->type_, binary->op_id_, left, right,
                               block);
    } else if (auto *bitcast = dynamic_cast<Bitcast *>(instruction)) {
        Value *operand = cloneAtScan(bitcast->get_operand(0), induction,
                                     scanIndex, loop, block, clones);
        clone = new Bitcast(Instruction::BitCast, operand, bitcast->type_,
                            block);
    } else if (auto *zext = dynamic_cast<ZextInst *>(instruction)) {
        Value *operand = cloneAtScan(zext->get_operand(0), induction,
                                     scanIndex, loop, block, clones);
        clone = new ZextInst(Instruction::ZExt, operand, zext->type_, block);
    } else if (auto *load = dynamic_cast<LoadInst *>(instruction)) {
        Value *pointer = cloneAtScan(load->get_operand(0), induction,
                                     scanIndex, loop, block, clones);
        clone = new LoadInst(pointer, block);
    }
    if (clone)
        clones[value] = clone;
    return clone;
}

/**
 * @brief 原地执行 applyTighten 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param candidate 参数 `candidate`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param function 待分析或改写的函数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyTighten(const ResetCandidate &candidate, Module *module,
                  Function *function) {
    // 把扫描得到的最后重置位置变为新循环起点，并为“没有重置”保留原入口。
    // 修改入口边后重设 header PHI 的 preheader 入值，保持其余循环携带状态不变。
    BasicBlock *preheader = candidate.preheader;
    BasicBlock *header = candidate.header;
    auto *oldBranch =
        dynamic_cast<BranchInst *>(preheader->get_terminator());
    if (!oldBranch || oldBranch->num_ops() != 1 ||
        oldBranch->get_operand(0) != header)
        return false;

    // 先断开原入口，随后构造完整的反向扫描 CFG；新入口只会在 merge 处接回 header。
    header->remove_pre_basic_block(preheader);
    preheader->remove_succ_basic_block(header);
    preheader->delete_instr(oldBranch);

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    BasicBlock *scanInit = nullptr;
    if (!candidate.boundKnownPositive)
        scanInit = new BasicBlock(module, "resetpoint.scan.init", function);
    auto *scanHeader =
        new BasicBlock(module, "resetpoint.scan.header", function);
    auto *scanBody =
        new BasicBlock(module, "resetpoint.scan.body", function);
    auto *scanContinue =
        new BasicBlock(module, "resetpoint.scan.cont", function);
    auto *scanFound =
        new BasicBlock(module, "resetpoint.scan.found", function);
    auto *scanMiss =
        new BasicBlock(module, "resetpoint.scan.miss", function);
    auto *merge = new BasicBlock(module, "resetpoint.merge", function);

    BasicBlock *scanEntry = preheader;
    if (!candidate.boundKnownPositive) {
        auto *hasIterations = new ICmpInst(ICmpInst::ICMP_SGT,
                                           candidate.bound, zero, preheader);
        new BranchInst(hasIterations, scanInit, merge, preheader);
        scanEntry = scanInit;
    }
    auto *lastIndex = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                     candidate.bound, one, scanEntry);
    new BranchInst(scanHeader, scanEntry);

    // 从 bound-1 向 0 反向寻找最近一次 factor==0；找到即停止，因此该位置之后的
    // 递推结果与从零开始执行相同，可直接作为原循环的新起点。
    auto *scanIndex = PhiInst::create_phi(module->int32_ty_, scanHeader);
    scanHeader->add_instruction_front(scanIndex);
    scanIndex->addIncoming(lastIndex, scanEntry);
    auto *inRange = new ICmpInst(ICmpInst::ICMP_SGE, scanIndex, zero,
                                 scanHeader);
    new BranchInst(inRange, scanBody, scanMiss, scanHeader);

    std::map<Value *, Value *> clones;
    Value *scanFactor = cloneAtScan(candidate.factor, candidate.induction,
                                    scanIndex, *candidate.recurrenceLoop,
                                    scanBody, clones);
    auto *factorZero =
        new ConstantInt(scanFactor ? scanFactor->type_ : module->int32_ty_, 0);
    auto *isReset = new ICmpInst(ICmpInst::ICMP_EQ, scanFactor, factorZero,
                                 scanBody);
    new BranchInst(isReset, scanFound, scanContinue, scanBody);

    auto *previous = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                    scanIndex, one, scanContinue);
    new BranchInst(scanHeader, scanContinue);
    scanIndex->addIncoming(previous, scanContinue);

    new BranchInst(merge, scanFound);
    new BranchInst(merge, scanMiss);

    // 有重置时从最后重置点开始；未找到或原循环为空时仍从 0 开始。
    auto *start = PhiInst::create_phi(module->int32_ty_, merge);
    merge->add_instruction_front(start);
    if (!candidate.boundKnownPositive)
        start->addIncoming(zero, preheader);
    start->addIncoming(scanIndex, scanFound);
    start->addIncoming(zero, scanMiss);
    new BranchInst(header, merge);

    if (!removePhiIncoming(candidate.induction, preheader))
        return false;
    candidate.induction->addIncoming(start, merge);

    debugLog("tightened recurrence loop=" + header->name_ +
             " state loop=" + candidate.stateLoop->header->name_ +
             " in func=" + function->name_);
    return true;
}

/**
 * @brief 实现 runOnFunctionImpl 对应的局部分析或变换辅助逻辑。
 * @param function 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param basicAA 参数 `basicAA`，用于本函数的分析、匹配或 IR 构造。
 * @param argumentAA 参数 `argumentAA`，用于本函数的分析、匹配或 IR 构造。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool runOnFunctionImpl(Function *function, Module *module,
                       BasicAliasAnalysis &basicAA,
                       ArgumentAliasAnalysis &argumentAA,
                       AnalysisManager &AM) {
    // 先在稳定的 LoopInfo 上收集并去重全部候选，再统一提交入口收紧。
    // 不能边遍历边改写，否则第一个候选会使后续保存的 Loop* 失效。
    if (function->basic_blocks_.empty())
        return false;

    LoopInfo &loopInfo = AM.getLoopInfo(function);
    DominatorTreeAnalysis &DT = AM.getDominatorTree(function);
    std::vector<ResetCandidate> candidates;
    std::set<BasicBlock *> seenHeaders;
    for (const auto &loop : loopInfo.allLoops()) {
        ResetCandidate candidate;
        if (analyzeCandidate(*loop, loopInfo, DT, basicAA, argumentAA,
                             candidate) &&
            seenHeaders.insert(candidate.header).second)
            candidates.push_back(candidate);
    }

    bool changed = false;
    for (const ResetCandidate &candidate : candidates)
        changed |= applyTighten(candidate, module, function);
    if (changed)
        function->set_instr_name();
    return changed;
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopResetPointElimination::execute(Module *module) {
    AnalysisManager AM;
    BasicAliasAnalysis &basicAA = AM.getBasicAA(module);
    ArgumentAliasAnalysis argumentAA;
    argumentAA.analyze(module);
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            runOnFunction(function, module, basicAA, argumentAA, AM);
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param manager 参数 `manager`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopResetPointElimination::execute(
    Module *module, AnalysisManager &manager) {
    BasicAliasAnalysis &basicAA = manager.getBasicAA(module);
    ArgumentAliasAnalysis argumentAA;
    argumentAA.analyze(module);
    bool changed = false;
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function, module, basicAA, argumentAA,
                                     manager);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param function 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param basicAA 参数 `basicAA`，用于本函数的分析、匹配或 IR 构造。
 * @param argumentAA 参数 `argumentAA`，用于本函数的分析、匹配或 IR 构造。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopResetPointElimination::runOnFunction(
    Function *function, Module *module, BasicAliasAnalysis &basicAA,
    ArgumentAliasAnalysis &argumentAA, AnalysisManager &AM) {
    return runOnFunctionImpl(function, module, basicAA, argumentAA, AM);
}
