#pragma once

#include "machine_ir.hpp"

namespace backend::aarch64 {

class AArch64FrameLowering {
public:
    void run(MachineFunction &function) const;

private:
    void determineCalleeSaves(MachineFunction &function) const;
    void layoutFrame(MachineFunction &function) const;
    void eliminateFrameIndices(MachineFunction &function) const;
    void insertPrologueEpilogues(MachineFunction &function) const;
};

} // namespace backend::aarch64
