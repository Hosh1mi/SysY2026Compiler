/**
 * @file loopSkewing.cpp
 * @brief 循环倾斜：依据仿射依赖距离对循环迭代域做倾斜变换，为并行化或向量化创造合法次序。
 * @details 从仿射访问计算依赖距离并选择倾斜系数；只有变换后的距离保持词典序为正时生成新迭代域。
 */

#include "../../../include/mid/opt/loopSkewing.hpp"

#include "../../../include/mid/analysis/analysisManager.hpp"
#include "../../../include/mid/analysis/affineAnalysis.hpp"
#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../../include/mid/opt/loopWorklist.hpp"
#include "../../../include/mid/transform/loopCloneUtils.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

/**
 * @brief 读取调试开关并判断是否输出诊断信息。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool debugEnabled() {
    return std::getenv("DEBUG_LOOP_SKEWING") != nullptr;
}

/**
 * @brief 生成 reject 对应的调试诊断，不参与程序语义。
 * @param loop 待检查或变换的循环。
 * @param reason 拒绝变换或匹配失败的原因。
 * @param output 可选的诊断信息输出参数。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void reject(const Loop &loop, const std::string &reason,
            std::string *output) {
    if (output) *output = reason;
    if (debugEnabled())
        std::cerr << "[LoopSkewing] reject header=" << loop.header->name_
                  << ": " << reason << "\n";
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
 * @brief 实现 affineInOuter 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param outerIV 参数 `outerIV`，用于本函数的分析、匹配或 IR 构造。
 * @param coefficient 参数 `coefficient`，用于本函数的分析、匹配或 IR 构造。
 * @param offset 参数 `offset`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool affineInOuter(Value *value, PhiInst *outerIV, long long &coefficient,
                   long long &offset) {
    if (value == outerIV) {
        coefficient = 1;
        offset = 0;
        return true;
    }
    if (auto *constant = dynamic_cast<ConstantInt *>(value)) {
        coefficient = 0;
        offset = constant->value_;
        return true;
    }

    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || (!binary->is_add() && !binary->is_sub()))
        return false;

    long long lhsCoefficient = 0;
    long long lhsOffset = 0;
    long long rhsCoefficient = 0;
    long long rhsOffset = 0;
    if (!affineInOuter(binary->get_operand(0), outerIV, lhsCoefficient,
                       lhsOffset) ||
        !affineInOuter(binary->get_operand(1), outerIV, rhsCoefficient,
                       rhsOffset))
        return false;

    if (binary->is_sub()) {
        rhsCoefficient = -rhsCoefficient;
        rhsOffset = -rhsOffset;
    }
    coefficient = lhsCoefficient + rhsCoefficient;
    offset = lhsOffset + rhsOffset;
    return coefficient >= -1 && coefficient <= 1;
}

/**
 * @brief 在加减表达式中查找唯一的外层 header PHI 来源。
 * @param value 待分析的表达式值。
 * @param outer 目标外层循环。
 * @param visited 已访问值集合，用于避免递归环。
 * @return 找到唯一来源时返回该 PHI，否则返回 nullptr。
 */
PhiInst *findParentPhi(Value *value, Loop *outer,
                       std::set<Value *> &visited) {
    if (!value || !outer || !visited.insert(value).second)
        return nullptr;
    if (auto *phi = dynamic_cast<PhiInst *>(value))
        return phi->parent_ == outer->header ? phi : nullptr;
    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || (!binary->is_add() && !binary->is_sub()))
        return nullptr;
    PhiInst *lhs = findParentPhi(binary->get_operand(0), outer, visited);
    PhiInst *rhs = findParentPhi(binary->get_operand(1), outer, visited);
    if (lhs && rhs && lhs != rhs) return nullptr;
    return lhs ? lhs : rhs;
}

/**
 * @brief 实现 edgeProvesAtLeast 对应的局部分析或变换辅助逻辑。
 * @param source 参数 `source`，用于本函数的分析、匹配或 IR 构造。
 * @param target 参数 `target`，用于本函数的分析、匹配或 IR 构造。
 * @param subject 参数 `subject`，用于本函数的分析、匹配或 IR 构造。
 * @param threshold 参数 `threshold`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool edgeProvesAtLeast(BasicBlock *source, BasicBlock *target,
                       Value *subject, long long threshold) {
    if (!source || !target || !subject) return false;
    BasicBlock *guard = source;
    BasicBlock *edgeTarget = target;
    auto *branch = dynamic_cast<BranchInst *>(guard->get_terminator());
    if (branch && branch->num_ops() == 1 && branch->get_operand(0) == target &&
        source->pre_bbs_.size() == 1) {
        edgeTarget = source;
        guard = source->pre_bbs_.front();
        branch = dynamic_cast<BranchInst *>(guard->get_terminator());
    }
    if (!branch || branch->num_ops() != 3)
        return false;

    auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    auto *constant = compare
                         ? dynamic_cast<ConstantInt *>(compare->get_operand(1))
                         : nullptr;
    if (!compare || compare->get_operand(0) != subject || !constant)
        return false;

    const bool trueEdge = branch->get_operand(1) == edgeTarget;
    const bool falseEdge = branch->get_operand(2) == edgeTarget;
    if (trueEdge == falseEdge) return false;
    long long knownLower = std::numeric_limits<long long>::min();
    if (trueEdge && compare->icmp_op_ == ICmpInst::ICMP_SGE)
        knownLower = constant->value_;
    else if (trueEdge && compare->icmp_op_ == ICmpInst::ICMP_SGT)
        knownLower = constant->value_ + 1;
    else if (falseEdge && compare->icmp_op_ == ICmpInst::ICMP_SLT)
        knownLower = constant->value_;
    else if (falseEdge && compare->icmp_op_ == ICmpInst::ICMP_SLE)
        knownLower = constant->value_ + 1;
    return knownLower >= threshold;
}

/**
 * @brief 实现 incomingIsNonNegative 对应的局部分析或变换辅助逻辑。
 * @param value 待检查、映射或物化的 IR 值。
 * @param source 参数 `source`，用于本函数的分析、匹配或 IR 构造。
 * @param target 参数 `target`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool incomingIsNonNegative(Value *value, BasicBlock *source,
                           BasicBlock *target) {
    if (auto *constant = dynamic_cast<ConstantInt *>(value))
        return constant->value_ >= 0;

    Value *subject = value;
    long long required = 0;
    if (auto *binary = dynamic_cast<BinaryInst *>(value)) {
        auto *constant = dynamic_cast<ConstantInt *>(binary->get_operand(1));
        if (!constant) return false;
        if (binary->is_sub()) {
            subject = binary->get_operand(0);
            required = constant->value_;
        } else if (binary->is_add()) {
            subject = binary->get_operand(0);
            required = -constant->value_;
        } else {
            return false;
        }
    }
    return edgeProvesAtLeast(source, target, subject, required);
}

/**
 * @brief 实现 phiIsNonNegative 对应的局部分析或变换辅助逻辑。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool phiIsNonNegative(PhiInst *phi) {
    if (!phi || !phi->parent_ || phi->num_ops() == 0)
        return false;
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
        auto *source = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
        if (!source ||
            !incomingIsNonNegative(phi->get_operand(i), source, phi->parent_))
            return false;
    }
    return true;
}

/**
 * @brief 实现 provePositiveStart 对应的局部分析或变换辅助逻辑。
 * @param plan 参数 `plan`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool provePositiveStart(const LoopSkewPlan &plan) {
    if (plan.outerCoefficient != 1 || plan.offset < 1)
        return false;
    return phiIsNonNegative(plan.outerIV);
}

/**
 * @brief 实现 preheaderProvesNonEmpty 对应的局部分析或变换辅助逻辑。
 * @param plan 参数 `plan`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool preheaderProvesNonEmpty(const LoopSkewPlan &plan) {
    if (!plan.preheader || plan.preheader->pre_bbs_.size() != 1)
        return false;
    BasicBlock *guardBlock = plan.preheader->pre_bbs_.front();
    auto *branch = dynamic_cast<BranchInst *>(guardBlock->get_terminator());
    if (!branch || branch->num_ops() != 3 ||
        branch->get_operand(1) != plan.preheader)
        return false;
    auto *compare = dynamic_cast<ICmpInst *>(branch->get_operand(0));
    return compare && compare->icmp_op_ == ICmpInst::ICMP_SLT &&
           compare->get_operand(0) == plan.innerStart &&
           compare->get_operand(1) == plan.innerBound;
}

/**
 * @brief 判断 hasUnsupportedUses 所描述的结构、合法性或安全条件是否成立。
 * @param plan 参数 `plan`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool hasUnsupportedUses(const LoopSkewPlan &plan) {
    for (const Use &use : plan.innerIV->use_list_) {
        auto *user = use.user_;
        if (!user || !user->parent_ ||
            (!plan.inner->blocks.count(user->parent_) &&
             user != plan.innerUpdate))
            return true;
    }
    for (const Use &use : plan.innerUpdate->use_list_) {
        auto *user = use.user_;
        if (user != plan.innerIV && user != plan.innerCompare)
            return true;
    }
    return false;
}

/**
 * @brief 查找基本块中第一条非 PHI 指令。
 * @param block 待搜索的基本块。
 * @return 找到时返回指令；基本块仅含 PHI 时返回 nullptr。
 */
Instruction *firstNonPhi(BasicBlock *block) {
    for (auto *instruction : block->instr_list_)
        if (!instruction->is_phi()) return instruction;
    return nullptr;
}

} // namespace

/**
 * @brief 分析 LoopSkew 的结构、递推或依赖信息。
 * @param inner 待分析的内层循环。
 * @param reason 拒绝变换或匹配失败的原因。
 * @return 成功时返回分析或变换计划，失败时返回空值。
 */
std::optional<LoopSkewPlan> analyzeLoopSkew(Loop &inner,
                                            std::string *reason) {
    // 分析阶段从内层控制 IV 提取关于外层 IV 的仿射起点和边界，
    // 再用依赖距离求合法倾斜系数；仅生成计划，不在证明过程中修改 CFG。
    if (!inner.parent) {
        reject(inner, "no parent loop", reason);
        return std::nullopt;
    }
    if (!inner.preheader || !inner.singleLatch()) {
        reject(inner, "unsupported nested CFG", reason);
        return std::nullopt;
    }

    const InductionDescriptor *control = inner.getInductionDescriptor();
    if (!control || !control->constantStep || *control->constantStep != 1 ||
        control->predicate != ICmpInst::ICMP_SLT ||
        control->guardPosition != InductionGuardPosition::Latch ||
        !control->comparesUpdate) {
        reject(inner, "requires rotated signed +1 control", reason);
        return std::nullopt;
    }
    if (!isI32(control->phi) || !isI32(control->start) ||
        !isI32(control->bound)) {
        reject(inner, "requires i32 control values", reason);
        return std::nullopt;
    }
    auto *latchBranch = dynamic_cast<BranchInst *>(
        inner.singleLatch()->get_terminator());
    if (!latchBranch || latchBranch->num_ops() != 3 ||
        latchBranch->get_operand(0) != control->compare ||
        !inner.blocks.count(
            dynamic_cast<BasicBlock *>(latchBranch->get_operand(1))) ||
        inner.blocks.count(
            dynamic_cast<BasicBlock *>(latchBranch->get_operand(2))) ||
        control->compare->icmp_op_ != ICmpInst::ICMP_SLT ||
        control->compare->get_operand(0) != control->update ||
        control->compare->get_operand(1) != control->bound) {
        reject(inner, "requires a true-edge latch guard update < bound",
               reason);
        return std::nullopt;
    }

    std::set<Value *> visited;
    PhiInst *outerIV = findParentPhi(control->start, inner.parent, visited);
    if (!outerIV) {
        reject(inner, "parent has no affine control IV", reason);
        return std::nullopt;
    }

    LoopSkewPlan plan;
    plan.outer = inner.parent;
    plan.inner = &inner;
    plan.outerIV = outerIV;
    plan.innerIV = control->phi;
    plan.innerUpdate = control->update;
    plan.innerCompare = control->compare;
    plan.innerStart = control->start;
    plan.innerBound = control->bound;
    plan.preheader = inner.preheader;
    plan.latch = inner.singleLatch();
    if (!affineInOuter(plan.innerStart, outerIV, plan.outerCoefficient,
                       plan.offset) ||
        plan.outerCoefficient == 0) {
        reject(inner, "inner start is not coupled affinely to parent IV",
               reason);
        return std::nullopt;
    }
    if (!provePositiveStart(plan)) {
        reject(inner, "cannot prove skew coordinate arithmetic in i32",
               reason);
        return std::nullopt;
    }
    if (!preheaderProvesNonEmpty(plan)) {
        reject(inner, "missing dominating non-empty guard", reason);
        return std::nullopt;
    }
    if (hasUnsupportedUses(plan)) {
        reject(inner, "control IV or update has unsupported live-out", reason);
        return std::nullopt;
    }

    if (reason) reason->clear();
    return plan;
}

/**
 * @brief 原地执行 applyLoopSkew 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param plan 参数 `plan`，用于本函数的分析、匹配或 IR 构造。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyLoopSkew(const LoopSkewPlan &plan, Module *module) {
    // 计划已经给出新起点/边界的仿射表达式；此处只负责物化它们并重写控制 PHI。
    // 重连回边后同步更新退出 PHI，保证循环外观察到的仍是原迭代域最后状态。
    if (!plan.inner || !plan.innerIV || !plan.innerUpdate ||
        !plan.innerCompare || !plan.preheader || !plan.latch || !module)
        return false;

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    auto *distance = PhiInst::create_phi(module->int32_ty_, plan.inner->header);
    distance->add_phi_pair_operand(zero, plan.preheader);
    plan.inner->header->add_instruction_front(distance);

    auto *bodyIndex = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     plan.innerStart, distance,
                                     plan.inner->header, true);
    Instruction *insertionPoint = firstNonPhi(plan.inner->header);
    if (!insertionPoint ||
        !plan.inner->header->add_instruction_before_inst(bodyIndex,
                                                          insertionPoint))
        return false;

    auto *nextDistance = new BinaryInst(module->int32_ty_, Instruction::Add,
                                        distance, one, plan.latch, true);
    plan.latch->add_instruction_before_inst(nextDistance, plan.innerUpdate);
    distance->add_phi_pair_operand(nextDistance, plan.latch);

    auto *distanceBound = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                         plan.innerBound, plan.innerStart,
                                         plan.preheader, true);
    plan.preheader->add_instruction_before_terminator(distanceBound);

    std::vector<Use> ivUses(plan.innerIV->use_list_.begin(),
                            plan.innerIV->use_list_.end());
    for (const Use &use : ivUses) {
        auto *user = use.user_;
        if (!user || user == plan.innerUpdate) continue;
        user->set_operand(use.operand_index_, bodyIndex);
    }

    plan.innerCompare->set_operand(0, nextDistance);
    plan.innerCompare->set_operand(1, distanceBound);
    plan.innerCompare->icmp_op_ = ICmpInst::ICMP_SLT;

    plan.latch->delete_instr(plan.innerUpdate);
    plan.inner->header->delete_instr(plan.innerIV);
    plan.inner->header->parent_->set_instr_name();

    if (debugEnabled())
        std::cerr << "[LoopSkewing] transformed func="
                  << plan.inner->header->parent_->name_ << " header="
                  << plan.inner->header->name_ << " coordinate=(inner-start)"
                  << " outer-coeff=" << plan.outerCoefficient
                  << " offset=" << plan.offset << "\n";
    return true;
}

namespace {

/**
 * @brief 描述将矩形循环嵌套改写为按加权波前执行所需的完整计划。
 */
struct RectangularWavefrontPlan {
    std::vector<Loop *> nest;                              ///< 从外到内排列的矩形循环层级。
    std::vector<const InductionDescriptor *> controls;     ///< 各层循环的归纳变量描述。
    std::vector<long long> weights;                        ///< 各层坐标对波前编号的权重。
    std::vector<Instruction *> accesses;                   ///< 参与依赖距离检查的内存访问。
    BasicBlock *cell = nullptr;                            ///< 执行单个迭代空间单元的基本块。
    BasicBlock *preheader = nullptr;                       ///< 最外层循环预头。
    BasicBlock *exit = nullptr;                            ///< 整个循环嵌套的唯一出口。
    Value *start = nullptr;                                ///< 波前编号的起始值。
    Value *bound = nullptr;                                ///< 波前编号的开区间上界。
};

/**
 * @brief 实现 lexPositive 对应的局部分析或变换辅助逻辑。
 * @param distance 参数 `distance`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool lexPositive(const std::vector<long long> &distance) {
    for (long long component : distance) {
        if (component != 0) return component > 0;
    }
    return false;
}

/**
 * @brief 实现 allZero 对应的局部分析或变换辅助逻辑。
 * @param distance 参数 `distance`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool allZero(const std::vector<long long> &distance) {
    for (long long component : distance)
        if (component != 0) return false;
    return true;
}

/**
 * @brief 判断 sameValue 所描述的结构、合法性或安全条件是否成立。
 * @param lhs 表达式左操作数。
 * @param rhs 表达式右操作数。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool sameValue(Value *lhs, Value *rhs) {
    if (lhs == rhs) return true;
    auto *lc = dynamic_cast<ConstantInt *>(lhs);
    auto *rc = dynamic_cast<ConstantInt *>(rhs);
    return lc && rc && lc->value_ == rc->value_;
}

/**
 * @brief 实现 deriveScheduleWeights 对应的局部分析或变换辅助逻辑。
 * @param deps 参数 `deps`，用于本函数的分析、匹配或 IR 构造。
 * @param dimensions 参数 `dimensions`，用于本函数的分析、匹配或 IR 构造。
 * @param weights 参数 `weights`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool deriveScheduleWeights(const std::vector<std::vector<long long>> &deps,
                           size_t dimensions,
                           std::vector<long long> &weights) {
    weights.assign(dimensions, 1);
    if (dimensions == 0) return false;
    for (int level = static_cast<int>(dimensions) - 2; level >= 0; --level) {
        long long required = 1;
        for (const auto &distance : deps) {
            int first = -1;
            for (size_t i = 0; i < distance.size(); ++i)
                if (distance[i] != 0) {
                    first = static_cast<int>(i);
                    break;
                }
            if (first != level || distance[level] <= 0) continue;
            __int128 tail = 0;
            for (size_t i = level + 1; i < dimensions; ++i)
                tail += static_cast<__int128>(weights[i]) * distance[i];
            if (tail < 1) {
                __int128 numerator = 1 - tail;
                __int128 candidate =
                    (numerator + distance[level] - 1) / distance[level];
                if (candidate > std::numeric_limits<int>::max()) return false;
                required = std::max(required,
                                    static_cast<long long>(candidate));
            }
        }
        weights[level] = required;
    }
    for (const auto &distance : deps) {
        __int128 dot = 0;
        for (size_t i = 0; i < dimensions; ++i)
            dot += static_cast<__int128>(weights[i]) * distance[i];
        if (dot <= 0) return false;
    }
    return weights.back() == 1;
}

/**
 * @brief 分析 RectangularWavefront 的结构、递推或依赖信息。
 * @param outer 参数 `outer`，用于本函数的分析、匹配或 IR 构造。
 * @param LI 参数 `LI`，用于本函数的分析、匹配或 IR 构造。
 * @param argAlias 参数 `argAlias`，用于本函数的分析、匹配或 IR 构造。
 * @param basicAA 参数 `basicAA`，用于本函数的分析、匹配或 IR 构造。
 * @param reason 拒绝变换或匹配失败的原因。
 * @return 成功时返回分析或变换计划，失败时返回空值。
 */
std::optional<RectangularWavefrontPlan> analyzeRectangularWavefront(
    Loop &outer, LoopInfo &LI, const ArgumentAliasAnalysis &argAlias,
    const BasicAliasAnalysis &basicAA, std::string &reason) {
    RectangularWavefrontPlan plan;
    Loop *cursor = &outer;
    while (cursor && plan.nest.size() < 3) {
        plan.nest.push_back(cursor);
        if (cursor->children.size() > 1) {
            reason = "rectangular nest has multiple children";
            return std::nullopt;
        }
        cursor = cursor->children.empty() ? nullptr : cursor->children.front();
    }
    if (cursor || plan.nest.size() < 2) {
        reason = "requires a perfect two- or three-level nest";
        return std::nullopt;
    }
    plan.preheader = outer.preheader;
    plan.exit = outer.singleExit();
    if (!plan.preheader || !plan.exit) {
        reason = "outer loop lacks dedicated preheader or exit";
        return std::nullopt;
    }

    for (Loop *loop : plan.nest) {
        const InductionDescriptor *control = loop->getInductionDescriptor();
        if (!control || !control->constantStep ||
            *control->constantStep != 1 ||
            control->predicate != ICmpInst::ICMP_SLT ||
            !isI32(control->phi) || !loop->singleLatch()) {
            reason = "requires signed i32 unit-stride controls";
            return std::nullopt;
        }
        if (!sameValue(control->start,
                       plan.controls.empty() ? control->start : plan.start) ||
            !sameValue(control->bound,
                       plan.controls.empty() ? control->bound : plan.bound)) {
            reason = "requires a common rectangular iteration domain";
            return std::nullopt;
        }
        if (plan.controls.empty()) {
            plan.start = control->start;
            plan.bound = control->bound;
        }
        plan.controls.push_back(control);
    }
    auto *startConstant = dynamic_cast<ConstantInt *>(plan.start);
    if (!startConstant || startConstant->value_ < 0) {
        reason = "requires a non-negative constant lower bound";
        return std::nullopt;
    }

    Loop *inner = plan.nest.back();
    if (inner->blocks.size() != 1 || inner->header != inner->singleLatch()) {
        reason = "requires a single-block cell loop";
        return std::nullopt;
    }
    plan.cell = inner->header;
    unsigned storeCount = 0;
    for (auto *instruction : plan.cell->instr_list_) {
        if (instruction->is_load() || instruction->is_store()) {
            plan.accesses.push_back(instruction);
            storeCount += instruction->is_store();
        }
        if (instruction->is_call()) {
            auto *call = static_cast<CallInst *>(instruction);
            auto *callee = dynamic_cast<Function *>(
                call->get_operand(call->num_ops() - 1));
            if (!callee || !basicAA.isPure(callee)) {
                reason = "cell contains an impure call";
                return std::nullopt;
            }
        }
    }
    if (storeCount != 1) {
        reason = "requires exactly one cell store";
        return std::nullopt;
    }

    AffineAnalysis affine(LI);
    DependenceAnalysis dependence(LI, affine);
    dependence.setArgAlias(&argAlias);
    std::vector<std::vector<long long>> distances;
    for (Instruction *store : plan.accesses) {
        if (!store->is_store()) continue;
        for (Instruction *access : plan.accesses) {
            if (access != store && !access->is_load() && !access->is_store())
                continue;
            std::vector<std::pair<Instruction *, Instruction *>> orientations;
            if (access->is_load()) {
                orientations.push_back({store, access});
                orientations.push_back({access, store});
            } else {
                orientations.push_back({store, access});
            }
            for (auto [source, sink] : orientations) {
                auto result = dependence.getConstantDistance(
                    source, sink, plan.nest);
                if (result.status ==
                    DependenceAnalysis::DistanceStatus::Unknown) {
                    reason = "memory dependence has no exact distance";
                    return std::nullopt;
                }
                if (result.status ==
                        DependenceAnalysis::DistanceStatus::Exact &&
                    !allZero(result.distance) && lexPositive(result.distance))
                    distances.push_back(std::move(result.distance));
            }
        }
    }
    if (distances.empty()) {
        reason = "rectangular nest is DOALL";
        return std::nullopt;
    }
    if (plan.nest.size() == 3) {
        std::vector<std::vector<long long>> projected;
        for (const auto &distance : distances) {
            if (distance[0] != 0 || distance[1] != 0)
                projected.push_back({distance[0], distance[1]});
        }
        std::vector<long long> outerWeights;
        if (projected.empty() ||
            !deriveScheduleWeights(projected, 2, outerWeights)) {
            reason = "no bounded locality-preserving outer wave schedule";
            return std::nullopt;
        }
        plan.weights = {outerWeights[0], outerWeights[1], 0};
        for (const auto &distance : distances) {
            __int128 waveDistance =
                static_cast<__int128>(plan.weights[0]) * distance[0] +
                static_cast<__int128>(plan.weights[1]) * distance[1];
            // 同一波次中的依赖只能落在同一个 (i,j) lane 内，因为该 lane 仍按原顺序
            // 串行执行最内层循环；跨 lane 的零距离波次依赖会破坏执行次序。
            if (waveDistance < 0 ||
                (waveDistance == 0 &&
                 (distance[0] != 0 || distance[1] != 0 ||
                  distance[2] <= 0))) {
                reason = "outer wave leaves a cross-lane dependence";
                return std::nullopt;
            }
        }
    } else if (!deriveScheduleWeights(distances, plan.nest.size(),
                                      plan.weights)) {
        reason = "no bounded positive affine wave schedule";
        return std::nullopt;
    }
    reason.clear();
    return plan;
}

/**
 * @brief 生成两个有符号整数值的最小值选择表达式。
 * @param lhs 左操作数。
 * @param rhs 右操作数。
 * @param block 指令插入基本块。
 * @return select(lhs<rhs, lhs, rhs) 的结果值。
 */
Value *emitSelectMin(Value *lhs, Value *rhs, BasicBlock *block) {
    auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, lhs, rhs, block);
    return new SelectInst(cmp, lhs, rhs, block);
}

/**
 * @brief 生成两个有符号整数值的最大值选择表达式。
 * @param lhs 左操作数。
 * @param rhs 右操作数。
 * @param block 指令插入基本块。
 * @return select(lhs>rhs, lhs, rhs) 的结果值。
 */
Value *emitSelectMax(Value *lhs, Value *rhs, BasicBlock *block) {
    auto *cmp = new ICmpInst(ICmpInst::ICMP_SGT, lhs, rhs, block);
    return new SelectInst(cmp, lhs, rhs, block);
}

/**
 * @brief 生成对正除数取数学下整的有符号除法。
 * @param value 被除数。
 * @param divisor 正常量除数。
 * @param module 所属模块。
 * @param block 指令插入基本块。
 * @return floor(value/divisor) 对应的 IR 值。
 */
Value *emitFloorDiv(Value *value, long long divisor, Module *module,
                    BasicBlock *block) {
    if (divisor == 1) return value;
    auto *d = new ConstantInt(module->int32_ty_, divisor);
    auto *q = new BinaryInst(module->int32_ty_, Instruction::SDiv,
                             value, d, block);
    auto *r = new BinaryInst(module->int32_ty_, Instruction::SRem,
                             value, new ConstantInt(module->int32_ty_, divisor),
                             block);
    auto *negative = new ICmpInst(ICmpInst::ICMP_SLT, value,
                                  new ConstantInt(module->int32_ty_, 0), block);
    auto *hasRemainder = new ICmpInst(
        ICmpInst::ICMP_NE, r, new ConstantInt(module->int32_ty_, 0), block);
    auto *adjust = new BinaryInst(module->int1_ty_, Instruction::And,
                                  negative, hasRemainder, block);
    auto *minusOne = new BinaryInst(module->int32_ty_, Instruction::Sub, q,
                                    new ConstantInt(module->int32_ty_, 1), block);
    return new SelectInst(adjust, minusOne, q, block);
}

/**
 * @brief 生成对正除数取数学上整的有符号除法。
 * @param value 被除数。
 * @param divisor 正常量除数。
 * @param module 所属模块。
 * @param block 指令插入基本块。
 * @return ceil(value/divisor) 对应的 IR 值。
 */
Value *emitCeilDiv(Value *value, long long divisor, Module *module,
                   BasicBlock *block) {
    if (divisor == 1) return value;
    auto *q = new BinaryInst(module->int32_ty_, Instruction::SDiv, value,
                             new ConstantInt(module->int32_ty_, divisor), block);
    auto *r = new BinaryInst(module->int32_ty_, Instruction::SRem, value,
                             new ConstantInt(module->int32_ty_, divisor), block);
    auto *positive = new ICmpInst(ICmpInst::ICMP_SGT, value,
                                  new ConstantInt(module->int32_ty_, 0), block);
    auto *hasRemainder = new ICmpInst(
        ICmpInst::ICMP_NE, r, new ConstantInt(module->int32_ty_, 0), block);
    auto *adjust = new BinaryInst(module->int1_ty_, Instruction::And,
                                  positive, hasRemainder, block);
    auto *plusOne = new BinaryInst(module->int32_ty_, Instruction::Add, q,
                                   new ConstantInt(module->int32_ty_, 1), block);
    return new SelectInst(adjust, plusOne, q, block);
}

/**
 * @brief 在波前新 CFG 中递归重建无副作用的循环内表达式。
 * @param value 待重建的原值。
 * @param block 新指令的插入基本块。
 * @param map 原值到新值的缓存映射。
 * @param nestBlocks 原循环嵌套包含的基本块集合。
 * @return 重建或可直接复用的值；遇到内存、副作用或 PHI 时返回 nullptr。
 */
Value *rematerialize(Value *value, BasicBlock *block,
                     std::unordered_map<Value *, Value *> &map,
                     const std::set<BasicBlock *> &nestBlocks) {
    auto found = map.find(value);
    if (found != map.end()) return found->second;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction || !instruction->parent_ ||
        !nestBlocks.count(instruction->parent_))
        return value;
    if (instruction->is_phi() || instruction->isTerminator() ||
        instruction->is_load() || instruction->is_store() ||
        instruction->is_call())
        return nullptr;
    if (auto *binary = dynamic_cast<BinaryInst *>(instruction)) {
        Value *lhs = rematerialize(binary->get_operand(0), block, map,
                                   nestBlocks);
        Value *rhs = rematerialize(binary->get_operand(1), block, map,
                                   nestBlocks);
        if (!lhs || !rhs) return nullptr;
        auto *clone = new BinaryInst(binary->type_, binary->op_id_, lhs, rhs,
                                     block);
        clone->copySemFlagsFrom(binary);
        map[value] = clone;
        return clone;
    }
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(instruction)) {
        Value *base = rematerialize(gep->get_operand(0), block, map,
                                    nestBlocks);
        if (!base) return nullptr;
        std::vector<Value *> indices;
        for (unsigned i = 1; i < gep->num_ops(); ++i) {
            Value *index = rematerialize(gep->get_operand(i), block, map,
                                         nestBlocks);
            if (!index) return nullptr;
            indices.push_back(index);
        }
        auto *clone = new GetElementPtrInst(base, indices, block);
        clone->copySemFlagsFrom(gep);
        map[value] = clone;
        return clone;
    }
    return nullptr;
}

/**
 * @brief 在矩形波前退出处递归构造原循环值的最终迭代结果。
 * @param value 待恢复的原循环值。
 * @param plan 已验证的矩形波前计划。
 * @param block 最终值的插入基本块。
 * @param cache 已物化结果缓存。
 * @param visiting 当前递归栈，用于拒绝无法解析的值环。
 * @return 可恢复时返回最终值，否则返回 nullptr。
 */
Value *materializeFinalValue(
    Value *value, const RectangularWavefrontPlan &plan, BasicBlock *block,
    std::unordered_map<Value *, Value *> &cache,
    std::set<Value *> &visiting) {
    auto found = cache.find(value);
    if (found != cache.end()) return found->second;
    if (!value || dynamic_cast<Constant *>(value) ||
        dynamic_cast<Argument *>(value) ||
        dynamic_cast<GlobalVariable *>(value))
        return value;
    if (!visiting.insert(value).second) return nullptr;

    for (const InductionDescriptor *control : plan.controls) {
        if (value == control->update) {
            visiting.erase(value);
            return cache[value] = control->bound;
        }
        if (value == control->phi) {
            if (control->guardPosition == InductionGuardPosition::Header) {
                visiting.erase(value);
                return cache[value] = control->bound;
            }
            auto *last = new BinaryInst(
                control->phi->type_, Instruction::Sub, control->bound,
                new ConstantInt(control->phi->type_, 1), block);
            visiting.erase(value);
            return cache[value] = last;
        }
    }
    if (auto *phi = dynamic_cast<PhiInst *>(value)) {
        auto dependsOnCompletedIteration = [&](auto &&self, Value *candidate,
                                               std::set<Value *> &seen)
            -> bool {
            for (const InductionDescriptor *control : plan.controls)
                if (candidate == control->update)
                    return true;
            auto *instruction = dynamic_cast<Instruction *>(candidate);
            if (!instruction || !seen.insert(candidate).second)
                return false;
            for (unsigned i = 0; i < instruction->num_ops(); ++i) {
                if (dynamic_cast<BasicBlock *>(instruction->get_operand(i)))
                    continue;
                if (self(self, instruction->get_operand(i), seen))
                    return true;
            }
            return false;
        };

        // 快速路径只会在公共迭代域非空时进入。若 LCSSA PHI 合并“空循环值”和
        // “完整执行值”，应先选后者再递归物化，避免错误采用未执行路径的入值。
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            std::set<Value *> seen;
            if (!dependsOnCompletedIteration(
                    dependsOnCompletedIteration, phi->get_operand(i), seen))
                continue;
            Value *mapped = materializeFinalValue(
                phi->get_operand(i), plan, block, cache, visiting);
            if (mapped) {
                visiting.erase(value);
                return cache[value] = mapped;
            }
        }
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            for (const InductionDescriptor *control : plan.controls) {
                if (phi->get_operand(i) != control->update) continue;
                Value *mapped = materializeFinalValue(
                    phi->get_operand(i), plan, block, cache, visiting);
                if (mapped) {
                    visiting.erase(value);
                    return cache[value] = mapped;
                }
            }
        }
        for (auto loopIt = plan.nest.rbegin(); loopIt != plan.nest.rend();
             ++loopIt) {
            for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
                auto *pred =
                    dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
                if (!pred || !(*loopIt)->blocks.count(pred)) continue;
                Value *mapped = materializeFinalValue(
                    phi->get_operand(i), plan, block, cache, visiting);
                if (mapped) {
                    visiting.erase(value);
                    return cache[value] = mapped;
                }
            }
        }
    }
    if (auto *binary = dynamic_cast<BinaryInst *>(value)) {
        Value *lhs = materializeFinalValue(binary->get_operand(0), plan,
                                           block, cache, visiting);
        Value *rhs = materializeFinalValue(binary->get_operand(1), plan,
                                           block, cache, visiting);
        if (lhs && rhs) {
            auto *mapped = new BinaryInst(binary->type_, binary->op_id_, lhs,
                                          rhs, block);
            mapped->copySemFlagsFrom(binary);
            visiting.erase(value);
            return cache[value] = mapped;
        }
    }
    visiting.erase(value);
    return nullptr;
}

/**
 * @brief 查询 PHI 来自指定前驱的入值。
 * @param phi 待查询的 PHI 指令。
 * @param predecessor 指定前驱基本块。
 * @return 找到时返回对应值，否则返回 nullptr。
 */
Value *phiIncomingFrom(PhiInst *phi, BasicBlock *predecessor) {
    for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2)
        if (phi->get_operand(i + 1) == predecessor)
            return phi->get_operand(i);
    return nullptr;
}

/**
 * @brief 判断 isUnitPointerRecurrence 所描述的结构、合法性或安全条件是否成立。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param preheader 循环预头基本块。
 * @param initial 参数 `initial`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool isUnitPointerRecurrence(PhiInst *phi, BasicBlock *preheader,
                             Value *&initial) {
    initial = phiIncomingFrom(phi, preheader);
    Value *back = phiIncomingFrom(phi, phi->parent_);
    auto *gep = dynamic_cast<GetElementPtrInst *>(back);
    auto *one = gep && gep->num_ops() == 2
                    ? dynamic_cast<ConstantInt *>(gep->get_operand(1))
                    : nullptr;
    return initial && gep && gep->get_operand(0) == phi && one &&
           one->value_ == 1;
}

/**
 * @brief 原地执行 applyRectangularWavefront 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param plan 参数 `plan`，用于本函数的分析、匹配或 IR 构造。
 * @param function 待分析或改写的函数。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param newLoopHeaders 参数 `newLoopHeaders`，用于本函数的分析、匹配或 IR 构造。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool applyRectangularWavefront(const RectangularWavefrontPlan &plan,
                               Function *function, Module *module,
                               std::vector<BasicBlock *> &newLoopHeaders) {
    // 用 schedule 权重建立按波次推进的外层，再在每个波次枚举合法点。
    // 依赖顺序由计划保证；提交阶段负责克隆计算单元和重建所有跨层 PHI。
    std::vector<BasicBlock *> createdBlocks;
    auto fail = [&](const std::string &reason) {
        if (debugEnabled())
            std::cerr << "[LoopSkewing] rectangular apply rejected: "
                      << reason << "\n";
        std::vector<Instruction *> garbage;
        for (BasicBlock *block : createdBlocks)
            garbage.insert(garbage.end(), block->instr_list_.begin(),
                           block->instr_list_.end());
        for (BasicBlock *block : createdBlocks) {
            std::vector<Instruction *> instructions(block->instr_list_.begin(),
                                                     block->instr_list_.end());
            for (Instruction *instruction : instructions)
                block->delete_instr(instruction);
        }
        for (Instruction *instruction : garbage) delete instruction;
        for (BasicBlock *block : createdBlocks)
            function->remove_bb(block);
        for (BasicBlock *block : createdBlocks) delete block;
        createdBlocks.clear();
        return false;
    };
    if (!function || !module || plan.nest.size() < 2 ||
        plan.nest.size() > 3 || plan.weights.size() != plan.nest.size())
        return fail("invalid plan");

    Loop *outer = plan.nest.front();
    Loop *inner = plan.nest.back();
    BasicBlock *oldPreheader = plan.preheader;
    auto *oldPreTerm = oldPreheader->get_terminator();
    if (!oldPreTerm) return fail("preheader has no terminator");
    bool hasOuterEdge = false;
    for (unsigned i = 0; i < oldPreTerm->num_ops(); ++i)
        hasOuterEdge |= oldPreTerm->get_operand(i) == outer->header;
    if (!hasOuterEdge) return fail("preheader has no edge to outer header");

    std::vector<PhiInst *> pointerPhis;
    std::unordered_map<PhiInst *, Value *> pointerStarts;
    for (auto *instruction : plan.cell->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi || phi == plan.controls.back()->phi) continue;
        Value *initial = nullptr;
        if (!dynamic_cast<PointerType *>(phi->type_) ||
            !isUnitPointerRecurrence(phi, inner->preheader, initial))
            return fail("unsupported cell phi " + phi->name_);
        pointerPhis.push_back(phi);
        pointerStarts[phi] = initial;
    }

    std::vector<Instruction *> liveOuts;
    for (BasicBlock *block : outer->blocks) {
        for (Instruction *instruction : block->instr_list_) {
            for (const Use &use : instruction->use_list_) {
                auto *user = use.user_;
                if (!user || !user->parent_ ||
                    outer->blocks.count(user->parent_))
                    continue;
                if (std::find(liveOuts.begin(), liveOuts.end(), instruction) ==
                    liveOuts.end())
                    liveOuts.push_back(instruction);
            }
        }
    }
    for (BasicBlock *pred : plan.exit->pre_bbs_)
        if (!outer->blocks.count(pred))
            return fail("outer exit has a non-loop predecessor");

    long long weightSum = 0;
    for (size_t dimension = 0; dimension < plan.weights.size(); ++dimension) {
        long long weight = plan.weights[dimension];
        const bool serialInnermost = plan.nest.size() == 3 &&
                                     dimension == 2 && weight == 0;
        if ((!serialInnermost && weight <= 0) ||
            weightSum > std::numeric_limits<int>::max() - weight)
            return fail("schedule weight overflow");
        weightSum += weight;
    }

    int serial = static_cast<int>(function->basic_blocks_.size());
    auto makeBlock = [&](const std::string &tag) {
        auto *block = new BasicBlock(module,
                                     "wavefront." + tag + "." +
                                         std::to_string(serial++),
                                     function);
        createdBlocks.push_back(block);
        return block;
    };
    auto *guard = makeBlock("guard");
    auto *fastEntry = makeBlock("fast.entry");
    auto *fallbackEntry = makeBlock("fallback.entry");
    auto *waveHeader = makeBlock("wave.h");
    auto *waveBody = makeBlock("wave.body");
    auto *laneHeader = makeBlock("lane.h");
    auto *laneBody = makeBlock("lane.body");
    BasicBlock *innerHeader =
        plan.nest.size() == 3 ? makeBlock("inner.h") : nullptr;
    auto *cell = makeBlock("cell");
    BasicBlock *innerLatch =
        plan.nest.size() == 3 ? makeBlock("inner.latch") : nullptr;
    auto *laneLatch = makeBlock("lane.latch");
    auto *waveLatch = makeBlock("wave.latch");
    auto *done = makeBlock("done");
    laneHeader->setSemFlag(SemFlag::WavefrontCoincident);

    auto *zero = new ConstantInt(module->int32_ty_, 0);
    auto *one = new ConstantInt(module->int32_ty_, 1);
    auto *sixteen = new ConstantInt(module->int32_ty_, 16);

    auto *extent = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                  plan.bound, plan.start, guard);
    auto *nonEmpty = new ICmpInst(ICmpInst::ICMP_SGT, plan.bound, plan.start,
                                  guard);
    auto *largeEnough = new ICmpInst(ICmpInst::ICMP_SGE, extent, sixteen,
                                     guard);
    long long startValue = static_cast<ConstantInt *>(plan.start)->value_;
    long long maxExtent =
        (static_cast<long long>(std::numeric_limits<int>::max()) - 1) /
            weightSum +
        1;
    long long safeBound = std::min<long long>(
        std::numeric_limits<int>::max(), startValue + maxExtent);
    auto *boundSafe = new ICmpInst(
        ICmpInst::ICMP_SLE, plan.bound,
        new ConstantInt(module->int32_ty_, safeBound), guard);
    auto *validSize = new BinaryInst(module->int1_ty_, Instruction::And,
                                     nonEmpty, largeEnough, guard);
    auto *fastPath = new BinaryInst(module->int1_ty_, Instruction::And,
                                    validSize, boundSafe, guard);
    new BranchInst(fastPath, fastEntry, fallbackEntry, guard);
    new BranchInst(waveHeader, fastEntry);
    new BranchInst(outer->header, fallbackEntry);

    auto *extentMinusOne = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                          extent, one, waveHeader);
    auto *scaledExtent = new BinaryInst(
        module->int32_ty_, Instruction::Mul, extentMinusOne,
        new ConstantInt(module->int32_ty_, weightSum), waveHeader);
    auto *waveBound = new BinaryInst(module->int32_ty_, Instruction::Add,
                                     scaledExtent, one, waveHeader);
    auto *wave = PhiInst::create_phi(module->int32_ty_, waveHeader);
    wave->add_phi_pair_operand(zero, fastEntry);
    waveHeader->add_instruction_front(wave);
    auto *waveCmp = new ICmpInst(ICmpInst::ICMP_SLT, wave, waveBound,
                                 waveHeader);
    new BranchInst(waveCmp, waveBody, done, waveHeader);

    long long tailWeight = weightSum - plan.weights[0];
    auto *tailSpan = new BinaryInst(
        module->int32_ty_, Instruction::Mul, extentMinusOne,
        new ConstantInt(module->int32_ty_, tailWeight), waveBody);
    auto *laneLowerNumerator = new BinaryInst(
        module->int32_ty_, Instruction::Sub, wave, tailSpan, waveBody);
    Value *laneLower = emitCeilDiv(laneLowerNumerator, plan.weights[0],
                                   module, waveBody);
    laneLower = emitSelectMax(laneLower, zero, waveBody);
    Value *laneUpper = emitFloorDiv(wave, plan.weights[0], module, waveBody);
    laneUpper = new BinaryInst(module->int32_ty_, Instruction::Add, laneUpper,
                               one, waveBody);
    laneUpper = emitSelectMin(laneUpper, extent, waveBody);
    new BranchInst(laneHeader, waveBody);

    auto *lane = PhiInst::create_phi(module->int32_ty_, laneHeader);
    lane->add_phi_pair_operand(laneLower, waveBody);
    laneHeader->add_instruction_front(lane);
    auto *laneCmp = new ICmpInst(ICmpInst::ICMP_SLT, lane, laneUpper,
                                 laneHeader);
    new BranchInst(laneCmp, laneBody, waveLatch, laneHeader);
    auto *weightedLane = new BinaryInst(
        module->int32_ty_, Instruction::Mul, lane,
        new ConstantInt(module->int32_ty_, plan.weights[0]), laneBody);
    auto *remainder = new BinaryInst(module->int32_ty_, Instruction::Sub,
                                     wave, weightedLane, laneBody);

    std::vector<Value *> normalized(plan.nest.size());
    normalized[0] = lane;
    if (plan.nest.size() == 2) {
        normalized[1] = remainder;
        new BranchInst(cell, laneBody);
    } else {
        // 波次调度只重排外层两个坐标；第三维的同波次依赖继续由原串行内层循环
        // 保序，同时保留该维度的单位步长内存访问。
        normalized[1] = remainder;
        new BranchInst(innerHeader, laneBody);

        auto *innerIV = PhiInst::create_phi(module->int32_ty_, innerHeader);
        innerIV->add_phi_pair_operand(zero, laneBody);
        innerHeader->add_instruction_front(innerIV);
        auto *innerCmp = new ICmpInst(ICmpInst::ICMP_SLT, innerIV, extent,
                                      innerHeader);
        normalized[2] = innerIV;
        new BranchInst(innerCmp, cell, laneLatch, innerHeader);

        auto *innerNext = new BinaryInst(module->int32_ty_, Instruction::Add,
                                         innerIV, one, innerLatch);
        innerIV->add_phi_pair_operand(innerNext, innerLatch);
        new BranchInst(innerHeader, innerLatch);
    }

    std::set<BasicBlock *> nestBlocks(outer->blocks.begin(),
                                      outer->blocks.end());
    std::unordered_set<BasicBlock *> cloneBlocks(outer->blocks.begin(),
                                                  outer->blocks.end());
    std::unordered_map<Value *, Value *> valueMap;
    for (size_t dimension = 0; dimension < plan.controls.size(); ++dimension) {
        Value *coordinate = new BinaryInst(module->int32_ty_, Instruction::Add,
                                            plan.start, normalized[dimension],
                                            cell);
        valueMap[plan.controls[dimension]->phi] = coordinate;
        valueMap[plan.controls[dimension]->update] =
            new BinaryInst(module->int32_ty_, Instruction::Add, coordinate,
                           one, cell);
    }
    for (PhiInst *phi : pointerPhis) {
        Value *startPointer = rematerialize(pointerStarts[phi], cell, valueMap,
                                            nestBlocks);
        if (!startPointer) return fail("cannot rematerialize pointer start");
        valueMap[phi] = new GetElementPtrInst(
            startPointer, {normalized.back()}, cell);
    }

    std::unordered_set<Instruction *> pointerUpdates;
    for (PhiInst *phi : pointerPhis)
        pointerUpdates.insert(static_cast<Instruction *>(
            phiIncomingFrom(phi, plan.cell)));
    for (Instruction *instruction : plan.cell->instr_list_) {
        if (instruction->is_phi() || instruction->isTerminator() ||
            instruction == plan.controls.back()->update ||
            instruction == plan.controls.back()->compare ||
            pointerUpdates.count(instruction))
            continue;
        for (unsigned i = 0; i < instruction->num_ops(); ++i) {
            Value *operand = instruction->get_operand(i);
            if (dynamic_cast<BasicBlock *>(operand) || valueMap.count(operand))
                continue;
            Value *mapped = rematerialize(operand, cell, valueMap, nestBlocks);
            if (!mapped)
                return fail("cannot rematerialize operand of " +
                            instruction->name_);
            valueMap[operand] = mapped;
        }
        if (!loop_clone::cloneInstruction(instruction, cell, valueMap,
                                          cloneBlocks))
            return fail("cannot clone cell instruction " +
                        instruction->name_);
    }
    new BranchInst(plan.nest.size() == 3 ? innerLatch : laneLatch, cell);

    auto *laneNext = new BinaryInst(module->int32_ty_, Instruction::Add, lane,
                                    one, laneLatch);
    lane->add_phi_pair_operand(laneNext, laneLatch);
    new BranchInst(laneHeader, laneLatch);
    auto *waveNext = new BinaryInst(module->int32_ty_, Instruction::Add, wave,
                                    one, waveLatch);
    wave->add_phi_pair_operand(waveNext, waveLatch);
    new BranchInst(waveHeader, waveLatch);

    std::unordered_map<Value *, Value *> finalCache;
    std::set<Value *> visiting;
    std::vector<std::pair<PhiInst *, Value *>> exitIncoming;
    for (Instruction *instruction : plan.exit->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi) break;
        Value *incoming = nullptr;
        for (unsigned i = 0; i + 1 < phi->num_ops(); i += 2) {
            auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
            if (!pred || !outer->blocks.count(pred)) continue;
            if (incoming && incoming != phi->get_operand(i))
                return fail("exit phi has differing loop incoming values");
            incoming = phi->get_operand(i);
        }
        if (!incoming) continue;
        Value *mapped = materializeFinalValue(incoming, plan, done,
                                              finalCache, visiting);
        if (!mapped) return fail("cannot materialize exit phi incoming");
        exitIncoming.push_back({phi, mapped});
    }
    std::vector<std::pair<Instruction *, Value *>> liveOutMappings;
    for (Instruction *liveOut : liveOuts) {
        bool exportedByLCSSA = false;
        for (const Use &use : liveOut->use_list_) {
            auto *phi = dynamic_cast<PhiInst *>(use.user_);
            if (!phi || phi->parent_ != plan.exit || use.operand_index_ + 1 >=
                                                       phi->num_ops())
                continue;
            auto *pred = dynamic_cast<BasicBlock *>(
                phi->get_operand(use.operand_index_ + 1));
            if (pred && outer->blocks.count(pred)) {
                exportedByLCSSA = true;
                break;
            }
        }
        if (exportedByLCSSA)
            continue;
        Value *mapped = materializeFinalValue(liveOut, plan, done,
                                              finalCache, visiting);
        if (!mapped)
            return fail("cannot materialize live-out " + liveOut->name_);
        liveOutMappings.push_back({liveOut, mapped});
    }
    for (auto [phi, mapped] : exitIncoming)
        phi->add_phi_pair_operand(mapped, done);
    for (auto [liveOut, mapped] : liveOutMappings) {
        std::vector<Use> uses(liveOut->use_list_.begin(),
                              liveOut->use_list_.end());
        auto *exportPhi = PhiInst::create_phi(liveOut->type_, plan.exit);
        for (BasicBlock *pred : plan.exit->pre_bbs_)
            exportPhi->add_phi_pair_operand(liveOut, pred);
        exportPhi->add_phi_pair_operand(mapped, done);
        plan.exit->add_instruction_front(exportPhi);
        for (const Use &use : uses) {
            auto *user = use.user_;
            if (user && user->parent_ &&
                !outer->blocks.count(user->parent_))
                user->set_operand(use.operand_index_, exportPhi);
        }
    }
    new BranchInst(plan.exit, done);

    for (Instruction *instruction : outer->header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi) break;
        for (unsigned i = 1; i < phi->num_ops(); i += 2)
            if (phi->get_operand(i) == oldPreheader)
                phi->set_operand(i, fallbackEntry);
    }
    for (unsigned i = 0; i < oldPreTerm->num_ops(); ++i)
        if (oldPreTerm->get_operand(i) == outer->header)
            oldPreTerm->set_operand(i, guard);
    oldPreheader->remove_succ_basic_block(outer->header);
    outer->header->remove_pre_basic_block(oldPreheader);
    oldPreheader->add_succ_basic_block(guard);
    guard->add_pre_basic_block(oldPreheader);

    function->set_instr_name();
    if (debugEnabled()) {
        std::cerr << "[LoopSkewing] rectangular wavefront func="
                  << function->name_ << " depth=" << plan.nest.size()
                  << " weights=";
        for (size_t i = 0; i < plan.weights.size(); ++i)
            std::cerr << (i ? "," : "") << plan.weights[i];
        std::cerr << "\n";
    }
    newLoopHeaders.push_back(waveHeader);
    newLoopHeaders.push_back(laneHeader);
    if (innerHeader) newLoopHeaders.push_back(innerHeader);
    return true;
}

} // namespace

/**
 * @brief 在单个非声明函数上运行本变换。
 * @param function 待分析或改写的函数。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool LoopSkewing::runOnFunction(Function *function, AnalysisManager *AM) {
    // 优先尝试矩形波前计划，失败后再尝试普通双层倾斜。
    // 任一计划提交后立即刷新循环/别名分析，只重新访问受影响邻域。
    if (std::getenv("DISABLE_LOOP_SKEWING")) return false;
    BasicAliasAnalysis localBasicAA;
    BasicAliasAnalysis *basicAA = nullptr;
    if (AM) {
        basicAA = &AM->getBasicAA(function->parent_);
    } else {
        localBasicAA.analyze(function->parent_);
        basicAA = &localBasicAA;
    }

    LoopInfo localLoopInfo;
    LoopInfo *loopInfo = nullptr;
    if (AM)
        loopInfo = &AM->getLoopInfo(function);
    else {
        localLoopInfo.analyze(function);
        loopInfo = &localLoopInfo;
    }

    AffectedLoopWorklist rectangularWorklist;
    AffectedLoopWorklist skewWorklist;
    rectangularWorklist.seed(*loopInfo);
    skewWorklist.seed(*loopInfo);

    bool changed = false;
    while (true) {
        bool applied = false;
        std::vector<BasicBlock *> affectedHeaders;
        ArgumentAliasAnalysis argumentAlias;
        argumentAlias.analyze(function->parent_);

        while (Loop *loop = rectangularWorklist.take(*loopInfo)) {
            if (loop->parent) continue;
            std::string reason;
            auto rectangular = analyzeRectangularWavefront(
                *loop, *loopInfo, argumentAlias, *basicAA, reason);
            if (debugEnabled()) {
                if (rectangular) {
                    std::cerr << "[LoopSkewing] rectangular candidate func="
                              << function->name_ << " header="
                              << loop->header->name_ << " weights=";
                    for (size_t i = 0; i < rectangular->weights.size(); ++i)
                        std::cerr << (i ? "," : "")
                                  << rectangular->weights[i];
                    std::cerr << "\n";
                } else {
                    reject(*loop, reason, nullptr);
                }
            }
            std::vector<BasicBlock *> newLoopHeaders;
            if (rectangular && applyRectangularWavefront(
                                   *rectangular, function,
                                   function->parent_, newLoopHeaders)) {
                applied = true;
                changed = true;
                affectedHeaders.push_back(loop->header);
                affectedHeaders.insert(affectedHeaders.end(),
                                       newLoopHeaders.begin(),
                                       newLoopHeaders.end());
                break;
            }
        }

        if (!applied) {
            while (Loop *loop = skewWorklist.take(*loopInfo)) {
                std::string reason;
                auto plan = analyzeLoopSkew(*loop, &reason);
                if (!plan) continue;
                BasicBlock *header = loop->header;
                if (applyLoopSkew(*plan, function->parent_)) {
                    applied = true;
                    changed = true;
                    affectedHeaders.push_back(header);
                    break;
                }
            }
        }
        if (!applied) break;

        if (AM) {
            AM->clear(function);
            loopInfo = &AM->getLoopInfo(function);
        } else {
            localLoopInfo.analyze(function);
            loopInfo = &localLoopInfo;
        }
        for (BasicBlock *header : affectedHeaders)
            skewWorklist.addNeighborhood(*loopInfo, header);
    }
    return changed;
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @return 无返回值；所需结果通过 IR 原地修改或输出参数给出。
 */
void LoopSkewing::execute(Module *module) {
    for (auto *function : module->function_list_)
        if (!function->is_declaration()) runOnFunction(function, nullptr);
}

/**
 * @brief 执行当前优化 Pass，并按需更新或失效分析结果。
 * @param module 待处理的 IR 模块，函数可能原地修改其内容。
 * @param AM 分析管理器，用于获取并维护本次变换依赖的分析结果。
 * @return 返回本次运行后仍然有效的分析集合。
 */
PreservedAnalyses LoopSkewing::execute(Module *module, AnalysisManager &AM) {
    bool changed = false;
    for (auto *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function, &AM);
    return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
