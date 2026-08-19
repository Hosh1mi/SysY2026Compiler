#include "../../include/mid/opt/branchFactUtils.hpp"
#include "../../include/mid/ir/constant.hpp"

#include <cstdint>

// 路径事实公共工具：把布尔值、icmp 及其取反形式统一编码，供 CVP 和跳转穿线复用。
// 对事实的查询只在能够证明真值时返回结果，缺少信息时用 nullopt 保持保守性。

// 为“谓词 + 左右操作数”生成稳定哈希；Value 指针代表同一轮 IR 中的 SSA 身份。
size_t ICmpKeyHash::operator()(const ICmpKey &key) const {
    size_t h = static_cast<size_t>(key.pred);
    h ^= reinterpret_cast<uintptr_t>(key.lhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= reinterpret_cast<uintptr_t>(key.rhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

// 识别 `(i1 == 0/1)` 与 `(i1 != 0/1)` 包装，并将已有的内部布尔事实传播到外层比较。
static std::optional<bool> decodeWrappedBool(ICmpInst *icmp,
                                             const BoolFactMap &boolFacts,
                                             const ICmpFactMap &cmpFacts) {
    auto decode = [&](Value *boolValue, int constant,
                      ICmpInst::ICmpOp pred) -> std::optional<bool> {
        if (boolValue->type_->tid_ != Type::IntegerTyID ||
            static_cast<IntegerType *>(boolValue->type_)->num_bits_ != 1)
            return std::nullopt;
        auto known = getKnownBool(boolValue, boolFacts, cmpFacts);
        if (!known.has_value()) return std::nullopt;
        bool eq = (*known == (constant != 0));
        return pred == ICmpInst::ICMP_EQ ? eq : !eq;
    };

    int rhsConst = 0;
    if (getIntegerConstantValue(icmp->get_operand(1), rhsConst))
        return decode(icmp->get_operand(0), rhsConst, icmp->icmp_op_);

    int lhsConst = 0;
    if (getIntegerConstantValue(icmp->get_operand(0), lhsConst))
        return decode(icmp->get_operand(1), lhsConst, icmp->icmp_op_);

    return std::nullopt;
}

// 从整数常量或整数 zeroinitializer 中提取数值，屏蔽两种常量表示的差异。
bool getIntegerConstantValue(Value *value, int &out) {
    if (auto *ci = dynamic_cast<ConstantInt *>(value)) {
        out = ci->value_;
        return true;
    }
    if (auto *cz = dynamic_cast<ConstantZero *>(value)) {
        if (cz->type_->tid_ == Type::IntegerTyID) {
            out = 0;
            return true;
        }
    }
    return false;
}

// 将一条 icmp 转为事实表使用的结构化键。
ICmpKey makeICmpKey(ICmpInst *icmp) {
    return {icmp->icmp_op_, icmp->get_operand(0), icmp->get_operand(1)};
}

// 返回谓词的逻辑补集，用于从 `cmp` 的事实推导 `!cmp`。
ICmpInst::ICmpOp invertICmpPredicate(ICmpInst::ICmpOp pred) {
    switch (pred) {
        case ICmpInst::ICMP_EQ:  return ICmpInst::ICMP_NE;
        case ICmpInst::ICMP_NE:  return ICmpInst::ICMP_EQ;
        case ICmpInst::ICMP_UGT: return ICmpInst::ICMP_ULE;
        case ICmpInst::ICMP_UGE: return ICmpInst::ICMP_ULT;
        case ICmpInst::ICMP_ULT: return ICmpInst::ICMP_UGE;
        case ICmpInst::ICMP_ULE: return ICmpInst::ICMP_UGT;
        case ICmpInst::ICMP_SGT: return ICmpInst::ICMP_SLE;
        case ICmpInst::ICMP_SGE: return ICmpInst::ICMP_SLT;
        case ICmpInst::ICMP_SLT: return ICmpInst::ICMP_SGE;
        case ICmpInst::ICMP_SLE: return ICmpInst::ICMP_SGT;
    }
    return pred;
}

// 查询值的已知真值：依次检查直接事实、常量、精确比较、反向比较和布尔包装。
std::optional<bool> getKnownBool(Value *value, const BoolFactMap &boolFacts,
                                 const ICmpFactMap &cmpFacts) {
    auto factIt = boolFacts.find(value);
    if (factIt != boolFacts.end())
        return factIt->second;

    int constant = 0;
    if (getIntegerConstantValue(value, constant))
        return constant != 0;

    auto *icmp = dynamic_cast<ICmpInst *>(value);
    if (!icmp) return std::nullopt;

    auto exactIt = cmpFacts.find(makeICmpKey(icmp));
    if (exactIt != cmpFacts.end())
        return exactIt->second;

    ICmpKey inverseKey{invertICmpPredicate(icmp->icmp_op_),
                       icmp->get_operand(0), icmp->get_operand(1)};
    auto inverseIt = cmpFacts.find(inverseKey);
    if (inverseIt != cmpFacts.end())
        return !inverseIt->second;

    return decodeWrappedBool(icmp, boolFacts, cmpFacts);
}

// 记录一条路径假设，并递归展开布尔包装，使内外两层条件都能被后续查询命中。
void recordAssumedBool(Value *value, bool assumed, BoolFactMap &boolFacts,
                       ICmpFactMap &cmpFacts) {
    auto it = boolFacts.find(value);
    if (it == boolFacts.end())
        boolFacts.emplace(value, assumed);
    else
        it->second = assumed;

    auto *icmp = dynamic_cast<ICmpInst *>(value);
    if (!icmp) return;

    cmpFacts[makeICmpKey(icmp)] = assumed;

    auto deriveWrapped = [&](Value *boolValue, int constant,
                             ICmpInst::ICmpOp pred) {
        if (boolValue->type_->tid_ != Type::IntegerTyID ||
            static_cast<IntegerType *>(boolValue->type_)->num_bits_ != 1)
            return;

        bool eq = pred == ICmpInst::ICMP_EQ;
        bool boolValueIsTrue = assumed ? eq : !eq;
        if (constant == 0)
            boolValueIsTrue = !boolValueIsTrue;
        recordAssumedBool(boolValue, boolValueIsTrue, boolFacts, cmpFacts);
    };

    int rhsConst = 0;
    if (getIntegerConstantValue(icmp->get_operand(1), rhsConst)) {
        deriveWrapped(icmp->get_operand(0), rhsConst, icmp->icmp_op_);
        return;
    }

    int lhsConst = 0;
    if (getIntegerConstantValue(icmp->get_operand(0), lhsConst))
        deriveWrapped(icmp->get_operand(1), lhsConst, icmp->icmp_op_);
}
