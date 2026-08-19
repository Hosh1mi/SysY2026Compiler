// ModuloRecurrenceAnalysis 识别 state = normalize(state + delta(i), mod) 形式的循环状态。
// 它追踪 PHI 回边、验证正模语义和中间值范围，并确保每轮贡献可以安全合并，为 LoopRepFold
// 使用闭式或 runtime 批量计算迭代结果提供合法性摘要。
#include "../../include/mid/analysis/moduloRecurrenceAnalysis.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace ModuloRecurrenceAnalysis {
namespace {

// checkedAdd：检查整数运算或结果是否可表示；溢出时返回 false，不产生截断结果。
bool checkedAdd(long long lhs, long long rhs, long long &result) {
    __int128 wide = static_cast<__int128>(lhs) + rhs;
    if (wide < std::numeric_limits<int>::min() ||
        wide > std::numeric_limits<int>::max())
        return false;
    result = static_cast<long long>(wide);
    return true;
}

// checkedSub：检查整数运算或结果是否可表示；溢出时返回 false，不产生截断结果。
bool checkedSub(long long lhs, long long rhs, long long &result) {
    __int128 wide = static_cast<__int128>(lhs) - rhs;
    if (wide < std::numeric_limits<int>::min() ||
        wide > std::numeric_limits<int>::max())
        return false;
    result = static_cast<long long>(wide);
    return true;
}

// checkedMul：检查整数运算或结果是否可表示；溢出时返回 false，不产生截断结果。
bool checkedMul(long long lhs, long long rhs, long long &result) {
    __int128 wide = static_cast<__int128>(lhs) * rhs;
    if (wide < std::numeric_limits<int>::min() ||
        wide > std::numeric_limits<int>::max())
        return false;
    result = static_cast<long long>(wide);
    return true;
}

// dependsOnImpl：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool dependsOnImpl(Value *value, Value *target,
                   std::set<Value *> &visiting, unsigned depth) {
    if (value == target)
        return true;
    if (!value)
        return false;
    // This predicate is used to prove that a contribution is independent of
    // a loop-carried state.  Exhausting the search budget is "unknown", not
    // proof of independence, so reject the optimization conservatively.
    if (depth > 24)
        return true;
    // A backedge to a value already on the current DFS path adds no new path
    // to the target.  The first visit still examines all of that value's
    // operands, including any real dependency on the target.
    if (!visiting.insert(value).second)
        return false;
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction) {
        visiting.erase(value);
        return false;
    }
    for (unsigned i = 0; i < instruction->num_ops(); ++i) {
        Value *operand = instruction->get_operand(i);
        if (dynamic_cast<BasicBlock *>(operand) ||
            dynamic_cast<Function *>(operand))
            continue;
        if (dependsOnImpl(operand, target, visiting, depth + 1)) {
            visiting.erase(value);
            return true;
        }
    }
    visiting.erase(value);
    return false;
}

// inferBoundsImpl：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool inferBoundsImpl(Value *value, long long &lower, long long &upper,
                     std::set<Value *> &visiting, unsigned depth) {
    if (!value || depth > 16 || !visiting.insert(value).second)
        return false;
    auto finish = [&](bool result) {
        visiting.erase(value);
        return result;
    };

    if (auto *constant = dynamic_cast<ConstantInt *>(value)) {
        lower = upper = constant->value_;
        return finish(true);
    }

    auto *instruction = dynamic_cast<Instruction *>(value);
    auto *type = value->type_
                     ? dynamic_cast<IntegerType *>(value->type_)
                     : nullptr;
    if (!instruction || !type || type->num_bits_ > 32)
        return finish(false);

    if (auto *phi = dynamic_cast<PhiInst *>(instruction)) {
        if (phi->num_ops() == 0 || phi->num_ops() % 2 != 0)
            return finish(false);
        bool firstIncoming = true;
        for (unsigned i = 0; i < phi->num_ops(); i += 2) {
            long long incomingLower = 0, incomingUpper = 0;
            if (!inferBoundsImpl(phi->get_operand(i), incomingLower,
                                 incomingUpper, visiting, depth + 1))
                return finish(false);
            if (firstIncoming) {
                lower = incomingLower;
                upper = incomingUpper;
                firstIncoming = false;
            } else {
                lower = std::min(lower, incomingLower);
                upper = std::max(upper, incomingUpper);
            }
        }
        return finish(!firstIncoming);
    }

    if (auto *zext = dynamic_cast<ZextInst *>(instruction)) {
        auto *sourceType = dynamic_cast<IntegerType *>(
            zext->get_operand(0)->type_);
        if (!sourceType || sourceType->num_bits_ == 0 ||
            sourceType->num_bits_ >= 31)
            return finish(false);
        lower = 0;
        upper = (1LL << sourceType->num_bits_) - 1;
        return finish(true);
    }

    if (instruction->op_id_ == Instruction::SRem) {
        auto *modulus = dynamic_cast<ConstantInt *>(
            instruction->get_operand(1));
        if (!modulus || modulus->value_ <= 0)
            return finish(false);
        lower = -static_cast<long long>(modulus->value_) + 1;
        upper = static_cast<long long>(modulus->value_) - 1;
        return finish(true);
    }

    if (instruction->op_id_ == Instruction::And) {
        auto *lhsMask = dynamic_cast<ConstantInt *>(
            instruction->get_operand(0));
        auto *rhsMask = dynamic_cast<ConstantInt *>(
            instruction->get_operand(1));
        auto *mask = rhsMask ? rhsMask : lhsMask;
        if (!mask || mask->value_ < 0)
            return finish(false);
        lower = 0;
        upper = mask->value_;
        return finish(true);
    }

    if (instruction->op_id_ == Instruction::Add ||
        instruction->op_id_ == Instruction::Sub) {
        long long lhsLower = 0, lhsUpper = 0;
        long long rhsLower = 0, rhsUpper = 0;
        if (!inferBoundsImpl(instruction->get_operand(0), lhsLower, lhsUpper,
                             visiting, depth + 1) ||
            !inferBoundsImpl(instruction->get_operand(1), rhsLower, rhsUpper,
                             visiting, depth + 1))
            return finish(false);
        bool ok = instruction->op_id_ == Instruction::Add
                      ? checkedAdd(lhsLower, rhsLower, lower) &&
                            checkedAdd(lhsUpper, rhsUpper, upper)
                      : checkedSub(lhsLower, rhsUpper, lower) &&
                            checkedSub(lhsUpper, rhsLower, upper);
        return finish(ok);
    }

    if (instruction->op_id_ == Instruction::Mul) {
        auto *lhsConstant = dynamic_cast<ConstantInt *>(
            instruction->get_operand(0));
        auto *rhsConstant = dynamic_cast<ConstantInt *>(
            instruction->get_operand(1));
        ConstantInt *factor = rhsConstant ? rhsConstant : lhsConstant;
        Value *other = rhsConstant ? instruction->get_operand(0)
                                   : instruction->get_operand(1);
        if (!factor)
            return finish(false);
        long long otherLower = 0, otherUpper = 0;
        if (!inferBoundsImpl(other, otherLower, otherUpper, visiting,
                             depth + 1))
            return finish(false);
        long long first = 0, second = 0;
        if (!checkedMul(otherLower, factor->value_, first) ||
            !checkedMul(otherUpper, factor->value_, second))
            return finish(false);
        lower = std::min(first, second);
        upper = std::max(first, second);
        return finish(true);
    }

    if (instruction->op_id_ == Instruction::Shl) {
        auto *shift = dynamic_cast<ConstantInt *>(instruction->get_operand(1));
        if (!shift || shift->value_ < 0 || shift->value_ >= 31)
            return finish(false);
        long long operandLower = 0, operandUpper = 0;
        if (!inferBoundsImpl(instruction->get_operand(0), operandLower,
                             operandUpper, visiting, depth + 1))
            return finish(false);
        long long scale = 1LL << shift->value_;
        if (!checkedMul(operandLower, scale, lower) ||
            !checkedMul(operandUpper, scale, upper))
            return finish(false);
        return finish(true);
    }

    if (instruction->op_id_ == Instruction::SDiv) {
        auto *divisor = dynamic_cast<ConstantInt *>(
            instruction->get_operand(1));
        if (!divisor || divisor->value_ <= 0)
            return finish(false);
        long long dividendLower = 0, dividendUpper = 0;
        if (!inferBoundsImpl(instruction->get_operand(0), dividendLower,
                             dividendUpper, visiting, depth + 1))
            return finish(false);
        lower = dividendLower / divisor->value_;
        upper = dividendUpper / divisor->value_;
        return finish(true);
    }

    if (auto *select = dynamic_cast<SelectInst *>(instruction)) {
        long long trueLower = 0, trueUpper = 0;
        long long falseLower = 0, falseUpper = 0;
        if (!inferBoundsImpl(select->get_operand(1), trueLower, trueUpper,
                             visiting, depth + 1) ||
            !inferBoundsImpl(select->get_operand(2), falseLower, falseUpper,
                             visiting, depth + 1))
            return finish(false);
        lower = std::min(trueLower, falseLower);
        upper = std::max(trueUpper, falseUpper);
        return finish(true);
    }

    return finish(false);
}

} // namespace

// dependsOn：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool dependsOn(Value *value, Value *target) {
    std::set<Value *> visiting;
    return dependsOnImpl(value, target, visiting, 0);
}

// inferBounds：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool inferBounds(Value *value, Bounds &bounds) {
    std::set<Value *> visiting;
    return inferBoundsImpl(value, bounds.lower, bounds.upper, visiting, 0);
}

// analyze：清空旧结果后遍历当前分析单元，建立后续查询所需的完整摘要。
bool analyze(PhiInst *state, BinaryInst *remainder,
             const std::set<BasicBlock *> &updateBlocks,
             Recurrence &result) {
    if (!state || !remainder ||
        remainder->op_id_ != Instruction::SRem ||
        !updateBlocks.count(remainder->parent_))
        return false;
    auto *modulus = dynamic_cast<ConstantInt *>(remainder->get_operand(1));
    if (!modulus || modulus->value_ <= 0)
        return false;

    Recurrence candidate;
    candidate.state = state;
    candidate.remainder = remainder;
    candidate.modulus = modulus;
    candidate.updateChain.insert(remainder);
    int stateCoefficient = 0;

    std::function<bool(Value *, int)> collect = [&](Value *value, int sign) {
        if (value == state) {
            stateCoefficient += sign;
            return true;
        }
        auto *binary = dynamic_cast<BinaryInst *>(value);
        if (binary && updateBlocks.count(binary->parent_) &&
            (binary->op_id_ == Instruction::Add ||
             binary->op_id_ == Instruction::Sub)) {
            candidate.updateChain.insert(binary);
            if (!collect(binary->get_operand(0), sign))
                return false;
            int rhsSign = binary->op_id_ == Instruction::Sub ? -sign : sign;
            return collect(binary->get_operand(1), rhsSign);
        }
        if (dependsOn(value, state))
            return false;
        candidate.contributionTerms.push_back({value, sign});
        return true;
    };

    if (!collect(remainder->get_operand(0), 1) ||
        stateCoefficient != 1 || candidate.contributionTerms.empty())
        return false;
    result = std::move(candidate);
    return true;
}

// hasPrivateUpdateChain：逐项检查该性质所需条件；任何未知结构都按不能证明处理。
bool hasPrivateUpdateChain(const Recurrence &recurrence,
                           const std::set<BasicBlock *> &updateBlocks,
                           bool allowExternalUses) {
    for (Instruction *chainInstruction : recurrence.updateChain) {
        for (const auto &use : chainInstruction->use_list_) {
            auto *user = use.user_;
            if (recurrence.updateChain.count(user))
                continue;
            if (chainInstruction == recurrence.remainder &&
                user == recurrence.state)
                continue;
            if (allowExternalUses && user &&
                !updateBlocks.count(user->parent_))
                continue;
            return false;
        }
    }
    return true;
}

// inferContributionBounds：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool inferContributionBounds(Recurrence &recurrence,
                             const std::vector<PhiInst *> &loopStates,
                             PhiInst *inductionState) {
    Bounds bounds;
    for (SignedTerm &term : recurrence.contributionTerms) {
        for (PhiInst *otherState : loopStates) {
            if (otherState != inductionState &&
                dependsOn(term.value, otherState))
                return false;
        }
        if (!inferBounds(term.value, term.bounds))
            return false;
        term.hasBounds = true;
        long long nextLower = 0, nextUpper = 0;
        bool ok = term.sign > 0
                      ? checkedAdd(bounds.lower, term.bounds.lower, nextLower) &&
                            checkedAdd(bounds.upper, term.bounds.upper, nextUpper)
                      : checkedSub(bounds.lower, term.bounds.upper, nextLower) &&
                            checkedSub(bounds.upper, term.bounds.lower, nextUpper);
        if (!ok)
            return false;
        bounds.lower = nextLower;
        bounds.upper = nextUpper;
    }
    recurrence.contributionRange = bounds;
    recurrence.hasContributionRange = true;
    return true;
}

// advanceBounds：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool advanceBounds(Bounds &bounds, const Recurrence &recurrence,
                   unsigned repetitions) {
    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
        for (const SignedTerm &term : recurrence.contributionTerms) {
            if (!term.hasBounds)
                return false;
            long long nextLower = 0, nextUpper = 0;
            bool ok = term.sign > 0
                          ? checkedAdd(bounds.lower, term.bounds.lower,
                                       nextLower) &&
                                checkedAdd(bounds.upper, term.bounds.upper,
                                           nextUpper)
                          : checkedSub(bounds.lower, term.bounds.upper,
                                       nextLower) &&
                                checkedSub(bounds.upper, term.bounds.lower,
                                           nextUpper);
            if (!ok)
                return false;
            bounds.lower = nextLower;
            bounds.upper = nextUpper;
        }
    }
    return true;
}

// proveNoI32UpdateWrap：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool proveNoI32UpdateWrap(Recurrence &recurrence,
                          const std::vector<PhiInst *> &loopStates,
                          PhiInst *inductionState, Value *initial) {
    if (!inferContributionBounds(recurrence, loopStates, inductionState))
        return false;
    Bounds initialBounds;
    if (!inferBounds(initial, initialBounds))
        return false;
    const long long modulus = recurrence.modulus->value_;
    Bounds updateBounds{
        std::min(initialBounds.lower, -modulus + 1),
        std::max(initialBounds.upper, modulus - 1)};
    return advanceBounds(updateBounds, recurrence);
}

// fitsSignedI32：检查整数运算或结果是否可表示；溢出时返回 false，不产生截断结果。
bool fitsSignedI32(long long lower, long long upper) {
    return lower >= std::numeric_limits<int>::min() &&
           upper <= std::numeric_limits<int>::max();
}

// needsAtMostOneCorrection：封装该局部计算，为上层分析或 IR 构造返回所需结果。
bool needsAtMostOneCorrection(long long lower, long long upper,
                              long long modulus) {
    if (modulus <= 0)
        return false;
    return static_cast<__int128>(lower) > -2 * static_cast<__int128>(modulus) &&
           static_cast<__int128>(upper) < 2 * static_cast<__int128>(modulus);
}

} // namespace ModuloRecurrenceAnalysis
