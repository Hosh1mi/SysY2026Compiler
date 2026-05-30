#pragma once
// Internal header shared by EarlyCSE and GVN passes.
// Not part of the public include/ API.

#include "../ir/ir.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

// ---------- pair hash ----------
struct PairHash {
    template<typename T1, typename T2>
    size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>()(p.first);
        auto h2 = std::hash<T2>()(p.second);
        return h1 ^ (h2 << 1);
    }
};

// ---------- canonical constant map ----------
inline std::unordered_map<std::pair<Type*, unsigned long long>, Constant*, PairHash> canonical_constants;

inline Value* get_canonical_constant(Value *v) {
    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
        unsigned long long key = static_cast<unsigned long long>(ci->value_);
        auto &entry = canonical_constants[{ci->type_, key}];
        if (!entry) entry = new ConstantInt(ci->type_, ci->value_);
        return entry;
    } else if (auto *cf = dynamic_cast<ConstantFloat*>(v)) {
        unsigned long long key;
        memcpy(&key, &cf->value_, sizeof(key));
        auto &entry = canonical_constants[{cf->type_, key}];
        if (!entry) entry = new ConstantFloat(cf->type_, cf->value_);
        return entry;
    } else if (dynamic_cast<ConstantZero*>(v)) {
        auto &entry = canonical_constants[{v->type_, 0}];
        if (!entry) entry = new ConstantZero(v->type_);
        return entry;
    }
    return v;
}

// ---------- expression signature ----------
struct ExprSignature {
    Instruction::OpID op_id;
    Type* ty;
    unsigned extra_op;          // ICmp/FCmp comparison kind
    std::vector<Value*> ops;    // operands after value-number substitution

    bool operator==(const ExprSignature &other) const {
        return op_id == other.op_id && extra_op == other.extra_op &&
               ty == other.ty && ops == other.ops;
    }
};

namespace std {
    template<> struct hash<ExprSignature> {
        size_t operator()(const ExprSignature &s) const {
            size_t h = hash<unsigned>()(s.op_id);
            h ^= hash<unsigned>()(s.extra_op) + 0x9e3779b9 + (h<<6) + (h>>2);
            h ^= std::hash<void*>()(s.ty) + 0x9e3779b9 + (h<<6) + (h>>2);
            for (auto *v : s.ops) {
                size_t ph = hash<void*>()(v);
                h ^= ph + 0x9e3779b9 + (h<<6) + (h>>2);
            }
            return h;
        }
    };
}

// ---------- safety predicates ----------
inline bool is_safe_to_eliminate(Instruction *inst) {
    if (inst->is_void()) return false;          // store / br / ret / void call
    if (inst->is_call()) return false;
    if (inst->is_store()) return false;
    if (inst->is_alloca()) return false;
    if (inst->is_phi()) return false;
    return true;  // Load/Binary/Unary/ICmp/FCmp/GetElementPtr/ZExt/FPtoSI/SItoFP/BitCast
}

// ---------- commutativity ----------
inline bool is_commutative(Instruction::OpID op) {
    switch (op) {
        case Instruction::Add:
        case Instruction::Mul:
        case Instruction::FAdd:
        case Instruction::FMul:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
            return true;
        default: return false;
    }
}

inline bool icmp_commutative(ICmpInst::ICmpOp op) {
    return op == ICmpInst::ICMP_EQ || op == ICmpInst::ICMP_NE;
}
inline bool fcmp_commutative(FCmpInst::FCmpOp op) {
    return op == FCmpInst::FCMP_UEQ || op == FCmpInst::FCMP_UNE ||
           op == FCmpInst::FCMP_OEQ || op == FCmpInst::FCMP_ONE;
}

// ---------- signature computation ----------
inline ExprSignature compute_signature(Instruction *inst,
                                       const std::unordered_map<Value*, Value*> &vn_map) {
    ExprSignature sig;
    sig.op_id = inst->op_id_;
    sig.ty = inst->type_;
    sig.extra_op = 0;

    std::vector<Value*> ops;
    ops.reserve(inst->num_ops_);
    for (unsigned i = 0; i < inst->num_ops_; i++) {
        Value *op = inst->get_operand(i);
        auto it = vn_map.find(op);
        Value *rep = (it != vn_map.end()) ? it->second : op;
        rep = get_canonical_constant(rep);
        ops.push_back(rep);
    }

    if (auto *icmp = dynamic_cast<ICmpInst*>(inst)) {
        sig.extra_op = static_cast<unsigned>(icmp->icmp_op_);
        if (icmp_commutative(icmp->icmp_op_)) {
            if (ops[0] > ops[1]) std::swap(ops[0], ops[1]);
        }
    } else if (auto *fcmp = dynamic_cast<FCmpInst*>(inst)) {
        sig.extra_op = static_cast<unsigned>(fcmp->fcmp_op_);
        if (fcmp_commutative(fcmp->fcmp_op_)) {
            if (ops[0] > ops[1]) std::swap(ops[0], ops[1]);
        }
    } else if (is_commutative(inst->op_id_)) {
        if (ops.size() == 2 && ops[0] > ops[1])
            std::swap(ops[0], ops[1]);
    }

    sig.ops = std::move(ops);
    return sig;
}
