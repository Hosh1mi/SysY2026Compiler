// This file executes the linear MachineFunction pass list and provides the
// common diagnostics used by the code-generation driver.
#include "backend/machine_pass_manager.hpp"

#include "backend/verifier.hpp"

#include <cstdlib>
#include <iostream>

namespace backend::aarch64 {

MachineFunctionPassManager::MachineFunctionPassManager(
    const MachineVerifier &verifier, bool verifyEachPass, bool dump)
    : verifier_(verifier), verifyEachPass_(verifyEachPass), dump_(dump) {}

void MachineFunctionPassManager::addPass(std::string name,
                                         PassRunner runner) {
	passes_.push_back(PassEntry{std::move(name), runner});
}

void MachineFunctionPassManager::run(MachineFunction &function) const {
	const bool trace = std::getenv("TRACE_MACHINE_PIPELINE") != nullptr;
	const bool dump =
	    dump_ || std::getenv("DUMP_MACHINE_PIPELINE") != nullptr;

	for (const PassEntry &pass : passes_) {
		if (trace)
			std::cerr << "[MachinePipeline] " << pass.name << '\n';
		if (dump) {
			std::cerr << "; *** MIR before " << pass.name << " ***\n";
			printMachineIR(function, std::cerr);
		}

		pass.runner(function);

		if (verifyEachPass_)
			verifier_.verifyOrAbort(function, pass.name);
		if (dump) {
			std::cerr << "; *** MIR after " << pass.name << " ***\n";
			printMachineIR(function, std::cerr);
		}
	}
}

} // namespace backend::aarch64
