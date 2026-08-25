// SummableExpressionAnalysis 将循环每轮贡献拆成线性项、常量乘除、正模和可选 select 分支，
// 只接受 runtime 能按原 i32 语义重放的表达式。生成的摘要由 LoopRepFold 传给模求和 runtime。
#include "../../include/mid/analysis/summableExpressionAnalysis.hpp"

#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace SummableExpressionAnalysis {
namespace {

bool constantI32(Value *value, int &result) {
    auto *constant = dynamic_cast<ConstantInt *>(value);
    if (!constant || constant->value_ < std::numeric_limits<int>::min() ||
        constant->value_ > std::numeric_limits<int>::max())
        return false;
    result = static_cast<int>(constant->value_);
    return true;
}

// checkedAccumulate：检查整数运算或结果是否可表示；溢出时返回 false，不产生截断结果。
bool checkedAccumulate(int &value, std::int64_t delta) {
    std::int64_t next = static_cast<std::int64_t>(value) + delta;
    if (next < std::numeric_limits<int>::min() ||
        next > std::numeric_limits<int>::max())
        return false;
    value = static_cast<int>(next);
    return true;
}

// matchMultiplyByConstant：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchMultiplyByConstant(Value *value, Value *input, int &constant) {
    if (value == input) {
        constant = 1;
        return true;
    }
    auto *multiply = dynamic_cast<BinaryInst *>(value);
    if (!multiply) return false;
    if (multiply->op_id_ == Instruction::Shl &&
        multiply->get_operand(0) == input) {
        int shift = 0;
        if (!constantI32(multiply->get_operand(1), shift) ||
            shift < 0 || shift > 30)
            return false;
        constant = 1 << shift;
        return true;
    }
    if (!multiply->is_mul()) return false;
    if (multiply->get_operand(0) == input)
        return constantI32(multiply->get_operand(1), constant);
    if (multiply->get_operand(1) == input)
        return constantI32(multiply->get_operand(0), constant);
    return false;
}

struct AffineLine {
    int multiplier = 0;
    int constant = 0;
};

// analyzeAffine：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool analyzeAffine(Value *value, Value *symbol, AffineLine &result,
                   std::set<Value *> &visiting, unsigned depth) {
    if (!value || depth > 16 || !visiting.insert(value).second)
        return false;
    auto finish = [&](bool answer) {
        visiting.erase(value);
        return answer;
    };
    if (value == symbol) {
        result = {1, 0};
        return finish(true);
    }
    int constant = 0;
    if (constantI32(value, constant)) {
        result = {0, constant};
        return finish(true);
    }
    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (!binary) return finish(false);
    if (binary->is_add() || binary->is_sub()) {
        AffineLine lhs, rhs;
        if (!analyzeAffine(binary->get_operand(0), symbol, lhs, visiting,
                           depth + 1) ||
            !analyzeAffine(binary->get_operand(1), symbol, rhs, visiting,
                           depth + 1))
            return finish(false);
        const int sign = binary->is_sub() ? -1 : 1;
        std::int64_t multiplier = static_cast<std::int64_t>(lhs.multiplier) +
                                  sign * static_cast<std::int64_t>(rhs.multiplier);
        std::int64_t offset = static_cast<std::int64_t>(lhs.constant) +
                              sign * static_cast<std::int64_t>(rhs.constant);
        if (multiplier < -65535 || multiplier > 65535 ||
            offset < std::numeric_limits<int>::min() ||
            offset > std::numeric_limits<int>::max())
            return finish(false);
        result = {static_cast<int>(multiplier), static_cast<int>(offset)};
        return finish(true);
    }
    if (binary->is_mul()) {
        int factor = 0;
        Value *other = nullptr;
        if (constantI32(binary->get_operand(0), factor))
            other = binary->get_operand(1);
        else if (constantI32(binary->get_operand(1), factor))
            other = binary->get_operand(0);
        else
            return finish(false);
        AffineLine line;
        if (!analyzeAffine(other, symbol, line, visiting, depth + 1))
            return finish(false);
        std::int64_t multiplier =
            static_cast<std::int64_t>(line.multiplier) * factor;
        std::int64_t offset = static_cast<std::int64_t>(line.constant) * factor;
        if (multiplier < -65535 || multiplier > 65535 ||
            offset < std::numeric_limits<int>::min() ||
            offset > std::numeric_limits<int>::max())
            return finish(false);
        result = {static_cast<int>(multiplier), static_cast<int>(offset)};
        return finish(true);
    }
    if (binary->op_id_ == Instruction::Shl) {
        int shift = 0;
        if (!constantI32(binary->get_operand(1), shift) ||
            shift < 0 || shift > 30)
            return finish(false);
        AffineLine line;
        if (!analyzeAffine(binary->get_operand(0), symbol, line, visiting,
                           depth + 1))
            return finish(false);
        const std::int64_t factor = 1LL << shift;
        const std::int64_t multiplier =
            static_cast<std::int64_t>(line.multiplier) * factor;
        const std::int64_t offset =
            static_cast<std::int64_t>(line.constant) * factor;
        if (multiplier < -65535 || multiplier > 65535 ||
            offset < std::numeric_limits<int>::min() ||
            offset > std::numeric_limits<int>::max())
            return finish(false);
        result = {static_cast<int>(multiplier), static_cast<int>(offset)};
        return finish(true);
    }
    return finish(false);
}

// analyzeAffine：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool analyzeAffine(Value *value, Value *symbol, AffineLine &result) {
    std::set<Value *> visiting;
    return analyzeAffine(value, symbol, result, visiting, 0);
}

struct AffineSelectionBasis {
    AffineLine lhs;
    AffineLine rhs;
    bool trueUsesRight = true;
};

// matchReflected：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchReflected(Value *value, Value *base, int &constant) {
    auto *subtract = dynamic_cast<BinaryInst *>(value);
    return subtract && subtract->is_sub() &&
           constantI32(subtract->get_operand(0), constant) &&
           subtract->get_operand(1) == base;
}

// matchRedundantReflectedMaximum：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchRedundantReflectedMaximum(Value *value, PhiInst *induction,
                                    int &reflectionConstant,
                                    long long &lowerBound) {
    if (value == induction) {
        lowerBound = std::numeric_limits<int>::min();
        return true;
    }
    auto *select = dynamic_cast<SelectInst *>(value);
    if (!select) return false;
    auto *compare = dynamic_cast<ICmpInst *>(select->get_operand(0));
    if (!compare || compare->icmp_op_ != ICmpInst::ICMP_SLT)
        return false;
    Value *base = compare->get_operand(0);
    int constant = 0;
    if (!matchReflected(compare->get_operand(1), base, constant) ||
        constant < 0 || select->get_operand(1) != compare->get_operand(1) ||
        select->get_operand(2) != base)
        return false;
    int innerConstant = 0;
    long long innerLower = 0;
    if (!matchRedundantReflectedMaximum(base, induction, innerConstant,
                                        innerLower))
        return false;
    const long long layerLower =
        (static_cast<long long>(constant) + 1) / 2;
    if (base == induction) {
        reflectionConstant = constant;
        lowerBound = layerLower;
        return true;
    }
    if (innerLower < layerLower) return false;
    reflectionConstant = innerConstant;
    lowerBound = innerLower;
    return true;
}

// matchAffineSelectionBasis：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchAffineSelectionBasis(Value *value, PhiInst *induction,
                               AffineSelectionBasis &result) {
    int reflectionConstant = 0;
    long long reflectedLowerBound = 0;
    if (value != induction &&
        matchRedundantReflectedMaximum(value, induction, reflectionConstant,
                                       reflectedLowerBound)) {
        result = {{1, 0}, {-1, reflectionConstant}, true};
        return true;
    }
    auto *select = dynamic_cast<SelectInst *>(value);
    if (!select) return false;
    auto *compare = dynamic_cast<ICmpInst *>(select->get_operand(0));
    if (!compare) return false;
    Value *lhsValue = compare->get_operand(0);
    Value *rhsValue = compare->get_operand(1);
    switch (compare->icmp_op_) {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
        break;
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE:
        std::swap(lhsValue, rhsValue);
        break;
    default:
        return false;
    }
    Value *selectedTrue = select->get_operand(1);
    Value *selectedFalse = select->get_operand(2);
    bool trueUsesRight = selectedTrue == rhsValue && selectedFalse == lhsValue;
    bool trueUsesLeft = selectedTrue == lhsValue && selectedFalse == rhsValue;
    if (!trueUsesRight && !trueUsesLeft) return false;
    AffineLine lhs, rhs;
    if (!analyzeAffine(lhsValue, induction, lhs) ||
        !analyzeAffine(rhsValue, induction, rhs) ||
        lhs.multiplier == rhs.multiplier)
        return false;
    result = {lhs, rhs, trueUsesRight};
    return true;
}

// collectValues：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
void collectValues(Value *value, std::set<Value *> &seen,
                   std::vector<Value *> &values, unsigned depth) {
    if (!value || depth > 20 || !seen.insert(value).second) return;
    values.push_back(value);
    auto *instruction = dynamic_cast<Instruction *>(value);
    if (!instruction) return;
    for (unsigned index = 0; index < instruction->num_ops(); ++index) {
        Value *operand = instruction->get_operand(index);
        if (dynamic_cast<BasicBlock *>(operand) ||
            dynamic_cast<Function *>(operand))
            continue;
        collectValues(operand, seen, values, depth + 1);
    }
}

bool flattenSignedAdd(Value *value, int sign,
                      std::vector<std::pair<Value *, int>> &terms,
                      int &constant) {
    int immediate = 0;
    if (constantI32(value, immediate))
        return checkedAccumulate(constant,
                                 static_cast<std::int64_t>(sign) * immediate);
    auto *binary = dynamic_cast<BinaryInst *>(value);
    if (binary && binary->is_add())
        return flattenSignedAdd(binary->get_operand(0), sign, terms,
                                constant) &&
               flattenSignedAdd(binary->get_operand(1), sign, terms,
                                constant);
    if (binary && binary->is_sub())
        return flattenSignedAdd(binary->get_operand(0), sign, terms,
                                constant) &&
               flattenSignedAdd(binary->get_operand(1), -sign, terms,
                                constant);
    terms.push_back({value, sign});
    return true;
}

// matchFloorTerm：逐层匹配允许的 IR 形状并提取组成部分，结构或类型不符时返回失败。
bool matchFloorTerm(Value *value, Value *basis, int &numeratorMultiplier,
                    int &divisor, int &quotientMultiplier) {
    Value *quotient = value;
    quotientMultiplier = 1;
    auto *outerMultiply = dynamic_cast<BinaryInst *>(value);
    if (outerMultiply && outerMultiply->is_mul()) {
        int multiplier = 0;
        if (constantI32(outerMultiply->get_operand(0), multiplier)) {
            quotient = outerMultiply->get_operand(1);
            quotientMultiplier = multiplier;
        } else if (constantI32(outerMultiply->get_operand(1), multiplier)) {
            quotient = outerMultiply->get_operand(0);
            quotientMultiplier = multiplier;
        }
    }
    auto *division = dynamic_cast<BinaryInst *>(quotient);
    if (!division || !division->is_div() ||
        !constantI32(division->get_operand(1), divisor) || divisor <= 0)
        return false;
    return matchMultiplyByConstant(division->get_operand(0), basis,
                                   numeratorMultiplier);
}

// analyzeWithBasis：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool analyzeWithBasis(Value *dividend, Value *basis, PhiInst *induction,
                      const AffineSelectionBasis *selection,
                      int modulus, LinearFloorExpression &result) {
    std::vector<std::pair<Value *, int>> terms;
    int constant = 0;
    if (!flattenSignedAdd(dividend, 1, terms, constant)) return false;

    LinearFloorExpression candidate;
    candidate.induction = induction;
    if (selection) {
        candidate.hasAffineSelection = true;
        candidate.lhsMultiplier = selection->lhs.multiplier;
        candidate.lhsConstant = selection->lhs.constant;
        candidate.rhsMultiplier = selection->rhs.multiplier;
        candidate.rhsConstant = selection->rhs.constant;
        candidate.trueUsesRight = selection->trueUsesRight;
    }
    candidate.constant = constant;
    candidate.modulus = modulus;
    bool sawExpression = false;
    bool sawFloor = false;
    for (const auto &[term, sign] : terms) {
        int multiplier = 0;
        if (matchMultiplyByConstant(term, basis, multiplier)) {
            if (!checkedAccumulate(candidate.linearMultiplier,
                                   static_cast<std::int64_t>(sign) *
                                       multiplier))
                return false;
            sawExpression = true;
            continue;
        }
        int numeratorMultiplier = 0;
        int divisor = 0;
        int quotientMultiplier = 0;
        if (!sawFloor &&
            matchFloorTerm(term, basis, numeratorMultiplier, divisor,
                           quotientMultiplier)) {
            const std::int64_t signedQuotientMultiplier =
                static_cast<std::int64_t>(sign) * quotientMultiplier;
            if (numeratorMultiplier <= 0 ||
                signedQuotientMultiplier <= 0 ||
                numeratorMultiplier > 65535 || divisor > 4096 ||
                signedQuotientMultiplier > 65535)
                return false;
            candidate.divisionMultiplier = numeratorMultiplier;
            candidate.divisor = divisor;
            candidate.quotientMultiplier =
                static_cast<int>(signedQuotientMultiplier);
            sawExpression = true;
            sawFloor = true;
            continue;
        }
        return false;
    }
    if (!sawExpression || candidate.linearMultiplier < 0)
        return false;
    result = candidate;
    return true;
}

} // namespace

// analyzeModular：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
bool analyzeModular(Value *value, PhiInst *induction,
                    LinearFloorExpression &result) {
    auto *remainder = dynamic_cast<BinaryInst *>(value);
    int modulus = 0;
    if (!induction || !remainder || !remainder->is_rem() ||
        !constantI32(remainder->get_operand(1), modulus) || modulus <= 0)
        return false;

    Value *dividend = remainder->get_operand(0);
    std::set<Value *> seen;
    std::vector<Value *> values;
    collectValues(dividend, seen, values, 0);

    // The direct induction basis covers affine/floor expressions without any
    // min/max.  Reflected candidates are tried afterwards and therefore only
    // selected when the direct form cannot represent the expression.
    if (analyzeWithBasis(dividend, induction, induction, nullptr, modulus,
                         result))
        return true;
    for (Value *candidateBasis : values) {
        AffineSelectionBasis selection;
        if (candidateBasis == induction ||
            !matchAffineSelectionBasis(candidateBasis, induction, selection))
            continue;
        if (analyzeWithBasis(dividend, candidateBasis, induction, &selection,
                             modulus, result))
            return true;
    }
    return false;
}

} // namespace SummableExpressionAnalysis
