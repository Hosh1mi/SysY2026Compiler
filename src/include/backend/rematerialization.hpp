// This file defines target-aware rematerialization recipes used by live-range
// splitting and spilling to recreate values instead of loading stack slots.
#pragma once

#include "machine_analysis.hpp"

#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

struct RematerializationRecipe {
  RegClass regClass = RegClass::Invalid;
  MachineInstr *definition = nullptr;
  unsigned cost = 0;

  MachineInstr clone(VReg destination) const;
};

class RematerializationAnalysis {
public:
  using RecipeMap =
      std::unordered_map<VReg, RematerializationRecipe>;

  RecipeMap analyze(MachineFunction &function,
                    const MachineRegisterIndex &registers,
                    const std::vector<VReg> &candidates) const;
};

} // namespace backend::aarch64
