#include "../../include/backend/arm64/passManager.hpp"

#include "../../include/backend/arm64/passes.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
size_t countMachineInstructions(const MachineFunction &function) {
    size_t count = 0;
    for (const auto &block : function.blocks)
        count += block.instrs.size();
    return count;
}
}

void Arm64MachinePassManager::addPass(
    std::unique_ptr<Arm64MachineFunctionPass> pass) {
    passes_.push_back(std::move(pass));
}

bool Arm64MachinePassManager::run(MachineFunction &function, bool localFixedPoint) {
    bool changed = false;
    const bool profilePasses = std::getenv("PROFILE_PASSES") != nullptr;
    for (auto &pass : passes_) {
        int localIterations = 0;
        bool passChangedAny = false;
        do {
            auto start = std::chrono::steady_clock::now();
            bool passChanged = pass->run(function);
            changed |= passChanged;
            passChangedAny |= passChanged;
            if (profilePasses) {
                auto end = std::chrono::steady_clock::now();
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                std::cerr << "[MachinePassProfile] " << function.name << " "
                          << pass->name() << " " << us << " us"
                          << " changed=" << (passChanged ? 1 : 0)
                          << " instrs=" << countMachineInstructions(function)
                          << "\n";
            }
            if (!localFixedPoint || !passChanged)
                break;
            ++localIterations;
        } while (localIterations < 256);

        if (profilePasses && localFixedPoint && passChangedAny && localIterations >= 256) {
            std::cerr << "[MachinePassProfile] WARNING " << function.name << " "
                      << pass->name() << " hit local iteration limit\n";
        }
    }
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
        const bool profilePasses = std::getenv("PROFILE_PASSES") != nullptr;
        cleanup_.run(function, true);
        int rounds = 0;
        while (optimizations_.run(function, true)) {
            ++rounds;
            if (profilePasses)
                std::cerr << "[MachinePipeline] " << function.name
                          << " round=" << rounds
                          << " instrs=" << countMachineInstructions(function)
                          << "\n";
            cleanup_.run(function, true);
        }
        if (profilePasses)
            std::cerr << "[MachinePipeline] " << function.name
                      << " converged_rounds=" << rounds
                      << " instrs=" << countMachineInstructions(function)
                      << "\n";
        cleanup_.run(function, true);
    }
    // Scheduling is terminal: running code motion, instruction combining or
    // address-mode formation afterwards would invalidate its dependency and
    // latency decisions. The scheduler only reorders instructions, so it does
    // not require a deletion-only cleanup pass after it.
    scheduler_.run(function);
}
