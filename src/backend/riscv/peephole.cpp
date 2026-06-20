#include "../../include/backend/riscv/peephole.hpp"

namespace riscv {

bool removeSelfMoves(MFunction &func) {
    bool changed = false;
    for (auto it = func.insts.begin(); it != func.insts.end();) {
        bool selfMove = it->opcode == MOpcode::Move && it->operands.size() == 2 &&
                        it->operands[0] == it->operands[1];
        if (selfMove) {
            it = func.insts.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    return changed;
}

bool forwardAdjacentStoreLoads(MFunction &func) {
    bool changed = false;
    for (size_t i = 0; i + 1 < func.insts.size(); ++i) {
        const MInst &store = func.insts[i];
        const MInst &load = func.insts[i + 1];
        if (store.opcode != MOpcode::Store || load.opcode != MOpcode::Load ||
            store.operands.size() != 2 || load.operands.size() != 2 ||
            store.operands[1] != load.operands[1])
            continue;

        std::string move;
        if (store.mnemonic == "sd" && load.mnemonic == "ld") {
            move = "mv " + load.operands[0] + ", " + store.operands[0];
        } else if (store.mnemonic == "sw" && load.mnemonic == "lw") {
            // lw sign-extends the stored low word; addiw with zero preserves
            // that behavior even if the source register is not canonical i32.
            move = "addiw " + load.operands[0] + ", " + store.operands[0] + ", 0";
        } else if (store.mnemonic == "fsw" && load.mnemonic == "flw") {
            move = "fmv.s " + load.operands[0] + ", " + store.operands[0];
        } else if (store.mnemonic == "fsd" && load.mnemonic == "fld") {
            move = "fmv.d " + load.operands[0] + ", " + store.operands[0];
        } else {
            continue;
        }
        func.insts[i + 1] = MInst::inst(std::move(move));
        changed = true;
    }
    return changed;
}

}  // namespace riscv
