#include "../../include/backend/riscv/machineDCE.hpp"
#include "../../include/backend/riscv/liveness.hpp"

namespace riscv {

namespace {

bool isDeletable(const MInst &m) {
    if (m.isBarrier || m.mayLoad || m.mayStore || m.isCall || m.defs.empty()) return false;
    switch (m.opcode) {
    case MOpcode::Move:
    case MOpcode::Alu:
    case MOpcode::Mul:
    case MOpcode::Div:
    case MOpcode::Address:
        return true;
    default:
        return false;
    }
}

bool hasLiveDefinition(const MInst &m, const std::set<std::string> &liveOut) {
    for (const auto &reg : m.defs)
        if (liveOut.count(reg)) return true;
    return false;
}

}  // namespace

bool eliminateDeadMachineInstructions(MFunction &func) {
    bool changedAny = false;
    bool changed;
    do {
        changed = false;
        LivenessResult liveness = analyzeLiveness(func);
        for (auto &bb : func.blocks) {
            for (auto it = bb.insts.begin(); it != bb.insts.end(); ++it) {
                auto live = liveness.liveOut.find(&*it);
                if (live == liveness.liveOut.end()) continue;
                if (isDeletable(*it) && !hasLiveDefinition(*it, live->second)) {
                    bb.insts.erase(it);
                    changed = changedAny = true;
                    break;
                }
            }
            if (changed) break;
        }
    } while (changed);
    return changedAny;
}

}  // namespace riscv
