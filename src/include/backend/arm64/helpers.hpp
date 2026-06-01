#pragma once
#include "../../mid/ir/ir.hpp"
#include <map>
#include <ostream>
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
    default: return 8;
    }
}

inline bool isFloat(Type *ty)  { return ty->tid_ == Type::FloatTyID; }
inline bool isInt(Type *ty)    { return ty->tid_ == Type::IntegerTyID; }
inline bool isPtr(Type *ty)    { return ty->tid_ == Type::PointerTyID; }
inline bool isVoid(Type *ty)   { return ty->tid_ == Type::VoidTyID; }
inline bool isLabel(Type *ty)  { return ty->tid_ == Type::LabelTyID; }

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
            if (r >= 19) regs.insert(r);
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
            if (r >= 8 && r <= 15) regs.insert(r);
        }
    }
    return std::vector<int>(regs.begin(), regs.end());
}

// ── store/load with potentially large negative offset (x29-relative) ────────

inline void emitStoreReg(std::ostream &os, const std::string &reg, int off) {
    if (off >= -256 && off <= 255) {
        os << "\tstr " << reg << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        // 使用 x16 作为临时基址寄存器，避免覆盖可能为 x17 的源寄存器
        std::string base = (reg == "x17") ? "x16" : "x17";
        if (pos <= 4095) {
            os << "\tsub " << base << ", x29, #" << pos << "\n";
        } else {
            os << "\tmovz " << base << ", #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk " << base << ", #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub " << base << ", x29, " << base << "\n";
        }
        os << "\tstr " << reg << ", [" << base << "]\n";
    }
}

inline void emitLoadReg(std::ostream &os, const std::string &reg, int off) {
    if (off >= -256 && off <= 255) {
        os << "\tldr " << reg << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        // 使用 x16 作为临时基址寄存器，避免覆盖可能为 x17 的目标寄存器
        std::string base = (reg == "x17") ? "x16" : "x17";
        if (pos <= 4095) {
            os << "\tsub " << base << ", x29, #" << pos << "\n";
        } else {
            os << "\tmovz " << base << ", #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk " << base << ", #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub " << base << ", x29, " << base << "\n";
        }
        os << "\tldr " << reg << ", [" << base << "]\n";
    }
}

// Emit stp with potentially large negative offset (x29-relative)
inline void emitStorePair(std::ostream &os, const std::string &r1,
                          const std::string &r2, int off) {
    // x and d registers are 64-bit, requiring 8-byte alignment for stp
    bool is64 = (r1[0] == 'x' || r1[0] == 'd' || r2[0] == 'x' || r2[0] == 'd');
    int range = is64 ? 504 : 252;
    int align = is64 ? 8 : 4;
    if (off >= -range && off <= range && off % align == 0) {
        os << "\tstp " << r1 << ", " << r2 << ", [x29, #" << off << "]\n";
    } else if (off >= -256 && off <= 255) {
        // Offset valid for single str but not stp (misaligned or out of stp range):
        // fall back to two individual stores
        emitStoreReg(os, r1, off);
        emitStoreReg(os, r2, off + (is64 ? 8 : 4));  // r2 was at higher address
    } else {
        std::string base = (r1 == "x17" || r2 == "x17") ? "x16" : "x17";
        int pos = -off;
        if (pos <= 4095) {
            os << "\tsub " << base << ", x29, #" << pos << "\n";
        } else {
            os << "\tmovz " << base << ", #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk " << base << ", #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub " << base << ", x29, " << base << "\n";
        }
        os << "\tstp " << r1 << ", " << r2 << ", [" << base << "]\n";
    }
}

// Emit ldp with potentially large negative offset (x29-relative)
inline void emitLoadPair(std::ostream &os, const std::string &r1,
                         const std::string &r2, int off) {
    bool is64 = (r1[0] == 'x' || r1[0] == 'd' || r2[0] == 'x' || r2[0] == 'd');
    int range = is64 ? 504 : 252;
    int align = is64 ? 8 : 4;
    if (off >= -range && off <= range && off % align == 0) {
        os << "\tldp " << r1 << ", " << r2 << ", [x29, #" << off << "]\n";
    } else if (off >= -256 && off <= 255) {
        // Offset valid for single ldr but not ldp: fall back to two individual loads
        emitLoadReg(os, r1, off);
        emitLoadReg(os, r2, off + (is64 ? 8 : 4));  // r2 was at higher address
    } else {
        std::string base = (r1 == "x17" || r2 == "x17") ? "x16" : "x17";
        int pos = -off;
        if (pos <= 4095) {
            os << "\tsub " << base << ", x29, #" << pos << "\n";
        } else {
            os << "\tmovz " << base << ", #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk " << base << ", #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub " << base << ", x29, " << base << "\n";
        }
        os << "\tldp " << r1 << ", " << r2 << ", [" << base << "]\n";
    }
}

// ── label helpers ────────────────────────────────────────────────────────────

inline std::string bbLabel(Function *f, BasicBlock *bb) {
    return f->name_ + "_" + bb->name_;
}
