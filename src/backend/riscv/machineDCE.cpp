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
        for (auto it = func.insts.begin(); it != func.insts.end(); ++it) {
            auto live = liveness.liveOut.find(&*it);
            if (live == liveness.liveOut.end()) continue;
            if (isDeletable(*it) && !hasLiveDefinition(*it, live->second)) {
                func.insts.erase(it);
                changed = changedAny = true;
                break;
            }
        }
    } while (changed);
    return changedAny;
}

}  // namespace riscv
