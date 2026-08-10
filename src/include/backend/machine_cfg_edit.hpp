// This file defines reusable, verified mutations of Machine control-flow
// edges for PHI lowering and live-range boundary placement.
#pragma once

#include "machine_ir.hpp"

#include <string>

namespace backend::aarch64 {

class MachineCFGEdit {
public:
  static bool canSplitEdge(const MachineBasicBlock &predecessor,
                           const MachineBasicBlock &successor);

  // Replaces predecessor -> successor with predecessor -> split -> successor
  // and retargets every explicit branch operand naming the old successor.
  static MachineBasicBlock &splitEdge(MachineFunction &function,
                                      MachineBasicBlock &predecessor,
                                      MachineBasicBlock &successor,
                                      std::string name);
};

} // namespace backend::aarch64
