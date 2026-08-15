// This file defines the linear MachineFunction pass list used by the native
// backend, plus its optional diagnostics.
#pragma once

#include "machine_ir.hpp"

#include <string>
#include <vector>

namespace backend::aarch64 {

class MachineVerifier;

class MachineFunctionPassManager {
public:
  typedef bool (*PassRunner)(MachineFunction &);

  MachineFunctionPassManager(const MachineVerifier &verifier,
                             bool verifyEachPass, bool dump);

  void addPass(std::string name, PassRunner runner);
  void run(MachineFunction &function) const;

private:
  struct PassEntry {
    std::string name;
    PassRunner runner;
  };

  const MachineVerifier &verifier_;
  bool verifyEachPass_;
  bool dump_;
  std::vector<PassEntry> passes_;
};

} // namespace backend::aarch64
