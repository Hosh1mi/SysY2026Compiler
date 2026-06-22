#include "../../include/backend/arm64/passManager.hpp"

#include "../../include/backend/arm64/passes.hpp"

#include <utility>

void Arm64MachinePassManager::addPass(
    std::unique_ptr<Arm64MachineFunctionPass> pass) {
    passes_.push_back(std::move(pass));
}

bool Arm64MachinePassManager::run(MachineFunction &function) {
    bool changed = false;
    for (auto &pass : passes_)
        changed |= pass->run(function);
    return changed;
}

Arm64MachineOptimizationPipeline::Arm64MachineOptimizationPipeline(
    bool enableOptimizations, bool enableScheduling)
    : enableOptimizations_(enableOptimizations) {
    cleanup_.addPass(std::make_unique<Arm64MachineDCEPass>());

    optimizations_.addPass(std::make_unique<Arm64CopyPropagationPass>());
    optimizations_.addPass(std::make_unique<Arm64InstructionCombinePass>());
    optimizations_.addPass(std::make_unique<Arm64CodeMotionPass>());
    optimizations_.addPass(std::make_unique<Arm64MemoryOptimizationPass>());
    optimizations_.addPass(std::make_unique<Arm64BranchOptimizationPass>());
    optimizations_.addPass(std::make_unique<Arm64CanonicalizationPass>());
    optimizations_.addPass(std::make_unique<Arm64LocalCSEPass>());
    optimizations_.addPass(std::make_unique<Arm64PeepholePass>());

    if (enableScheduling)
        scheduler_.addPass(std::make_unique<Arm64PostRASchedulerPass>());
}

void Arm64MachineOptimizationPipeline::run(MachineFunction &function) {
    if (enableOptimizations_) {
        cleanup_.run(function);
        while (optimizations_.run(function))
            cleanup_.run(function);
        cleanup_.run(function);
    }
    // Scheduling is terminal: running code motion, instruction combining or
    // address-mode formation afterwards would invalidate its dependency and
    // latency decisions. The scheduler only reorders instructions, so it does
    // not require a deletion-only cleanup pass after it.
    scheduler_.run(function);
}
