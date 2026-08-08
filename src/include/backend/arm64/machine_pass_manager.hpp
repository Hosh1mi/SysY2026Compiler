// This file defines the staged MachineFunction pipeline used by the native
// backend.  It centralizes pass ordering, property contracts, verification,
// tracing, and profiling without coupling transformations to the driver.
#pragma once

#include "machine_ir.hpp"

#include <functional>
#include <string>
#include <vector>

namespace backend::aarch64 {

class MachineVerifier;

enum class MachinePassStage {
  MachineSSA,
  SSAElimination,
  PreRegAlloc,
  RegAlloc,
  PostRegAlloc,
  FrameFinalization,
  PreEmit,
};

struct MachinePassContract {
  MachineProperty required = MachineProperty::None;
  MachineProperty forbidden = MachineProperty::None;
};

class MachineFunctionPassManager {
public:
  using PassRunner = std::function<bool(MachineFunction &)>;

  explicit MachineFunctionPassManager(const MachineVerifier *verifier,
                                      bool verifyEachPass);

  void setDump(bool value) { dump_ = value; }
  void setTrace(bool value) { trace_ = value; }

  void addPass(std::string name, MachinePassStage stage,
               MachinePassContract contract, PassRunner runner);
  void run(MachineFunction &function) const;

private:
  struct PassEntry {
    std::string name;
    MachinePassStage stage;
    MachinePassContract contract;
    PassRunner runner;
  };

  const MachineVerifier *verifier_;
  bool verifyEachPass_;
  bool dump_ = false;
  bool trace_ = false;
  std::vector<PassEntry> passes_;
};

} // namespace backend::aarch64
