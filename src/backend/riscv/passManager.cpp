#include "../../include/backend/riscv/passManager.hpp"
#include "../../include/backend/riscv/machineDCE.hpp"
#include "../../include/backend/riscv/peephole.hpp"

#include <iostream>

namespace riscv {

namespace {

void verifyOrWarn(const MFunction &func, const char *stage) {
    std::string err;
    if (!verifyMFunction(func, err))
        std::cerr << "[riscv-verify] " << stage << ": " << err << "\n";
}

}  // namespace

void runMachinePasses(MFunction &func, const MachinePassOptions &opt) {
    buildCFG(func);
    if (opt.verify) verifyOrWarn(func, "lowered");
    if (opt.dumpPre) std::cerr << dumpMFunction(func);

    if (opt.runPeephole) {
        bool changed = true;
        for (int round = 0; changed && round < 8; ++round) {
            changed = false;
            changed |= forwardAdjacentStoreLoads(func);
            changed |= propagateCopies(func);
            changed |= redirectProducers(func);
            changed |= removeSelfMoves(func);
            changed |= eliminateDeadMachineInstructions(func);
            changed |= forwardBranches(func);
            changed |= removeDeadBlocks(func);
            changed |= removeFallthroughJumps(func);
        }
        buildCFG(func);  // 管线改写后刷新邻接，供后续分析/调度使用
    }

    if (opt.verify) verifyOrWarn(func, "optimized");
    if (opt.dumpPost) std::cerr << dumpMFunction(func);
}

}  // namespace riscv
