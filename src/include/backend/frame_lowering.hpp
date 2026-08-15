#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class AArch64FrameLowering {
public:
    static bool run(MachineFunction &function);

private:
    static void determineCalleeSaves(MachineFunction &function);
    static void layoutFrame(MachineFunction &function);
    static void eliminateFrameIndices(MachineFunction &function);
    static void insertPrologueEpilogues(MachineFunction &function);
};

} // namespace backend::aarch64
