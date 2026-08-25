/**
 * @file indVarSimplify.cpp
 * @brief 归纳变量化简：识别并规范化循环归纳变量，消除可推导的派生递推与冗余循环外使用。
 * @details 先描述 PHI 递推，再证明循环次数和退出值；替换循环外使用时保持 LCSSA 与语义标志不被加强。
 */

#include "../../../include/mid/opt/indVarSimplify.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/scalarEvolution.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/globalVariable.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

namespace {

/**
 * @brief 汇总一个循环归纳变量的递推关系及其循环内外使用点。
 */
struct Recurrence {
    PhiInst *phi = nullptr;                 ///< 循环头中承载当前归纳值的 PHI。
    BinaryInst *update = nullptr;           ///< 回边上计算下一归纳值的加减指令。
    Value *start = nullptr;                 ///< 从预头进入 PHI 的初始值。
    Value *step = nullptr;                  ///< 每次迭代使用的步长值。
    bool subtractStep = false;              ///< 为 true 时递推形式为 `phi - step`。
    std::vector<Use> insideUses;             ///< 位于当前循环内的 PHI 使用点。
    std::vector<Use> outsideUses;            ///< 位于当前循环外的 PHI 使用点。
};

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    return std::getenv("DEBUG_INDVAR_SIMPLIFY") != nullptr;
}

/**
 * @brief 判断 isI32 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isI32(Value *value) {
    auto *type = value ? dynamic_cast<IntegerType *>(value->type_) : nullptr;
    return type && type->num_bits_ == 32;
}

/**
 * @brief 判断 isZero 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isZero(Value *value) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    return constant && constant->value_ == 0;
}

/**
 * @brief 计算 computeConstantTripCount 所描述的派生信息，供合法性或收益判断使用。
 * @param control 参数 `control`，用于本函数的分析、匹配或 IR 构造。
 * @param tripCount 参数 `tripCount`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool computeConstantTripCount(const InductionDescriptor &control,
                              long long &tripCount) {
    auto *start = dynamic_cast<ConstantInt *>(control.start);
    auto *bound = dynamic_cast<ConstantInt *>(control.bound);
    if (!start || !bound || !control.constantStep)
        return false;

    const long long first = start->value_;
    const long long limit = bound->value_;
    const long long step = *control.constantStep;
    __int128 trips = 0;

    switch (control.predicate) {
    case ICmpInst::ICMP_SLT:
        if (step <= 0) return false;
        if (first < limit)
            trips = (static_cast<__int128>(limit) - first + step - 1) / step;
        break;
    case ICmpInst::ICMP_SLE:
        if (step <= 0) return false;
        if (first <= limit)
            trips = (static_cast<__int128>(limit) - first) / step + 1;
        break;
    case ICmpInst::ICMP_SGT: {
        if (step >= 0) return false;
        const long long magnitude = -step;
        if (first > limit)
            trips = (static_cast<__int128>(first) - limit + magnitude - 1) /
                    magnitude;
        break;
    }
    case ICmpInst::ICMP_SGE: {
        if (step >= 0) return false;
        const long long magnitude = -step;
        if (first >= limit)
            trips = (static_cast<__int128>(first) - limit) / magnitude + 1;
        break;
    }
    default:
        return false;
    }

    if (trips < 0 || trips > std::numeric_limits<int>::max())
        return false;

    const __int128 finalValue =
        static_cast<__int128>(first) + trips * step;
    if (finalValue < std::numeric_limits<int>::min() ||
        finalValue > std::numeric_limits<int>::max())
        return false;

    tripCount = static_cast<long long>(trips);
    return true;
}

/**
 * @brief 实现 continuationIsTrue 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param control 参数 `control`，用于本函数的分析、匹配或 IR 构造。
 * @param continuesOnTrue 参数 `continuesOnTrue`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool continuationIsTrue(const Loop &loop, const InductionDescriptor &control,
                        bool &continuesOnTrue) {
    BasicBlock *guard = control.guardPosition == InductionGuardPosition::Header
                            ? loop.header
                            : loop.singleLatch();
    auto *branch = guard
                       ? dynamic_cast<BranchInst *>(guard->get_terminator())
                       : nullptr;
    if (!branch || branch->num_ops() != 3)
        return false;
    auto *trueBlock = dynamic_cast<BasicBlock *>(branch->get_operand(1));
    auto *falseBlock = dynamic_cast<BasicBlock *>(branch->get_operand(2));
    if (!trueBlock || !falseBlock)
        return false;
    const bool trueInside = loop.blocks.count(trueBlock) != 0;
    const bool falseInside = loop.blocks.count(falseBlock) != 0;
    if (trueInside == falseInside)
        return false;
    continuesOnTrue = trueInside;
    return true;
}

/**
 * @brief 判断 canonicalizeConstantControl 所描述的结构、合法性或安全条件是否成立。
 * @param loop 待检查或变换的循环。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool canonicalizeConstantControl(Loop &loop, Module *module) {
    const InductionDescriptor *control = loop.getInductionDescriptor();
    if (!control || !module || !loop.preheader || !loop.singleLatch())
        return false;
    if (loop.canonicalIV)
        return false;

    if (!control->constantStep) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] reject canonical control header="
                      << loop.header->name_ << ": dynamic step\n";
        return false;
    }

    long long tripCount = 0;
    if (!computeConstantTripCount(*control, tripCount)) {
        if (debugEnabled())
            std::cerr << "[IndVarSimplify] reject canonical control header="
                      << loop.header->name_
                      << ": trip count or exit arithmetic is not proven safe\n";
        return false;
    }

    bool continuesOnTrue = false;
    if (!continuationIsTrue(loop, *control, continuesOnTrue))
        return false;

    BasicBlock *header = loop.header;
    BasicBlock *latch = loop.singleLatch();
    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    auto *trip = new ConstantInt(module->int32_ty_, tripCount);
    auto *canonical = PhiInst::create_phi(module->int32_ty_, header);
    canonical->add_phi_pair_operand(zero, loop.preheader);
    header->add_instruction_front(canonical);

    auto *next = new BinaryInst(module->int32_ty_, Instruction::Add,
                                canonical, one, latch, true);
    latch->add_instruction_before_inst(next, control->update);
    canonical->add_phi_pair_operand(next, latch);

    Value *guardValue =
        control->guardPosition == InductionGuardPosition::Header
            ? static_cast<Value *>(canonical)
            : static_cast<Value *>(next);
    control->compare->set_operand(0, guardValue);
    control->compare->set_operand(1, trip);
    control->compare->icmp_op_ = continuesOnTrue ? ICmpInst::ICMP_SLT
                                                  : ICmpInst::ICMP_SGE;

    if (debugEnabled())
        std::cerr << "[IndVarSimplify] canonicalized control header="
                  << header->name_ << " step=" << *control->constantStep
                  << " trips=" << tripCount << "\n";
    return true;
}

/**
 * @brief 判断 isLoopInvariant 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isLoopInvariant(Value *value, const Loop &loop) {
    if (dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return true;
    auto *inst = dynamic_cast<Instruction *>(value);
    return inst && !loop.blocks.count(inst->parent_);
}

/**
 * @brief 判断 isLoopInvariantExpression 所描述的结构、合法性或安全条件是否成立。
 * @param value 待检查、映射或物化的 IR 值。
 * @param loop 待检查或变换的循环。
 * @param visiting 参数 `visiting`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isLoopInvariantExpression(Value *value, const Loop &loop,
                               std::set<Value *> &visiting) {
    if (isLoopInvariant(value, loop))
        return true;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !instruction->parent_ ||
        !loop.blocks.count(instruction->parent_) ||
        instruction->is_phi() || instruction->is_call() ||
        instruction->is_load() || instruction->is_store())
        return false;
    if (!dynamic_cast<BinaryInst *>(instruction) &&
        !dynamic_cast<ICmpInst *>(instruction) &&
        !dynamic_cast<SelectInst *>(instruction))
        return false;
    if (!visiting.insert(value).second)
        return false;
    for (unsigned i = 0; i < instruction->num_ops(); ++i) {
        if (!isLoopInvariantExpression(instruction->get_operand(i), loop,
                                       visiting)) {
            visiting.erase(value);
            return false;
        }
    }
    visiting.erase(value);
    return true;
}

/**
 * @brief 收集或查找 getIncomingValues 所需的信息。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param start 参数 `start`，用于本函数的分析、匹配或 IR 构造。
 * @param latchValue 参数 `latchValue`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool getIncomingValues(PhiInst *phi, const Loop &loop, Value *&start,
                       Value *&latchValue) {
    start = nullptr;
    latchValue = nullptr;
    BasicBlock *latch = loop.singleLatch();
    if (!phi || phi->num_ops() != 4 || !loop.preheader || !latch)
        return false;

    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        auto *source = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (source == loop.preheader)
            start = phi->get_operand(i);
        else if (source == latch)
            latchValue = phi->get_operand(i);
        else
            return false;
    }
    return start && latchValue;
}

/**
 * @brief 匹配 Update 所描述的 IR 结构并提取结果。
 * @param latchValue 参数 `latchValue`，用于本函数的分析、匹配或 IR 构造。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param update 参数 `update`，用于本函数的分析、匹配或 IR 构造。
 * @param step 参数 `step`，用于本函数的分析、匹配或 IR 构造。
 * @param subtractStep 参数 `subtractStep`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchUpdate(Value *latchValue, PhiInst *phi, const Loop &loop,
                 BinaryInst *&update, Value *&step, bool &subtractStep) {
    update = dynamic_cast<BinaryInst *>(latchValue);
    if (!update || update->parent_ != loop.singleLatch() ||
        !isI32(update))
        return false;

    Value *lhs = update->get_operand(0);
    Value *rhs = update->get_operand(1);
    if (update->is_add()) {
        if (lhs == phi)
            step = rhs;
        else if (rhs == phi)
            step = lhs;
        else
            return false;
    } else if (update->is_sub() && lhs == phi) {
        step = rhs;
        subtractStep = true;
    } else {
        return false;
    }
    return isLoopInvariant(step, loop);
}

/**
 * @brief 实现 onlyUsedBy 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param expectedUser 参数 `expectedUser`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool onlyUsedBy(Value *value, Instruction *expectedUser) {
    if (!value || !expectedUser || value->use_list_.empty())
        return false;
    for (const Use &use : value->use_list_) {
        if (use.user_ != expectedUser)
            return false;
    }
    return true;
}

/**
 * @brief 匹配 Recurrence 所描述的 IR 结构并提取结果。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @param recurrence 参数 `recurrence`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool matchRecurrence(PhiInst *phi, const Loop &loop,
                     Recurrence &recurrence) {
    if (!phi || !isI32(phi))
        return false;

    Value *start = nullptr;
    Value *latchValue = nullptr;
    if (!getIncomingValues(phi, loop, start, latchValue) ||
        !isLoopInvariant(start, loop))
        return false;

    BinaryInst *update = nullptr;
    Value *step = nullptr;
    bool subtractStep = false;
    if (!matchUpdate(latchValue, phi, loop, update, step, subtractStep))
        return false;

    std::vector<Use> insideUses;
    std::vector<Use> outsideUses;
    for (const Use &use : phi->use_list_) {
        auto *user = use.user_;
        if (!user || !user->parent_)
            return false;
        if (loop.blocks.count(user->parent_))
            insideUses.push_back(use);
        else
            outsideUses.push_back(use);
    }

    recurrence.phi = phi;
    recurrence.update = update;
    recurrence.start = start;
    recurrence.step = step;
    recurrence.subtractStep = subtractStep;
    recurrence.insideUses = std::move(insideUses);
    recurrence.outsideUses = std::move(outsideUses);
    return true;
}

/**
 * @brief 判断 isDeadExceptForLiveOut 所描述的结构、合法性或安全条件是否成立。
 * @param recurrence 参数 `recurrence`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isDeadExceptForLiveOut(const Recurrence &recurrence) {
    if (!onlyUsedBy(recurrence.update, recurrence.phi))
        return false;
    for (const Use &use : recurrence.insideUses) {
        if (use.user_ != recurrence.update)
            return false;
    }
    return true;
}

/**
 * @brief 实现 wrappedI32 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @return 返回计算、分析或构造得到的结果。
 */
int wrappedI32(std::uint32_t value) {
    return static_cast<std::int32_t>(value);
}

/**
 * @brief 计算常量起点、常量步长递推在循环退出时的 i32 终值。
 * @param recurrence 待求值的归纳递推描述。
 * @param tripCount 已证明的循环迭代次数。
 * @param module 所属模块，用于取得 i32 类型并构造常量。
 * @return 可完全常量折叠时返回退出常量，否则返回 nullptr。
 */
ConstantInt *foldConstantExit(const Recurrence &recurrence,
                              long long tripCount, Module *module) {
    auto *start = dynamic_cast<ConstantInt *>(recurrence.start);
    auto *step = dynamic_cast<ConstantInt *>(recurrence.step);
    if (!start || !step || !module)
        return nullptr;

    std::uint32_t startBits = static_cast<std::uint32_t>(start->value_);
    std::uint32_t stepBits = static_cast<std::uint32_t>(step->value_);
    if (recurrence.subtractStep)
        stepBits = 0U - stepBits;
    std::uint32_t tripBits = static_cast<std::uint32_t>(tripCount);
    std::uint32_t result = startBits + stepBits * tripBits;
    return new ConstantInt(module->int32_ty_, wrappedI32(result));
}

/**
 * @brief 判断 hasFreeExitMaterialization 所描述的结构、合法性或安全条件是否成立。
 * @param recurrence 参数 `recurrence`，用于本函数的分析、匹配或 IR 构造。
 * @param tripCount 参数 `tripCount`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasFreeExitMaterialization(const Recurrence &recurrence,
                                long long tripCount) {
    return tripCount == 0 ||
           (dynamic_cast<ConstantInt *>(recurrence.start) &&
            dynamic_cast<ConstantInt *>(recurrence.step));
}

/**
 * @brief 在循环 preheader 中物化递推执行 tripCount 轮后的退出值。
 * @param recurrence 待物化的归纳递推描述。
 * @param tripCount 已证明的循环迭代次数。
 * @param loop 递推所属循环，用于确定插入位置。
 * @param module 所属模块，用于构造常量和算术指令。
 * @return 成功时返回退出值；缺少合法插入点时返回 nullptr。
 */
Value *materializeExitValue(const Recurrence &recurrence,
                            long long tripCount, const Loop &loop,
                            Module *module) {
    if (!loop.preheader || !module)
        return nullptr;

    if (tripCount == 0)
        return recurrence.start;
    if (auto *folded = foldConstantExit(recurrence, tripCount, module))
        return folded;

    Instruction *terminator = loop.preheader->get_terminator();
    Value *scaledStep = recurrence.step;
    if (tripCount != 1) {
        auto *scale = new ConstantInt(module->int32_ty_,
                                      wrappedI32(
                                          static_cast<std::uint32_t>(
                                              tripCount)));
        auto *multiply = new BinaryInst(module->int32_ty_,
                                        Instruction::Mul,
                                        recurrence.step, scale,
                                        loop.preheader, true);
        loop.preheader->add_instruction_before_inst(multiply, terminator);
        scaledStep = multiply;
    }

    if (!recurrence.subtractStep && isZero(recurrence.start))
        return scaledStep;

    Instruction::OpID operation = recurrence.subtractStep
                                      ? Instruction::Sub
                                      : Instruction::Add;
    auto *result = new BinaryInst(module->int32_ty_, operation,
                                  recurrence.start, scaledStep,
                                  loop.preheader, true);
    loop.preheader->add_instruction_before_inst(result, terminator);
    return result;
}

/**
 * @brief 实现 equivalentInvariant 对应的局部分析或变换辅助逻辑。
 * @param lhs 表达式左操作数。
 * @param rhs 表达式右操作数。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool equivalentInvariant(Value *lhs, Value *rhs,
                         ScalarEvolution &scalarEvolution) {
    if (lhs == rhs)
        return true;
    auto *lhsConstant = dynamic_cast<ConstantInt *>(lhs);
    auto *rhsConstant = dynamic_cast<ConstantInt *>(rhs);
    if (lhsConstant && rhsConstant)
        return lhsConstant->value_ == rhsConstant->value_;
    return scalarEvolution.getSCEV(lhs) == scalarEvolution.getSCEV(rhs);
}

/**
 * @brief 实现 equivalentStep 对应的局部分析或变换辅助逻辑。
 * @param lhs 表达式左操作数。
 * @param rhs 表达式右操作数。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool equivalentStep(const Recurrence &lhs, const Recurrence &rhs,
                    ScalarEvolution &scalarEvolution) {
    auto *lhsConstant = dynamic_cast<ConstantInt *>(lhs.step);
    auto *rhsConstant = dynamic_cast<ConstantInt *>(rhs.step);
    if (lhsConstant && rhsConstant) {
        long long lhsValue = lhsConstant->value_;
        long long rhsValue = rhsConstant->value_;
        if (lhs.subtractStep) lhsValue = -lhsValue;
        if (rhs.subtractStep) rhsValue = -rhsValue;
        return lhsValue == rhsValue;
    }
    return lhs.subtractStep == rhs.subtractStep &&
           equivalentInvariant(lhs.step, rhs.step, scalarEvolution);
}

/**
 * @brief 实现 equivalentRecurrence 对应的局部分析或变换辅助逻辑。
 * @param lhs 表达式左操作数。
 * @param rhs 表达式右操作数。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool equivalentRecurrence(const Recurrence &lhs, const Recurrence &rhs,
                          ScalarEvolution &scalarEvolution) {
    return lhs.phi->type_ == rhs.phi->type_ &&
           equivalentInvariant(lhs.start, rhs.start, scalarEvolution) &&
           equivalentStep(lhs, rhs, scalarEvolution);
}

/**
 * @brief 实现 constantStartDelta 对应的局部分析或变换辅助逻辑。
 * @param leader 参数 `leader`，用于本函数的分析、匹配或 IR 构造。
 * @param derived 参数 `derived`，用于本函数的分析、匹配或 IR 构造。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @param delta 参数 `delta`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool constantStartDelta(const Recurrence &leader,
                        const Recurrence &derived,
                        ScalarEvolution &scalarEvolution,
                        long long &delta) {
    if (!equivalentStep(leader, derived, scalarEvolution)) return false;
    auto *leaderStart = dynamic_cast<ConstantInt *>(leader.start);
    auto *derivedStart = dynamic_cast<ConstantInt *>(derived.start);
    if (!leaderStart || !derivedStart) return false;
    delta = static_cast<long long>(derivedStart->value_) -
            static_cast<long long>(leaderStart->value_);
    return delta >= std::numeric_limits<int>::min() &&
           delta <= std::numeric_limits<int>::max();
}

/**
 * @brief 原地执行 replacementDoesNotStrengthenSemantics 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param leader 参数 `leader`，用于本函数的分析、匹配或 IR 构造。
 * @param redundant 参数 `redundant`，用于本函数的分析、匹配或 IR 构造。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool replacementDoesNotStrengthenSemantics(const Recurrence &leader,
                                           const Recurrence &redundant,
                                           const Loop &loop) {
    if (leader.update->sem_flags_ == redundant.update->sem_flags_ ||
        leader.update->sem_flags_ == 0)
        return true;

    const InductionDescriptor *control = loop.getInductionDescriptor();
    return leader.phi == loop.canonicalIV && control &&
           control->phi == leader.phi && control->isUnitStride() &&
           control->guardPosition == InductionGuardPosition::Header &&
           !control->comparesUpdate &&
           control->predicate == ICmpInst::ICMP_SLT &&
           isZero(control->start);
}

/**
 * @brief 实现 swapPredicate 对应的局部分析或变换辅助逻辑。
 * @param predicate 参数 `predicate`，用于本函数的分析、匹配或 IR 构造。
 * @return 返回计算、分析或构造得到的结果。
 */
ICmpInst::ICmpOp swapPredicate(ICmpInst::ICmpOp predicate) {
    switch (predicate) {
    case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGE;
    case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGE;
    default: return predicate;
    }
}

/**
 * @brief 实现 proveRangeComparison 对应的局部分析或变换辅助逻辑。
 * @param predicate 参数 `predicate`，用于本函数的分析、匹配或 IR 构造。
 * @param minimum 参数 `minimum`，用于本函数的分析、匹配或 IR 构造。
 * @param maximum 参数 `maximum`，用于本函数的分析、匹配或 IR 构造。
 * @param constant 参数 `constant`，用于本函数的分析、匹配或 IR 构造。
 * @param result 用于写回匹配或计算结果的输出参数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool proveRangeComparison(ICmpInst::ICmpOp predicate, long long minimum,
                          long long maximum, long long constant,
                          bool &result) {
    switch (predicate) {
    case ICmpInst::ICMP_EQ:
        if (minimum == maximum && minimum == constant) {
            result = true;
            return true;
        }
        if (constant < minimum || constant > maximum) {
            result = false;
            return true;
        }
        return false;
    case ICmpInst::ICMP_NE:
        if (minimum == maximum && minimum == constant) {
            result = false;
            return true;
        }
        if (constant < minimum || constant > maximum) {
            result = true;
            return true;
        }
        return false;
    case ICmpInst::ICMP_SLT:
        if (maximum < constant) {
            result = true;
            return true;
        }
        if (minimum >= constant) {
            result = false;
            return true;
        }
        return false;
    case ICmpInst::ICMP_SLE:
        if (maximum <= constant) {
            result = true;
            return true;
        }
        if (minimum > constant) {
            result = false;
            return true;
        }
        return false;
    case ICmpInst::ICMP_SGT:
        if (minimum > constant) {
            result = true;
            return true;
        }
        if (maximum <= constant) {
            result = false;
            return true;
        }
        return false;
    case ICmpInst::ICMP_SGE:
        if (minimum >= constant) {
            result = true;
            return true;
        }
        if (maximum < constant) {
            result = false;
            return true;
        }
        return false;
    default:
        return false;
    }
}

/**
 * @brief 收集或查找 getRecurrenceRange 所需的信息。
 * @param recurrence 参数 `recurrence`，用于本函数的分析、匹配或 IR 构造。
 * @param tripCount 参数 `tripCount`，用于本函数的分析、匹配或 IR 构造。
 * @param minimum 参数 `minimum`，用于本函数的分析、匹配或 IR 构造。
 * @param maximum 参数 `maximum`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool getRecurrenceRange(const Recurrence &recurrence, long long tripCount,
                        long long &minimum, long long &maximum) {
    if (tripCount <= 0)
        return false;
    auto *start = dynamic_cast<ConstantInt *>(recurrence.start);
    auto *step = dynamic_cast<ConstantInt *>(recurrence.step);
    if (!start || !step)
        return false;

    long long effectiveStep = step->value_;
    if (recurrence.subtractStep)
        effectiveStep = -effectiveStep;
    const __int128 last =
        static_cast<__int128>(start->value_) +
        static_cast<__int128>(tripCount - 1) *
            static_cast<__int128>(effectiveStep);
    if (last < std::numeric_limits<int>::min() ||
        last > std::numeric_limits<int>::max())
        return false;

    long long lastValue = static_cast<long long>(last);
    minimum = std::min<long long>(start->value_, lastValue);
    maximum = std::max<long long>(start->value_, lastValue);
    return true;
}

/**
 * @brief 实现 simplifyRangeUsers 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool simplifyRangeUsers(Loop &loop, ScalarEvolution &scalarEvolution,
                        Module *module) {
    auto tripCount = scalarEvolution.getConstantTripCount(&loop);
    if (!tripCount || !module)
        return false;

    std::vector<Recurrence> recurrences;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        Recurrence recurrence;
        if (matchRecurrence(static_cast<PhiInst *>(inst), loop, recurrence))
            recurrences.push_back(std::move(recurrence));
    }

    bool changed = false;
    for (const Recurrence &recurrence : recurrences) {
        long long minimum = 0;
        long long maximum = 0;
        if (!getRecurrenceRange(recurrence, *tripCount, minimum, maximum))
            continue;

        std::vector<Use> uses(recurrence.phi->use_list_.begin(),
                              recurrence.phi->use_list_.end());
        std::set<Instruction *> visitedUsers;
        for (const Use &use : uses) {
            auto *user = use.user_;
            if (!user || !user->parent_ ||
                !loop.blocks.count(user->parent_) ||
                user == recurrence.update ||
                user == loop.controlInduction.compare ||
                !visitedUsers.insert(user).second)
                continue;

            Value *replacement = nullptr;
            if (auto *binary = dynamic_cast<BinaryInst *>(user)) {
                if (binary->get_operand(0) != recurrence.phi)
                    continue;
                auto *divisor =
                    dynamic_cast<ConstantInt *>(binary->get_operand(1));
                if (!divisor || divisor->value_ == 0)
                    continue;

                long long magnitude = divisor->value_;
                if (magnitude < 0)
                    magnitude = -magnitude;
                if (magnitude <= 1)
                    continue;

                if (minimum > -magnitude && maximum < magnitude) {
                    if (binary->op_id_ == Instruction::SDiv)
                        replacement =
                            new ConstantInt(module->int32_ty_, 0);
                    else if (binary->op_id_ == Instruction::SRem)
                        replacement = recurrence.phi;
                }
            } else if (auto *compare = dynamic_cast<ICmpInst *>(user)) {
                Value *other = nullptr;
                ICmpInst::ICmpOp predicate = compare->icmp_op_;
                if (compare->get_operand(0) == recurrence.phi) {
                    other = compare->get_operand(1);
                } else if (compare->get_operand(1) == recurrence.phi) {
                    other = compare->get_operand(0);
                    predicate = swapPredicate(predicate);
                }
                auto *constant = dynamic_cast<ConstantInt *>(other);
                bool result = false;
                if (constant &&
                    proveRangeComparison(predicate, minimum, maximum,
                                         constant->value_, result))
                    replacement =
                        new ConstantInt(module->int1_ty_, result ? 1 : 0);
            }

            if (!replacement)
                continue;
            user->replace_all_use_with(replacement);
            if (user->use_list_.empty())
                user->parent_->delete_instr(user);
            changed = true;

            if (debugEnabled())
                std::cerr
                    << "[IndVarSimplify] simplified constant-range IV user"
                    << " header=" << loop.header->name_ << "\n";
        }
    }
    return changed;
}

/**
 * @brief 查找 header PHI 来自循环 preheader 的初始入值。
 * @param phi 待查询的 header PHI。
 * @param loop PHI 所属循环。
 * @return 找到时返回初始入值，否则返回 nullptr。
 */
Value *preheaderIncomingValue(PhiInst *phi, const Loop &loop) {
    if (!phi || !loop.preheader)
        return nullptr;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == loop.preheader)
            return phi->get_operand(i);
    }
    return nullptr;
}

/**
 * @brief 原地执行 rewriteFirstIterationExitValues 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool rewriteFirstIterationExitValues(Loop &loop) {
    if (!loop.header || !loop.preheader)
        return false;
    auto *branch =
        dynamic_cast<BranchInst *>(loop.header->get_terminator());
    if (!branch || branch->num_ops() != 3)
        return false;

    std::set<Value *> visiting;
    if (!isLoopInvariantExpression(branch->get_operand(0), loop, visiting))
        return false;

    BasicBlock *exit = nullptr;
    auto *trueBlock =
        dynamic_cast<BasicBlock *>(branch->get_operand(1));
    auto *falseBlock =
        dynamic_cast<BasicBlock *>(branch->get_operand(2));
    if (!trueBlock || !falseBlock)
        return false;
    if (!loop.blocks.count(trueBlock) && loop.blocks.count(falseBlock))
        exit = trueBlock;
    else if (!loop.blocks.count(falseBlock) && loop.blocks.count(trueBlock))
        exit = falseBlock;
    else
        return false;

    bool changed = false;
    for (auto *inst : exit->instr_list_) {
        if (!inst->is_phi()) break;
        auto *exitPhi = static_cast<PhiInst *>(inst);
        for (unsigned i = 0; i + 1 < exitPhi->num_ops(); i += 2) {
            if (exitPhi->get_operand(i + 1) != loop.header)
                continue;
            auto *headerPhi =
                dynamic_cast<PhiInst *>(exitPhi->get_operand(i));
            if (!headerPhi || headerPhi->parent_ != loop.header)
                continue;
            Value *initial = preheaderIncomingValue(headerPhi, loop);
            if (!initial || exitPhi->get_operand(i) == initial)
                continue;
            exitPhi->set_operand(i, initial);
            changed = true;
        }
    }

    if (changed && debugEnabled())
        std::cerr << "[IndVarSimplify] rewrote first-iteration exit values"
                  << " header=" << loop.header->name_ << "\n";
    return changed;
}

/**
 * @brief 实现 eliminateCongruentIVs 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool eliminateCongruentIVs(Loop &loop,
                           ScalarEvolution &scalarEvolution) {
    std::vector<Recurrence> recurrences;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        Recurrence recurrence;
        if (matchRecurrence(static_cast<PhiInst *>(inst), loop, recurrence))
            recurrences.push_back(std::move(recurrence));
    }

    std::stable_sort(recurrences.begin(), recurrences.end(),
                     [&](const Recurrence &lhs, const Recurrence &rhs) {
                         bool lhsControl =
                             lhs.phi == loop.controlInduction.phi;
                         bool rhsControl =
                             rhs.phi == loop.controlInduction.phi;
                         return lhsControl && !rhsControl;
                     });

    bool changed = false;
    std::vector<Recurrence *> leaders;
    for (Recurrence &recurrence : recurrences) {
        Recurrence *leader = nullptr;
        long long affineDelta = 0;
        for (Recurrence *candidate : leaders) {
            if (equivalentRecurrence(*candidate, recurrence,
                                     scalarEvolution)) {
                leader = candidate;
                break;
            }
            if (constantStartDelta(*candidate, recurrence, scalarEvolution,
                                   affineDelta)) {
                leader = candidate;
                break;
            }
        }
        if (!leader) {
            leaders.push_back(&recurrence);
            continue;
        }

        if (recurrence.phi == loop.controlInduction.phi ||
            !replacementDoesNotStrengthenSemantics(*leader, recurrence,
                                                   loop))
            continue;

        if (affineDelta != 0) {
            auto *delta = new ConstantInt(recurrence.phi->type_, affineDelta);
            auto *current = new BinaryInst(
                recurrence.phi->type_, Instruction::Add, leader->phi, delta,
                loop.header, true);
            Instruction *firstNonPhi = nullptr;
            for (auto *instruction : loop.header->instr_list_)
                if (!instruction->is_phi()) {
                    firstNonPhi = instruction;
                    break;
                }
            if (!firstNonPhi || !loop.header->add_instruction_before_inst(
                                     current, firstNonPhi)) {
                delete current;
                continue;
            }

            auto *next = new BinaryInst(
                recurrence.phi->type_,
                recurrence.subtractStep ? Instruction::Sub
                                        : Instruction::Add,
                current, recurrence.step, recurrence.update->parent_, true);
            if (!recurrence.update->parent_->add_instruction_before_inst(
                    next, recurrence.update)) {
                loop.header->delete_instr(current);
                delete next;
                continue;
            }

            std::vector<Use> phiUses(recurrence.phi->use_list_.begin(),
                                     recurrence.phi->use_list_.end());
            for (const Use &use : phiUses) {
                auto *user = use.user_;
                if (user && user != recurrence.update)
                    user->set_operand(use.operand_index_, current);
            }
            std::vector<Use> updateUses(recurrence.update->use_list_.begin(),
                                        recurrence.update->use_list_.end());
            for (const Use &use : updateUses) {
                auto *user = use.user_;
                if (user && user != recurrence.phi)
                    user->set_operand(use.operand_index_, next);
            }
            recurrence.phi->parent_->delete_instr(recurrence.phi);
            recurrence.update->parent_->delete_instr(recurrence.update);
            changed = true;
            if (debugEnabled())
                std::cerr << "[IndVarSimplify] eliminated affine IV header="
                          << loop.header->name_ << " delta=" << affineDelta
                          << "\n";
            continue;
        }

        if (!onlyUsedBy(recurrence.update, recurrence.phi))
            continue;

        std::vector<Use> uses(recurrence.phi->use_list_.begin(),
                              recurrence.phi->use_list_.end());
        for (const Use &use : uses) {
            if (use.user_ == recurrence.update)
                continue;
            auto *user = use.user_;
            if (user)
                user->set_operand(use.operand_index_, leader->phi);
        }

        if (!onlyUsedBy(recurrence.phi, recurrence.update))
            continue;
        recurrence.phi->parent_->delete_instr(recurrence.phi);
        recurrence.update->parent_->delete_instr(recurrence.update);
        changed = true;

        if (debugEnabled())
            std::cerr << "[IndVarSimplify] eliminated congruent IV header="
                      << loop.header->name_ << "\n";
    }
    return changed;
}

/**
 * @brief 原地执行 rewriteConstantExitValues 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param loop 待检查或变换的循环。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool rewriteConstantExitValues(Loop &loop, ScalarEvolution &scalarEvolution,
                               Module *module) {
    auto tripCount = scalarEvolution.getConstantTripCount(&loop);
    if (!tripCount || !loop.preheader || !loop.singleLatch())
        return false;

    std::vector<Recurrence> candidates;
    for (auto *inst : loop.header->instr_list_) {
        if (!inst->is_phi()) break;
        Recurrence recurrence;
        if (!matchRecurrence(static_cast<PhiInst *>(inst), loop, recurrence) ||
            recurrence.outsideUses.empty())
            continue;

        bool dead = isDeadExceptForLiveOut(recurrence);
        if (!dead && !hasFreeExitMaterialization(recurrence, *tripCount))
            continue;
        candidates.push_back(std::move(recurrence));
    }

    bool changed = false;
    for (const Recurrence &recurrence : candidates) {
        Value *exitValue =
            materializeExitValue(recurrence, *tripCount, loop, module);
        if (!exitValue) continue;

        for (const Use &use : recurrence.outsideUses) {
            auto *user = use.user_;
            if (user)
                user->set_operand(use.operand_index_, exitValue);
        }

        if (onlyUsedBy(recurrence.phi, recurrence.update) &&
            onlyUsedBy(recurrence.update, recurrence.phi)) {
            recurrence.phi->parent_->delete_instr(recurrence.phi);
            recurrence.update->parent_->delete_instr(recurrence.update);
        }
        changed = true;

        if (debugEnabled())
            std::cerr << "[IndVarSimplify] rewrote constant exit header="
                      << loop.header->name_
                      << " trips=" << *tripCount << "\n";
    }
    return changed;
}

/**
 * @brief 实现 simplifyLoopOnce 对应的局部分析或变换辅助逻辑。
 * @param loop 待检查或变换的循环。
 * @param scalarEvolution 参数 `scalarEvolution`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool simplifyLoopOnce(Loop &loop, ScalarEvolution &scalarEvolution,
                      Module *module) {
    // 各子变换都可能改变 PHI 递推图，因此一次只提交第一项成功改写。
    // 调用方随后重建 LoopInfo 和 ScalarEvolution，再从规范化控制流开始下一轮。
    // 任一子变换都会改变后续规则读取的递推图。首次改写后立即返回，
    // 由调用方重建 LoopInfo 和 ScalarEvolution，再开始下一轮匹配。
    if (canonicalizeConstantControl(loop, module))
        return true;
    if (rewriteFirstIterationExitValues(loop))
        return true;
    if (eliminateCongruentIVs(loop, scalarEvolution))
        return true;
    if (simplifyRangeUsers(loop, scalarEvolution, module))
        return true;
    return rewriteConstantExitValues(loop, scalarEvolution, module);
}

} // namespace

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void IndVarSimplify::execute(Module *module) {
    AnalysisManager analysisManager;
    execute(module, analysisManager);
}

/**
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param analysisManager 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses IndVarSimplify::execute(Module *module,
                                          AnalysisManager &analysisManager) {
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, analysisManager);
    }
    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveBasicAA();
    return preserved;
}

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param func 待分析或改写的函数。
 * @param analysisManager 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool IndVarSimplify::runOnFunction(Function *func,
                                   AnalysisManager &analysisManager) {
    if (!func || func->basic_blocks_.empty())
        return false;

    bool changed = false;
    while (true) {
        LoopInfo &loopInfo = analysisManager.getLoopInfo(func);
        ScalarEvolution &scalarEvolution =
            analysisManager.getScalarEvolution(func);
        std::vector<Loop *> loops;
        for (const auto &loop : loopInfo.allLoops())
            loops.push_back(loop.get());
        std::sort(loops.begin(), loops.end(),
                  [](Loop *lhs, Loop *rhs) {
                      return lhs->depth > rhs->depth;
                  });

        bool iterationChanged = false;
        for (Loop *loop : loops) {
            if (!simplifyLoopOnce(*loop, scalarEvolution, func->parent_))
                continue;
            changed = true;
            iterationChanged = true;
            analysisManager.clear(func);
            break;
        }
        if (!iterationChanged)
            break;
    }
    return changed;
}
