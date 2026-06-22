#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

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

void Arm64FuncContext::emitStoreRegMachine(const std::string &reg, int off) {
    auto emitStore = [&](const std::string &line, std::initializer_list<std::string> uses) {
        MachineInstr inst = MachineInstr::make(line, MOpcode::Store, {}, uses);
        inst.mayStore = true;
        emitMachineInstr(std::move(inst));
    };

    if (off >= -256 && off <= 255) {
        emitStore("\tstr " + reg + ", [x29, #" + std::to_string(off) + "]",
                  {reg, "x29"});
    } else {
        int pos = -off;
        std::string base = (reg == "x17") ? "x16" : "x17";
        if (pos <= 4095) {
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, #" + std::to_string(pos),
                MOpcode::Alu, {base}, {"x29"}));
        } else {
            emitIntConst(pos, base);
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, " + base,
                MOpcode::Alu, {base}, {"x29", base}));
        }
        emitStore("\tstr " + reg + ", [" + base + "]", {reg, base});
    }
}

void Arm64FuncContext::emitLoadRegMachine(const std::string &reg, int off) {
    auto emitLoad = [&](const std::string &line, std::initializer_list<std::string> uses) {
        MachineInstr inst = MachineInstr::make(line, MOpcode::Load, {reg}, uses, 4);
        inst.mayLoad = true;
        emitMachineInstr(std::move(inst));
    };

    if (off >= -256 && off <= 255) {
        emitLoad("\tldr " + reg + ", [x29, #" + std::to_string(off) + "]",
                 {"x29"});
    } else {
        int pos = -off;
        std::string base = (reg == "x17") ? "x16" : "x17";
        if (pos <= 4095) {
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, #" + std::to_string(pos),
                MOpcode::Alu, {base}, {"x29"}));
        } else {
            emitIntConst(pos, base);
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, " + base,
                MOpcode::Alu, {base}, {"x29", base}));
        }
        emitLoad("\tldr " + reg + ", [" + base + "]", {base});
    }
}

void Arm64FuncContext::emitStorePairMachine(const std::string &r1, const std::string &r2, int off) {
    bool is64 = (r1[0] == 'x' || r1[0] == 'd' || r2[0] == 'x' || r2[0] == 'd');
    int range = is64 ? 504 : 252;
    int align = is64 ? 8 : 4;
    if (off >= -range && off <= range && off % align == 0) {
        MachineInstr inst = MachineInstr::make(
            "\tstp " + r1 + ", " + r2 + ", [x29, #" + std::to_string(off) + "]",
            MOpcode::PairStore, {}, {r1, r2, "x29"});
        inst.mayStore = true;
        emitMachineInstr(std::move(inst));
    } else if (off >= -256 && off <= 255) {
        emitStoreRegMachine(r1, off);
        emitStoreRegMachine(r2, off + (is64 ? 8 : 4));
    } else {
        std::string base = (r1 == "x17" || r2 == "x17") ? "x16" : "x17";
        int pos = -off;
        if (pos <= 4095) {
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, #" + std::to_string(pos),
                MOpcode::Alu, {base}, {"x29"}));
        } else {
            emitIntConst(pos, base);
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, " + base,
                MOpcode::Alu, {base}, {"x29", base}));
        }
        MachineInstr inst = MachineInstr::make(
            "\tstp " + r1 + ", " + r2 + ", [" + base + "]",
            MOpcode::PairStore, {}, {r1, r2, base});
        inst.mayStore = true;
        emitMachineInstr(std::move(inst));
    }
}

void Arm64FuncContext::emitLoadPairMachine(const std::string &r1, const std::string &r2, int off) {
    bool is64 = (r1[0] == 'x' || r1[0] == 'd' || r2[0] == 'x' || r2[0] == 'd');
    int range = is64 ? 504 : 252;
    int align = is64 ? 8 : 4;
    if (off >= -range && off <= range && off % align == 0) {
        MachineInstr inst = MachineInstr::make(
            "\tldp " + r1 + ", " + r2 + ", [x29, #" + std::to_string(off) + "]",
            MOpcode::PairLoad, {r1, r2}, {"x29"}, 4);
        inst.mayLoad = true;
        emitMachineInstr(std::move(inst));
    } else if (off >= -256 && off <= 255) {
        emitLoadRegMachine(r1, off);
        emitLoadRegMachine(r2, off + (is64 ? 8 : 4));
    } else {
        std::string base = (r1 == "x17" || r2 == "x17") ? "x16" : "x17";
        int pos = -off;
        if (pos <= 4095) {
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, #" + std::to_string(pos),
                MOpcode::Alu, {base}, {"x29"}));
        } else {
            emitIntConst(pos, base);
            emitMachineInstr(MachineInstr::make(
                "\tsub " + base + ", x29, " + base,
                MOpcode::Alu, {base}, {"x29", base}));
        }
        MachineInstr inst = MachineInstr::make(
            "\tldp " + r1 + ", " + r2 + ", [" + base + "]",
            MOpcode::PairLoad, {r1, r2}, {base}, 4);
        inst.mayLoad = true;
        emitMachineInstr(std::move(inst));
    }
}

void Arm64FuncContext::emitStoreRegSPMachine(const std::string &reg, int off) {
    MachineInstr inst = MachineInstr::make(
        "\tstr " + reg + ", [sp, #" + std::to_string(off) + "]",
        MOpcode::Store, {}, {reg, "sp"});
    inst.mayStore = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitLoadRegSPMachine(const std::string &reg, int off) {
    MachineInstr inst = MachineInstr::make(
        "\tldr " + reg + ", [sp, #" + std::to_string(off) + "]",
        MOpcode::Load, {reg}, {"sp"}, 4);
    inst.mayLoad = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitStorePairSPMachine(const std::string &r1, const std::string &r2, int off) {
    MachineInstr inst = MachineInstr::make(
        "\tstp " + r1 + ", " + r2 + ", [sp, #" + std::to_string(off) + "]",
        MOpcode::PairStore, {}, {r1, r2, "sp"});
    inst.mayStore = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitLoadPairSPMachine(const std::string &r1, const std::string &r2, int off) {
    MachineInstr inst = MachineInstr::make(
        "\tldp " + r1 + ", " + r2 + ", [sp, #" + std::to_string(off) + "]",
        MOpcode::PairLoad, {r1, r2}, {"sp"}, 4);
    inst.mayLoad = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitLoadMemMachine(const std::string &reg, const std::string &addrText,
                                          std::initializer_list<std::string> uses,
                                          MOpcode opcode, int latency) {
    MachineInstr inst = MachineInstr::make("\tldr " + reg + ", " + addrText,
                                           opcode, {reg}, uses, latency);
    inst.mayLoad = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitStoreMemMachine(const std::string &reg, const std::string &addrText,
                                           std::initializer_list<std::string> uses,
                                           MOpcode opcode) {
    MachineInstr inst = MachineInstr::make("\tstr " + reg + ", " + addrText,
                                           opcode, {}, uses);
    inst.mayStore = true;
    emitMachineInstr(std::move(inst));
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

// 取一个持有常量 val 的只读寄存器：优先用提升常量寄存器，
// 否则物化进 scratch。调用方不得写入返回的寄存器。
std::string Arm64FuncContext::intConstReg(int val) {
    auto it = promotedConsts_.find(val);
    if (it != promotedConsts_.end()) return it->second;
    std::string r = allocIntReg();
    emitIntConst(val, r);
    return r;
}

std::string Arm64FuncContext::loadInt(Value *v) {
    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
        if (ci->value_ == 0) return "wzr";
        return intConstReg(ci->value_);
    }
    if (hasAssignedReg(v)) {
        std::string reg = assignedReg(v);
        int num = physRegNo(reg);
        if (num >= 0) usedIntRegs_.insert(num);
        return reg;
    }
    std::string r = allocIntReg();
    int off = getSlot(v);
    emitLoadRegMachine(r, off);
    return r;
}

std::string Arm64FuncContext::loadFloat(Value *v) {
    if (auto *cf = dynamic_cast<ConstantFloat*>(v)) {
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
    emitLoadRegMachine(r, off);
    return r;
}

std::string Arm64FuncContext::loadAddr(Value *v) {
    if (auto *gv = dynamic_cast<GlobalVariable*>(v)) {
        std::string r = allocAddrReg();
        emitGlobalAddr(gv, r);
        return r;
    }
    if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
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
                emitRawAluMachine("\tsub " + r + ", x29, #" + std::to_string(absOff),
                                  r, {"x29"});
            } else {
                emitIntConst(absOff, "x17");
                emitBinaryMachine("sub", r, "x29", "x17");
            }
        } else {
            if (off <= 4095) {
                emitRawAluMachine("\tadd " + r + ", x29, #" + std::to_string(off),
                                  r, {"x29"});
            } else {
                emitIntConst(off, "x17");
                emitBinaryMachine("add", r, "x29", "x17");
            }
        }
        return r;
    }
    std::string r = allocAddrReg();
    int off = getSlot(v);
    emitLoadRegMachine(r, off);
    return r;
}

// ---- store from register to slot ----

void Arm64FuncContext::storeInt(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) emitMoveMachine(target, reg);
        return;
    }
    int off = getSlot(v);
    emitStoreRegMachine(reg, off);
}

void Arm64FuncContext::storeFloat(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) emitMoveMachine(target, reg, "fmov");
        return;
    }
    int off = getSlot(v);
    emitStoreRegMachine(reg, off);
}

void Arm64FuncContext::storeAddr(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v, true);
        if (target != reg) emitMoveMachine(target, reg);
        return;
    }
    int off = getSlot(v);
    emitStoreRegMachine(reg, off);
}

std::string Arm64FuncContext::loadVector(Value *v) {
    if (auto *cv = dynamic_cast<ConstantVector*>(v)) {
        auto sameIntLane = [&]() -> ConstantInt* {
            if (cv->elements_.empty()) return nullptr;
            auto *first = dynamic_cast<ConstantInt*>(cv->elements_[0]);
            if (!first) return nullptr;
            for (auto *elem : cv->elements_) {
                auto *ci = dynamic_cast<ConstantInt*>(elem);
                if (!ci || ci->value_ != first->value_) return nullptr;
            }
            return first;
        };

        if (auto *splat = sameIntLane()) {
            std::string rd = allocNEONReg();
            int value = splat->value_;
            if (value == 0) {
                emitRawAluMachine("\tmovi " + rd + ".4s, #0",
                                  rd, {}, MOpcode::Neon);
                return rd;
            }
            if (value > 0 && value <= 255) {
                emitRawAluMachine("\tmovi " + rd + ".4s, #" + std::to_string(value),
                                  rd, {}, MOpcode::Neon);
                return rd;
            }
        }

        // Materialize constant vector directly: mov into each lane.
        // mov v.s[lane] requires a W register; float constants need fmov first.
        std::string rd = allocNEONReg();
        for (size_t i = 0; i < cv->elements_.size(); i++) {
            Constant *elem = cv->elements_[i];
            std::string ws;
            if (isFloat(elem->type_)) {
                std::string sr = loadFloat(elem);
                ws = allocIntReg();
                emitMoveMachine(ws, sr, "fmov");
            } else {
                ws = loadInt(elem);
            }
            emitRawAluMachine("\tmov " + rd + ".s[" + std::to_string(i) + "], " + ws,
                              rd, {ws}, MOpcode::Neon);
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
        emitLoadMemMachine("q" + r.substr(1), "[x29, #" + std::to_string(off) + "]",
                           {"x29"}, MOpcode::Load, 4);
    } else {
        int pos = -off;
        if (pos <= 4095) {
            emitRawAluMachine("\tsub x16, x29, #" + std::to_string(pos),
                              "x16", {"x29"});
        } else {
            emitIntConst(pos, "x16");
            emitBinaryMachine("sub", "x16", "x29", "x16");
        }
        emitLoadMemMachine("q" + r.substr(1), "[x16]", {"x16"}, MOpcode::Load, 4);
    }
    return r;
}

void Arm64FuncContext::storeVector(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg)
            emitRawAluMachine("\tmov " + target + ".16b, " + reg + ".16b",
                              target, {reg}, MOpcode::Neon);
        return;
    }
    int off = getSlot(v);
    // str qN only supports unsigned offset; use sub+str for negative offsets
    if (off >= 0 && off <= 65520 && off % 16 == 0) {
        emitStoreMemMachine("q" + reg.substr(1), "[x29, #" + std::to_string(off) + "]",
                            {"q" + reg.substr(1), "x29"}, MOpcode::Store);
    } else {
        int pos = -off;
        if (pos <= 4095) {
            emitRawAluMachine("\tsub x16, x29, #" + std::to_string(pos),
                              "x16", {"x29"});
        } else {
            emitIntConst(pos, "x16");
            emitBinaryMachine("sub", "x16", "x29", "x16");
        }
        emitStoreMemMachine("q" + reg.substr(1), "[x16]",
                            {"q" + reg.substr(1), "x16"}, MOpcode::Store);
    }
}

// ---- constants ----

void Arm64FuncContext::emitIntConst(int val, const std::string &reg) {
    uint32_t u = (uint32_t)val;
    emitMachineInstr(MachineInstr::make(
        "\tmovz " + reg + ", #" + std::to_string(u & 0xFFFF),
        MOpcode::Mov, {reg}));
    if ((u >> 16) & 0xFFFF) {
        emitMachineInstr(MachineInstr::make(
            "\tmovk " + reg + ", #" + std::to_string((u >> 16) & 0xFFFF) + ", lsl #16",
            MOpcode::Mov, {reg}, {reg}));
    }
}

void Arm64FuncContext::emitFloatConst(float val, const std::string &reg) {
    int bits;
    std::memcpy(&bits, &val, sizeof(bits));
    uint32_t u = (uint32_t)bits;
    std::string tmp = allocIntReg();
    emitMachineInstr(MachineInstr::make(
        "\tmovz " + tmp + ", #" + std::to_string(u & 0xFFFF),
        MOpcode::Mov, {tmp}));
    if ((u >> 16) & 0xFFFF) {
        emitMachineInstr(MachineInstr::make(
            "\tmovk " + tmp + ", #" + std::to_string((u >> 16) & 0xFFFF) + ", lsl #16",
            MOpcode::Mov, {tmp}, {tmp}));
    }
    emitMachineInstr(MachineInstr::make(
        "\tfmov " + reg + ", " + tmp,
        MOpcode::Mov, {reg}, {tmp}));
}

void Arm64FuncContext::emitGlobalAddr(GlobalVariable *gv, const std::string &reg) {
    emitMachineInstr(MachineInstr::make(
        "\tadrp " + reg + ", " + gv->name_,
        MOpcode::Adr, {reg}));
    emitMachineInstr(MachineInstr::make(
        "\tadd " + reg + ", " + reg + ", :lo12:" + gv->name_,
        MOpcode::Alu, {reg}, {reg}));
}
