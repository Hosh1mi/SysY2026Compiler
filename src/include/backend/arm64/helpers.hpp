#pragma once
#include "../../mid/ir/ir.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

// ── type predicates ──────────────────────────────────────────────────────────

inline int typeSize(Type *ty) {
    switch (ty->tid_) {
    case Type::IntegerTyID: return 4;
    case Type::FloatTyID:   return 4;
    case Type::PointerTyID: return 8;
    case Type::ArrayTyID:
        return static_cast<ArrayType*>(ty)->num_elements_ *
               typeSize(static_cast<ArrayType*>(ty)->contained_);
    case Type::VectorTyID:
        return static_cast<VectorType*>(ty)->num_elements_ *
               typeSize(static_cast<VectorType*>(ty)->contained_);
    default: return 8;
    }
}

inline bool isFloat(Type *ty)  { return ty->tid_ == Type::FloatTyID; }
inline bool isInt(Type *ty)    { return ty->tid_ == Type::IntegerTyID; }
inline bool isPtr(Type *ty)    { return ty->tid_ == Type::PointerTyID; }
inline bool isVoid(Type *ty)   { return ty->tid_ == Type::VoidTyID; }
inline bool isLabel(Type *ty)  { return ty->tid_ == Type::LabelTyID; }
inline bool isVector(Type *ty) { return ty->tid_ == Type::VectorTyID; }

inline int align16(int n) { return (n + 15) & ~15; }

inline bool isAllocatableIntValue(Type *ty) {
    return ty->tid_ == Type::IntegerTyID;
}

inline bool isAllocatableFloatValue(Type *ty) {
    return ty->tid_ == Type::FloatTyID;
}

inline bool isAllocatablePtrValue(Type *ty) {
    return ty->tid_ == Type::PointerTyID;
}

inline bool isAllocatableNEONValue(Type *ty) {
    return ty->tid_ == Type::VectorTyID;
}

// ── callee-saved register collection ────────────────────────────────────────

inline std::vector<int> collectAssignedIntRegs(const std::map<Value*, std::string> &assignedRegs) {
    std::set<int> regs;
    for (const auto &entry : assignedRegs) {
        const std::string &reg = entry.second;
        if (!reg.empty() && (reg[0] == 'w' || reg[0] == 'x')) {
            int r = std::stoi(reg.substr(1));
            // Only callee-saved registers (r19-r28) need save/restore.
            // Caller-saved regs (r0-r18) including pre-colored args must not
            // be saved — doing so would clobber the return value on restore.
            if (r >= 19) {
                // Skip if the value has zero uses (dead code not eliminated).
                // Arguments are always considered live even with empty use_list.
                Value *v = entry.first;
                if (v->use_list_.empty() && !dynamic_cast<Argument*>(v))
                    continue;
                regs.insert(r);
            }
        }
    }
    return std::vector<int>(regs.begin(), regs.end());
}

inline std::vector<int> collectAssignedFloatRegs(const std::map<Value*, std::string> &assignedRegs) {
    std::set<int> regs;
    for (const auto &entry : assignedRegs) {
        const std::string &reg = entry.second;
        if (!reg.empty() && reg[0] == 's') {
            int r = std::stoi(reg.substr(1));
            // Only callee-saved float registers (s8-s15).
            if (r >= 8 && r <= 15) {
                Value *v = entry.first;
                if (v->use_list_.empty() && !dynamic_cast<Argument*>(v))
                    continue;
                regs.insert(r);
            }
        }
    }
    return std::vector<int>(regs.begin(), regs.end());
}

inline std::vector<int> collectAssignedNEONRegs(const std::map<Value*, std::string> &assignedRegs) {
    std::set<int> regs;
    for (const auto &entry : assignedRegs) {
        const std::string &reg = entry.second;
        if (!reg.empty() && reg[0] == 'v') {
            int r = std::stoi(reg.substr(1));
            // Only callee-saved NEON registers (v8-v15).
            if (r >= 8 && r <= 15) {
                Value *v = entry.first;
                if (v->use_list_.empty() && !dynamic_cast<ConstantVector*>(v))
                    continue;
                regs.insert(r);
            }
        }
    }
    return std::vector<int>(regs.begin(), regs.end());
}

// ── label helpers ────────────────────────────────────────────────────────────

inline std::string bbLabel(Function *f, BasicBlock *bb) {
    return f->name_ + "_" + bb->name_;
}
