#include "../../include/backend/arm64/machineDCE.hpp"
#include "../../include/backend/arm64/liveness.hpp"

#include <set>
#include <string>

namespace {

std::set<std::string> physicalBoundaryRegs() {
    std::set<std::string> regs;
    for (int r = 0; r <= 30; ++r)
        regs.insert("r" + std::to_string(r));
    for (int r = 0; r <= 31; ++r)
        regs.insert("v" + std::to_string(r));
    return regs;
}

std::set<std::string> effectiveUses(const MachineInstr &inst) {
    std::set<std::string> uses = inst.uses;
    if (inst.usesFlags)
        uses.insert(kMachineFlagsReg);
    if (inst.opcode == MOpcode::Neon && inst.text.find(".s[") != std::string::npos) {
        uses.insert(inst.defs.begin(), inst.defs.end());
    }
    return uses;
}

std::set<std::string> effectiveDefs(const MachineInstr &inst) {
    std::set<std::string> defs = inst.defs;
    if (inst.setsFlags)
        defs.insert(kMachineFlagsReg);
    return defs;
}

void applyTransfer(const MachineInstr &inst, std::set<std::string> &live) {
    auto defs = effectiveDefs(inst);
    auto uses = effectiveUses(inst);
    for (const auto &reg : defs)
        live.erase(reg);
    live.insert(uses.begin(), uses.end());
}

bool hasLiveDef(const MachineInstr &inst, const std::set<std::string> &liveOut) {
    auto defs = effectiveDefs(inst);
    for (const auto &reg : defs) {
        if (liveOut.count(reg))
            return true;
    }
    return false;
}

bool isDeletableOpcode(const MachineInstr &inst) {
    switch (inst.opcode) {
        case MOpcode::Mov:
        case MOpcode::Alu:
        case MOpcode::Mul:
        case MOpcode::Div:
        case MOpcode::Adr:
        case MOpcode::Cmp:
        case MOpcode::FlagUse:
        case MOpcode::Neon:
            return true;
        default:
            return false;
    }
}

bool canDelete(const MachineInstr &inst, const std::set<std::string> &liveOut) {
    if (inst.isBarrier || inst.isLabelLike || inst.isCall)
        return false;
    if (inst.mayLoad || inst.mayStore)
        return false;
    if (!isDeletableOpcode(inst))
        return false;
    if (inst.defs.empty() && !inst.setsFlags)
        return false;
    return !hasLiveDef(inst, liveOut);
}

} // namespace

bool machineDCE(MachineFunction &func) {
    bool changedAny = false;
    bool changed;
    const auto boundaryRegs = physicalBoundaryRegs();
    do {
        changed = false;
        for (auto &block : func.blocks) {
            std::set<std::string> live = boundaryRegs;
            for (auto rit = block.instrs.rbegin(); rit != block.instrs.rend(); ++rit) {
                auto base = rit.base();
                --base;

                if (canDelete(*rit, live)) {
                    block.instrs.erase(base);
                    changed = true;
                    changedAny = true;
                    break;
                }

                applyTransfer(*rit, live);
                if (rit->isBarrier || rit->isLabelLike) {
                    live.insert(boundaryRegs.begin(), boundaryRegs.end());
                }
            }
            if (changed)
                break;
        }
    } while (changed);

    return changedAny;
}
