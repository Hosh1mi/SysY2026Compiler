#pragma once

#include "../ir/instruction.hpp"

#include <optional>
#include <unordered_map>

struct ICmpKey {
    ICmpInst::ICmpOp pred;
    Value *lhs;
    Value *rhs;

    bool operator==(const ICmpKey &other) const {
        return pred == other.pred && lhs == other.lhs && rhs == other.rhs;
    }
};

struct ICmpKeyHash {
    size_t operator()(const ICmpKey &key) const;
};

using BoolFactMap = std::unordered_map<Value *, bool>;
using ICmpFactMap = std::unordered_map<ICmpKey, bool, ICmpKeyHash>;

bool getIntegerConstantValue(Value *value, int &out);
std::optional<bool> getKnownBool(Value *value, const BoolFactMap &boolFacts,
                                 const ICmpFactMap &cmpFacts);
void recordAssumedBool(Value *value, bool assumed, BoolFactMap &boolFacts,
                       ICmpFactMap &cmpFacts);
ICmpKey makeICmpKey(ICmpInst *icmp);
ICmpInst::ICmpOp invertICmpPredicate(ICmpInst::ICmpOp pred);
