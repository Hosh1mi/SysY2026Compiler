#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <cstdint>
#include <cstring>
#include <string>

// ---- scratch register pool ----

void Arm64FuncContext::resetRegs() {
    usedIntRegs_ = reservedIntRegs_;
    usedFloatRegs_ = reservedFloatRegs_;
    usedNEONRegs_ = reservedNEONRegs_;
}

std::string Arm64FuncContext::allocNEONReg() {
    for (int r = 0; r <= 7; r++) {
        if (!usedNEONRegs_.count(r)) {
            usedNEONRegs_.insert(r);
            return "v" + std::to_string(r);
        }
    }
    for (int r = 16; r <= 31; r++) {
        if (!usedNEONRegs_.count(r)) {
            usedNEONRegs_.insert(r);
            return "v" + std::to_string(r);
        }
    }
    usedNEONRegs_.insert(0);
    return "v0";
}

void Arm64FuncContext::freeNEONReg(const std::string &reg) {
    if (reg.size() >= 2 && reg[0] == 'v') {
        int num = std::stoi(reg.substr(1));
        if (reservedNEONRegs_.count(num)) return;
        usedNEONRegs_.erase(num);
    }
}

void Arm64FuncContext::resetNEONRegs() {
    usedNEONRegs_ = reservedNEONRegs_;
}

void Arm64FuncContext::freeIntReg(const std::string &reg) {
    if (reg.size() >= 2 && reg[0] == 'w') {
        int num = std::stoi(reg.substr(1));
        if (reservedIntRegs_.count(num)) return;
        usedIntRegs_.erase(num);
    }
}

void Arm64FuncContext::freeAddrReg(const std::string& reg) {
    if (reg.size() >= 2 && reg[0] == 'x') {
        int num = std::stoi(reg.substr(1));
        if (reservedIntRegs_.count(num)) return;
        usedIntRegs_.erase(num);
    }
}

static int physRegNo(const std::string &reg) {
    if (reg.size() < 2) return -1;
    return std::stoi(reg.substr(1));
}

std::string Arm64FuncContext::allocIntReg() {
    for (int r = 10; r <= 15; r++) {
        if (!usedIntRegs_.count(r)) {
            usedIntRegs_.insert(r);
            return "w" + std::to_string(r);
        }
    }
    usedIntRegs_.insert(16);
    return "w16";
}

std::string Arm64FuncContext::allocFloatReg() {
    for (int r = 16; r <= 31; ++r) {
        if (!usedFloatRegs_.count(r)) {
            usedFloatRegs_.insert(r);
            return "s" + std::to_string(r);
        }
    }
    usedFloatRegs_.insert(16);
    return "s16";
}

std::string Arm64FuncContext::allocAddrReg() {
    for (int r = 10; r <= 15; r++) {
        if (!usedIntRegs_.count(r)) {
            usedIntRegs_.insert(r);
            return "x" + std::to_string(r);
        }
    }
    usedIntRegs_.insert(16);
    return "x16";
}

// ---- load from slot/constant/global to scratch register ----

std::string Arm64FuncContext::loadInt(Value *v) {
    if (auto ci = dynamic_cast<ConstantInt*>(v)) {
        if (ci->value_ == 0) return "wzr";
        std::string r = allocIntReg();
        emitIntConst(ci->value_, r);
        return r;
    }
    if (hasAssignedReg(v)) {
        std::string reg = assignedReg(v);
        int num = physRegNo(reg);
        if (num >= 0) usedIntRegs_.insert(num);
        return reg;
    }
    std::string r = allocIntReg();
    int off = getSlot(v);
    emitLoadReg(os_, r, off);
    return r;
}

std::string Arm64FuncContext::loadFloat(Value *v) {
    if (auto cf = dynamic_cast<ConstantFloat*>(v)) {
        std::string r = allocFloatReg();
        emitFloatConst(cf->value_, r);
        return r;
    }
    if (hasAssignedReg(v)) {
        std::string reg = assignedReg(v);
        int num = physRegNo(reg);
        if (num >= 0) usedFloatRegs_.insert(num);
        return reg;
    }
    std::string r = allocFloatReg();
    int off = getSlot(v);
    emitLoadReg(os_, r, off);
    return r;
}

std::string Arm64FuncContext::loadAddr(Value *v) {
    if (auto gv = dynamic_cast<GlobalVariable*>(v)) {
        std::string r = allocAddrReg();
        emitGlobalAddr(gv, r);
        return r;
    }
    if (auto ci = dynamic_cast<ConstantInt*>(v)) {
        if (ci->value_ == 0) return "xzr";
        std::string r = allocAddrReg();
        emitIntConst(ci->value_, r);
        return r;
    }
    if (hasAssignedReg(v)) {
        std::string reg = assignedReg(v, true);
        int num = physRegNo(reg);
        if (num >= 0) usedIntRegs_.insert(num);
        return reg;
    }
    if (dynamic_cast<AllocaInst*>(v)) {
        std::string r = allocAddrReg();
        int off = getSlot(v);
        if (off < 0) {
            int absOff = -off;
            if (absOff <= 4095) {
                os_ << "\tsub " << r << ", x29, #" << absOff << "\n";
            } else {
                os_ << "\tmovz x17, #" << (absOff & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((absOff >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tsub " << r << ", x29, x17\n";
            }
        } else {
            if (off <= 4095) {
                os_ << "\tadd " << r << ", x29, #" << off << "\n";
            } else {
                os_ << "\tmovz x17, #" << (off & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((off >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tadd " << r << ", x29, x17\n";
            }
        }
        return r;
    }
    std::string r = allocAddrReg();
    int off = getSlot(v);
    emitLoadReg(os_, r, off);
    return r;
}

// ---- store from register to slot ----

void Arm64FuncContext::storeInt(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) os_ << "\tmov " << target << ", " << reg << "\n";
        return;
    }
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

void Arm64FuncContext::storeFloat(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) os_ << "\tfmov " << target << ", " << reg << "\n";
        return;
    }
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

void Arm64FuncContext::storeAddr(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v, true);
        if (target != reg) os_ << "\tmov " << target << ", " << reg << "\n";
        return;
    }
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

std::string Arm64FuncContext::loadVector(Value *v) {
    if (auto *cv = dynamic_cast<ConstantVector*>(v)) {
        // Materialize constant vector directly: mov into each lane.
        // mov v.s[lane] requires a W register; float constants need fmov first.
        std::string rd = allocNEONReg();
        for (size_t i = 0; i < cv->elements_.size(); i++) {
            Constant *elem = cv->elements_[i];
            std::string ws;
            if (isFloat(elem->type_)) {
                std::string sr = loadFloat(elem);
                ws = allocIntReg();
                os_ << "\tfmov " << ws << ", " << sr << "\n";
            } else {
                ws = loadInt(elem);
            }
            os_ << "\tmov " << rd << ".s[" << i << "], " << ws << "\n";
        }
        return rd;
    }
    if (hasAssignedReg(v)) {
        std::string reg = assignedReg(v);
        int num = physRegNo(reg);
        if (num >= 0) usedNEONRegs_.insert(num);
        return reg;
    }
    std::string r = allocNEONReg();
    int off = getSlot(v);
    // ldr qN only supports unsigned offset; use sub+ldr for negative offsets
    if (off >= 0 && off <= 65520 && off % 16 == 0) {
        os_ << "\tldr q" << r.substr(1) << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        if (pos <= 4095) {
            os_ << "\tsub x16, x29, #" << pos << "\n";
        } else {
            os_ << "\tmovz x16, #" << (pos & 0xFFFF) << "\n";
            os_ << "\tmovk x16, #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tsub x16, x29, x16\n";
        }
        os_ << "\tldr q" << r.substr(1) << ", [x16]\n";
    }
    return r;
}

void Arm64FuncContext::storeVector(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) os_ << "\tmov " << target << ".16b, " << reg << ".16b\n";
        return;
    }
    int off = getSlot(v);
    // str qN only supports unsigned offset; use sub+str for negative offsets
    if (off >= 0 && off <= 65520 && off % 16 == 0) {
        os_ << "\tstr q" << reg.substr(1) << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        if (pos <= 4095) {
            os_ << "\tsub x16, x29, #" << pos << "\n";
        } else {
            os_ << "\tmovz x16, #" << (pos & 0xFFFF) << "\n";
            os_ << "\tmovk x16, #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tsub x16, x29, x16\n";
        }
        os_ << "\tstr q" << reg.substr(1) << ", [x16]\n";
    }
}

// ---- constants ----

void Arm64FuncContext::emitIntConst(int val, const std::string &reg) {
    uint32_t u = (uint32_t)val;
    os_ << "\tmovz " << reg << ", #" << (u & 0xFFFF) << "\n";
    if ((u >> 16) & 0xFFFF) {
        os_ << "\tmovk " << reg << ", #" << ((u >> 16) & 0xFFFF) << ", lsl #16\n";
    }
}

void Arm64FuncContext::emitFloatConst(float val, const std::string &reg) {
    int bits;
    std::memcpy(&bits, &val, sizeof(bits));
    uint32_t u = (uint32_t)bits;
    std::string tmp = allocIntReg();
    os_ << "\tmovz " << tmp << ", #" << (u & 0xFFFF) << "\n";
    if ((u >> 16) & 0xFFFF) {
        os_ << "\tmovk " << tmp << ", #" << ((u >> 16) & 0xFFFF) << ", lsl #16\n";
    }
    os_ << "\tfmov " << reg << ", " << tmp << "\n";
}

void Arm64FuncContext::emitGlobalAddr(GlobalVariable *gv, const std::string &reg) {
    os_ << "\tadrp " << reg << ", " << gv->name_ << "\n";
    os_ << "\tadd " << reg << ", " << reg << ", :lo12:" << gv->name_ << "\n";
}
