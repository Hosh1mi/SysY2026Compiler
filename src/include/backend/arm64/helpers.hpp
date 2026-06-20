#pragma once
#include "../../mid/ir/ir.hpp"
#include "../../mid/analysis/valueFacts.hpp"
#include <cstdint>
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

// ── ARM64 immediate / addressing encoding helpers ────────────────────────────
// Shared by instruction selection and the constant-strength-reduction module.

// Pick the index-extension form for a GEP/address index: an index proven
// non-negative can be zero-extended (uxtw), otherwise sign-extend (sxtw).
inline const char *indexExtendOpcode(Value *idx, BasicBlock *ctx) {
    return ValueFacts::isKnownNonNegative(idx, ctx) ? "uxtw" : "sxtw";
}

inline uint32_t maskForWidth(unsigned width) {
    return width >= 32 ? UINT32_MAX : ((1u << width) - 1u);
}

inline uint32_t rotateRightWithin(uint32_t value, unsigned rot, unsigned width) {
    uint32_t mask = maskForWidth(width);
    value &= mask;
    rot %= width;
    if (rot == 0) return value;
    return ((value >> rot) | (value << (width - rot))) & mask;
}

inline uint32_t replicatePattern(uint32_t pattern, unsigned width) {
    uint32_t result = 0;
    for (unsigned pos = 0; pos < 32; pos += width)
        result |= pattern << pos;
    return result;
}

// True when `imm` is encodable as an AArch64 32-bit logical (and/orr/eor)
// immediate — i.e. a rotated, replicated run of ones over a 2/4/8/16/32-bit
// element (all-zeros and all-ones are excluded, they are not encodable).
inline bool isLogicalImm32(uint32_t imm) {
    if (imm == 0 || imm == UINT32_MAX) return false;

    for (unsigned width = 2; width <= 32; width <<= 1) {
        for (unsigned ones = 1; ones < width; ++ones) {
            uint32_t base = static_cast<uint32_t>((1ull << ones) - 1ull);
            for (unsigned rot = 0; rot < width; ++rot) {
                uint32_t pattern = rotateRightWithin(base, rot, width);
                if (replicatePattern(pattern, width) == imm)
                    return true;
            }
        }
    }
    return false;
}
