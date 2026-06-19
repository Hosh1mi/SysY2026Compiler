#include "../../include/backend/arm64/machineDCE.hpp"
#include "../../include/backend/arm64/liveness.hpp"

#include <set>
#include <string>

namespace {

std::set<std::string> effectiveDefs(const MachineInstr &inst) {
    std::set<std::string> defs = inst.defs;
    if (inst.setsFlags)
        defs.insert(kMachineFlagsReg);
    return defs;
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
    do {
        changed = false;
        MachineLivenessResult liveness = MachineLiveness().analyze(func);
        for (auto &block : func.blocks) {
            for (auto it = block.instrs.begin(); it != block.instrs.end(); ++it) {
                auto liveIt = liveness.instrLiveOut.find(&*it);
                if (liveIt == liveness.instrLiveOut.end())
                    continue;

                if (canDelete(*it, liveIt->second)) {
                    block.instrs.erase(it);
                    changed = true;
                    changedAny = true;
                    break;
                }
            }
            if (changed)
                break;
        }
    } while (changed);

    return changedAny;
}
