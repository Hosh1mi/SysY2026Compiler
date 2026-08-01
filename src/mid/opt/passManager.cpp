#include "../../include/mid/opt/passManager.hpp"
#include "../../include/mid/analysis/loopVerify.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::beginRepeatGroup(int maxRounds) {
    RepeatGroup g;
    g.begin = passes.size();
    g.end   = passes.size();
    g.maxRounds = maxRounds;
    groups_.push_back(g);
}

void PassManager::endRepeatGroup() {
    groups_.back().end = passes.size();
}

static size_t countInstructions(Module *module) {
    size_t n = 0;
    for (auto *func : module->function_list_) {
        for (auto *bb : func->basic_blocks_)
            n += bb->instr_list_.size();
    }
    return n;
}

static bool isLoopTransformPass(const std::string &name) {
    static const std::unordered_set<std::string> loopPasses = {
        "ScalarExpansion",
        "LoopDistribution",
        "LoopInvariantReduction",
        "LoopSimplify",
        "LCSSA",
        "IndVarSimplify",
        "SimpleLoopUnswitch",
        "LoopRotate",
        "PhiOpSink",
        "inductiveRangeCheckElimination",
        "LICM",
        "LoopDeletion",
        "TriangularRemapSourceCompose",
        "TriangularPanelize",
        "LoopFusion",
        "LoopInterchange",
        "ParallelizeLoops",
        "LoopResetPointElimination",
        "LoopVectorize",
        "IndVarStrengthReduce",
        "LoopRepFold",
        "LoopUnroll",
        "Hira",
    };
    return loopPasses.count(name) != 0;
}

static bool isLoopVerifyStrictEnabled() {
    return std::getenv("LOOP_VERIFY_STRICT") != nullptr;
}

static bool establishesLoopStructure(const std::string &name) {
    return name == "LoopSimplify";
}

static bool establishesLCSSA(const std::string &name) {
    return name == "LCSSA";
}

static int loopVerifyLevelAfterPass(const std::string &name,
                                    bool loopFormReady) {
    if (establishesLCSSA(name) && loopFormReady)
        return 3;
    if (establishesLoopStructure(name))
        return 2;
    return 1;
}

static bool canStrictlyVerifyLoopFormAfterPass(const std::string &name,
                                               bool loopFormReady) {
    return establishesLoopStructure(name) ||
           (establishesLCSSA(name) && loopFormReady);
}

PreservedAnalyses PassManager::runSinglePass(Pass &pass, Module *module) {
    const bool profilePasses = std::getenv("PROFILE_PASSES") != nullptr;
    const std::string passName = pass.name();
    const size_t beforeInsts = profilePasses ? countInstructions(module) : 0;
    auto start = std::chrono::steady_clock::now();
    if (dump_ir_) {
        std::cerr << "; === IR Before " << passName << " ===\n"
                  << module->print() << "\n";
    }

    PreservedAnalyses preserved = pass.execute(module, analyses_);

    if (verify_ir_) {
        const std::string context = "after " + passName;
        module->verify(context);
        if (isLoopTransformPass(passName)) {
            const bool strictLoopVerify = isLoopVerifyStrictEnabled();
            const int loopVerifyLevel =
                loopVerifyLevelAfterPass(passName, loop_form_ready_);
            std::cerr << "[VERIFY] " << context << ": ok\n";
            verifyLoopForms(module, loopVerifyLevel, context,
                            /*warnOnly=*/!strictLoopVerify ||
                                !canStrictlyVerifyLoopFormAfterPass(
                                    passName, loop_form_ready_),
                            /*reportClean=*/true);
        }
    }

    if (establishesLoopStructure(passName)) {
        loop_form_ready_ = true;
    } else if (!establishesLCSSA(passName) && !preserved.preservesAll()) {
        loop_form_ready_ = false;
    }

    analyses_.invalidate(module, preserved);

    if (dump_ir_) {
        std::cerr << "; === IR After " << passName << " ===\n"
                  << module->print() << "\n";
    }
    if (profilePasses) {
        auto end = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        const size_t afterInsts = countInstructions(module);
        std::cerr << "[PassProfile] " << passName
                  << " " << us << " us"
                  << " insts=" << afterInsts
                  << " delta=" << static_cast<long long>(afterInsts) -
                                    static_cast<long long>(beforeInsts)
                  << "\n";
    }
    return preserved;
}

void PassManager::verifyRepeatGroupExit(Module *module, bool completedNormally) {
    if (!verify_ir_)
        return;

    const std::string context = "after loop repeat group";
    module->verify(context);
    std::cerr << "[VERIFY] " << context << ": ok\n";

    const bool strictLoopVerify =
        isLoopVerifyStrictEnabled() && completedNormally;
    verifyLoopForms(module, completedNormally ? 3 : 1, context,
                    /*warnOnly=*/!strictLoopVerify,
                    /*reportClean=*/true);
}

void PassManager::run(Module *module) {
    const bool tracePipeline =
        dump_ir_ || std::getenv("DEBUG_LOOP_PIPELINE") != nullptr;

    size_t gi = 0; 
    for (size_t i = 0; i < passes.size();) {
        if (gi < groups_.size() && groups_[gi].begin == i &&
            groups_[gi].end > groups_[gi].begin) {
            const RepeatGroup &g = groups_[gi];
            const size_t entryInsts = countInstructions(module);
            const size_t instBudget = entryInsts * 2 + 1024;

            int maxRounds = g.maxRounds;
            if (const char *ov = std::getenv("LOOP_PIPELINE_MAX_ROUNDS"))
                maxRounds = std::atoi(ov);

            bool completedNormally = false;
            for (int round = 1; round <= maxRounds; round++) {
                bool roundChanged = false;
                std::string changedList;
                for (size_t j = g.begin; j < g.end; j++) {
                    PreservedAnalyses preserved =
                        runSinglePass(*passes[j], module);
                    if (passes[j]->convergenceRelevant() &&
                        !preserved.preservesAll()) {
                        roundChanged = true;
                        if (tracePipeline) {
                            if (!changedList.empty()) changedList += ", ";
                            changedList += passes[j]->name();
                        }
                    }
                }
                if (tracePipeline)
                    std::cerr << "[LoopPipeline] round " << round
                              << (roundChanged ? " changed: {" + changedList + "}"
                                               : " converged")
                              << "\n";
                if (!roundChanged)
                {
                    completedNormally = true;
                    break;
                }
                size_t now = countInstructions(module);
                if (now > instBudget) {
                    std::cerr << "[LoopPipeline] WARNING: instruction count "
                              << now << " exceeds budget " << instBudget
                              << " (entry " << entryInsts
                              << "), stopping repeat group\n";
                    break;
                }
            }
            verifyRepeatGroupExit(module, completedNormally);
            i = g.end;
            gi++;
            continue;
        }
        runSinglePass(*passes[i], module);
        i++;
    }
}
