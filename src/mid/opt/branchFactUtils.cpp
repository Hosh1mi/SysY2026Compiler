#include "../../include/mid/opt/branchFactUtils.hpp"
#include "../../include/mid/ir/constant.hpp"

#include <cstdint>

size_t ICmpKeyHash::operator()(const ICmpKey &key) const {
    size_t h = static_cast<size_t>(key.pred);
    h ^= reinterpret_cast<uintptr_t>(key.lhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= reinterpret_cast<uintptr_t>(key.rhs) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

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

ICmpKey makeICmpKey(ICmpInst *icmp) {
    return {icmp->icmp_op_, icmp->get_operand(0), icmp->get_operand(1)};
}

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
