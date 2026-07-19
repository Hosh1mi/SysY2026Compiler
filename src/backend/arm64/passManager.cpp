#include "../../include/backend/arm64/passManager.hpp"

#include "../../include/backend/arm64/passes.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
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

bool Arm64MachinePassManager::runPass(Arm64MachineFunctionPass &pass,
                                      MachineFunction &function,
                                      MachineAnalysisManager &analyses,
                                      bool toFixedPoint) {
    bool changed = false;
    const bool profilePasses = std::getenv("PROFILE_PASSES") != nullptr;
    std::set<std::string> seenStates;
    int iteration = 0;
    while (true) {
        if (toFixedPoint) {
            std::string state = printMachineFunction(function);
            if (!seenStates.insert(std::move(state)).second) {
                throw std::logic_error(std::string(pass.name()) +
                                       " entered a machine-IR rewrite cycle");
            }
        }

        auto start = std::chrono::steady_clock::now();
        bool passChanged = pass.run(function, analyses);
        changed |= passChanged;
        if (passChanged)
            analyses.invalidate();
        if (profilePasses) {
            auto end = std::chrono::steady_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            std::cerr << "[MachinePassProfile] " << function.name << " "
                      << pass.name() << " " << us << " us"
                      << " changed=" << (passChanged ? 1 : 0)
                      << " iteration=" << iteration
                      << " instrs=" << countMachineInstructions(function)
                      << "\n";
        }
        if (!toFixedPoint || !passChanged)
            break;
        ++iteration;
    }
    return changed;
}

bool Arm64MachinePassManager::runOnce(MachineFunction &function,
                                      MachineAnalysisManager &analyses) {
    bool changed = false;
    for (auto &pass : passes_)
        changed |= runPass(*pass, function, analyses, false);
    return changed;
}

bool Arm64MachinePassManager::runEachToFixedPoint(
    MachineFunction &function, MachineAnalysisManager &analyses) {
    bool changed = false;
    for (auto &pass : passes_)
        changed |= runPass(*pass, function, analyses, true);
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
    optimizations_.addPass(std::make_unique<Arm64CFGOptimizationPass>());
    optimizations_.addPass(std::make_unique<Arm64BitBranchOptimizationPass>());
    optimizations_.addPass(std::make_unique<Arm64CanonicalizationPass>());
    optimizations_.addPass(std::make_unique<Arm64LocalCSEPass>());
    optimizations_.addPass(std::make_unique<Arm64PeepholePass>());

    if (enableScheduling)
        scheduler_.addPass(std::make_unique<Arm64PostRASchedulerPass>());
}

void Arm64MachineOptimizationPipeline::run(MachineFunction &function) {
    MachineAnalysisManager analyses;
    if (enableOptimizations_) {
        const bool profilePasses = std::getenv("PROFILE_PASSES") != nullptr;
        cleanup_.runEachToFixedPoint(function, analyses);
        std::set<std::string> seenStates;
        int rounds = 0;
        while (true) {
            std::string state = printMachineFunction(function);
            if (!seenStates.insert(std::move(state)).second)
                throw std::logic_error("ARM64 machine optimization pipeline entered a rewrite cycle");

            bool changed = optimizations_.runEachToFixedPoint(function, analyses);
            changed |= cleanup_.runEachToFixedPoint(function, analyses);
            if (profilePasses)
                std::cerr << "[MachinePipeline] " << function.name
                          << " round=" << rounds
                          << " instrs=" << countMachineInstructions(function)
                          << "\n";
            if (!changed)
                break;
            ++rounds;
        }
        if (profilePasses)
            std::cerr << "[MachinePipeline] " << function.name
                      << " converged_rounds=" << rounds
                      << " instrs=" << countMachineInstructions(function)
                      << "\n";
    }
    // Scheduling is terminal: running code motion, instruction combining or
    // address-mode formation afterwards would invalidate its dependency and
    // latency decisions. The scheduler only reorders instructions, so it does
    // not require a deletion-only cleanup pass after it.
    scheduler_.runOnce(function, analyses);
}
