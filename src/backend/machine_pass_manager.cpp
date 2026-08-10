// This file executes the staged MachineFunction pipeline and provides the
// common diagnostics expected from a production code-generation pipeline.
#include "backend/machine_pass_manager.hpp"

#include "backend/verifier.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace backend::aarch64 {
namespace {

std::string_view stageName(MachinePassStage stage) {
  switch (stage) {
  case MachinePassStage::MachineSSA:
    return "machine-ssa";
  case MachinePassStage::SSAElimination:
    return "ssa-elimination";
  case MachinePassStage::PreRegAlloc:
    return "pre-regalloc";
  case MachinePassStage::RegAlloc:
    return "regalloc";
  case MachinePassStage::PostRegAlloc:
    return "post-regalloc";
  case MachinePassStage::FrameFinalization:
    return "frame-finalization";
  case MachinePassStage::PreEmit:
    return "pre-emit";
  }
  return "unknown";
}

bool envEnabled(const char *name) { return std::getenv(name) != nullptr; }

} // namespace

MachineFunctionPassManager::MachineFunctionPassManager(
    const MachineVerifier *verifier, bool verifyEachPass)
    : verifier_(verifier), verifyEachPass_(verifyEachPass) {}

void MachineFunctionPassManager::addPass(std::string name,
                                         MachinePassStage stage,
                                         MachinePassContract contract,
                                         PassRunner runner) {
  if (!runner)
    throw std::invalid_argument("machine pass requires a runner");
  if (!passes_.empty() && static_cast<unsigned>(stage) <
                              static_cast<unsigned>(passes_.back().stage))
    throw std::logic_error(
        "machine passes must be added in nondecreasing stage order");
  passes_.push_back(
      PassEntry{std::move(name), stage, contract, std::move(runner)});
}

void MachineFunctionPassManager::run(MachineFunction &function) const {
  const bool trace = trace_ || envEnabled("TRACE_MACHINE_PIPELINE");
  const bool dump = dump_ || envEnabled("DUMP_MACHINE_PIPELINE");
  const bool profile = envEnabled("PROFILE_MACHINE_PASSES");

  for (const PassEntry &pass : passes_) {
    if (!function.hasAllProperties(pass.contract.required))
      throw std::logic_error("machine pass " + pass.name +
                             " is missing a required MachineFunction property");
    if (pass.contract.forbidden != MachineProperty::None &&
        function.hasProperty(pass.contract.forbidden))
      throw std::logic_error(
          "machine pass " + pass.name +
          " encountered a forbidden MachineFunction property");

    if (trace)
      std::cerr << "[MachinePipeline] " << stageName(pass.stage)
                << " :: " << pass.name << '\n';
    if (dump)
      std::cerr << "; *** MIR before " << pass.name << " ***\n"
                << printMachineIR(function);

    const auto start = std::chrono::steady_clock::now();
    const bool changed = pass.runner(function);
    const auto end = std::chrono::steady_clock::now();

    if (verifyEachPass_ && verifier_)
      verifier_->verifyOrThrow(function, pass.name);
    if (dump)
      std::cerr << "; *** MIR after " << pass.name << " ***\n"
                << printMachineIR(function);
    if (profile) {
      const auto micros =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count();
      std::cerr << "[MachinePassProfile] " << pass.name << ' ' << micros
                << " us changed=" << changed << '\n';
    }
  }
}

} // namespace backend::aarch64
