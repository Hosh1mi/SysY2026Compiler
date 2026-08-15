// This file executes the linear MachineFunction pass list and provides the
// common diagnostics used by the code-generation driver.
#include "backend/machine_pass_manager.hpp"

#include "backend/verifier.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace backend::aarch64 {
namespace {

bool envEnabled(const char *name) { return std::getenv(name) != nullptr; }

} // namespace

MachineFunctionPassManager::MachineFunctionPassManager(
    const MachineVerifier *verifier, bool verifyEachPass)
    : verifier_(verifier), verifyEachPass_(verifyEachPass) {}

void MachineFunctionPassManager::addPass(std::string name,
                                         PassRunner runner) {
	passes_.push_back(PassEntry{std::move(name), runner});
}

void MachineFunctionPassManager::run(MachineFunction &function) const {
	const bool trace = trace_ || envEnabled("TRACE_MACHINE_PIPELINE");
	const bool dump = dump_ || envEnabled("DUMP_MACHINE_PIPELINE");
	const bool profile = envEnabled("PROFILE_MACHINE_PASSES");

	for (const PassEntry &pass : passes_) {
		if (trace)
			std::cerr << "[MachinePipeline] " << pass.name << '\n';
		if (dump)
			std::cerr << "; *** MIR before " << pass.name << " ***\n"
			          << printMachineIR(function);

		const auto start = std::chrono::steady_clock::now();
		const bool changed = pass.runner(function);
		const auto end = std::chrono::steady_clock::now();

		if (verifyEachPass_ && verifier_)
			verifier_->verifyOrAbort(function, pass.name);
		if (dump)
			std::cerr << "; *** MIR after " << pass.name << " ***\n"
			          << printMachineIR(function);
		if (profile) {
			const auto micros =
			    std::chrono::duration_cast<std::chrono::microseconds>(end -
			                                                          start)
			        .count();
			std::cerr << "[MachinePassProfile] " << pass.name << ' ' << micros
			          << " us changed=" << changed << '\n';
		}
	}
}

} // namespace backend::aarch64
