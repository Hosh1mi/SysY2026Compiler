#include "../../include/mid/opt/passManager.hpp"
#include "../../include/mid/analysis/loopVerify.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/scalarEvolution.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::beginRepeatGroup(int maxRounds, bool trackAllChanges,
                                   bool verifyLoopForm) {
    RepeatGroup g;
    g.begin = passes.size();
    g.end   = passes.size();
    g.maxRounds = maxRounds;
    g.trackAllChanges = trackAllChanges;
    g.verifyLoopForm = verifyLoopForm;
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
        "LoopInvariantReduction",
        "LastIterationElimination",
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
        "LoopSkewing",
        "LoopInterchange",
        "ParallelizeLoops",
        "LoopResetPointElimination",
        "LoopVectorize",
        "IndVarStrengthReduce",
        "LoopRepFold",
        "LoopUnroll",
        "LoopPeel",
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

// DUMP_IR_PASS=PassA,PassB restricts --dump-ir to the listed pass names.
// Without the env var, --dump-ir still dumps every pass.
static bool passNameInCSV(const char *csv, const std::string &passName) {
    if (!csv || !*csv)
        return false;
    if (std::strcmp(csv, "*") == 0)
        return true;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item == passName)
            return true;
    }
    return false;
}

static bool dumpIRPassMatches(const std::string &passName) {
    const char *filter = std::getenv("DUMP_IR_PASS");
    if (!filter || !*filter)
        return true;
    return passNameInCSV(filter, passName);
}

// When DUMP_IR_GATE=PassName is set, --dump-ir only emits for DUMP_IR_PASS
// matches at/after that gate pass has begun.  Useful to skip early SCCP/DCE
// dumps on huge pre-cleanup IR.
static bool dumpIRGateAllows(const std::string &passName) {
    const char *gate = std::getenv("DUMP_IR_GATE");
    if (!gate || !*gate)
        return true;
    static bool gateSeen = false;
    if (passName == gate)
        gateSeen = true;
    return gateSeen;
}

static bool shouldDumpIRForPass(bool dumpIR, const std::string &passName) {
    return dumpIR && dumpIRGateAllows(passName) && dumpIRPassMatches(passName);
}

static void dumpSCEVSnapshot(Module *module, const std::string &when,
                             const std::string &passName) {
    const char *filter = std::getenv("DUMP_SCEV_PASS");
    if (!passNameInCSV(filter, passName))
        return;

    std::cerr << "; === SCEV " << when << " " << passName << " ===\n";
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        LoopInfo LI;
        LI.analyze(func);
        ScalarEvolution SE(LI);
        std::cerr << "; function @" << func->name_ << "\n";
        for (auto &loopPtr : LI.allLoops()) {
            Loop *loop = loopPtr.get();
            if (!loop || !loop->header)
                continue;
            auto constTC = SE.getConstantTripCount(loop);
            std::cerr << "; loop header=" << loop->header->name_
                      << " depth=" << loop->depth
                      << " blocks=";
            for (size_t i = 0; i < loop->blocksOrdered.size(); ++i) {
                if (i) std::cerr << ",";
                std::cerr << loop->blocksOrdered[i]->name_;
            }
            if (constTC) {
                // Constant trip count here means number of latch executions /
                // body iterations for the recognized induction form.
                std::cerr << " tripCount=" << *constTC
                          << " backedgeTaken="
                          << (*constTC > 0 ? *constTC - 1 : 0);
            } else {
                std::cerr << " tripCount=<none> backedgeTaken=<none>";
            }
            const SCEV *trip = SE.getTripCount(loop);
            std::cerr << " tripSCEV=" << (trip ? trip->print() : "<null>");
            if (loop->tripCount)
                std::cerr << " boundVal="
                          << (loop->tripCount->name_.empty()
                                  ? "<anon>"
                                  : loop->tripCount->name_);
            if (loop->controlInduction.phi)
                std::cerr << " iv=" << loop->controlInduction.phi->name_;
            if (loop->controlInduction.constantStep)
                std::cerr << " step=" << *loop->controlInduction.constantStep;
            std::cerr << "\n";
            for (auto *inst : loop->header->instr_list_) {
                if (!inst->is_phi())
                    break;
                const SCEV *s = SE.getSCEV(inst);
                std::cerr << ";   header.phi %" << inst->name_
                          << " => " << (s ? s->print() : "<null>") << "\n";
            }
        }
    }
}

PreservedAnalyses PassManager::runSinglePass(Pass &pass, Module *module) {
    const bool profilePasses = std::getenv("PROFILE_PASSES") != nullptr;
    const std::string passName = pass.name();
    if (std::getenv("TRACE_PASS_PIPELINE"))
        std::cerr << "[PipelinePass] " << passName << "\n";
    const size_t beforeInsts = profilePasses ? countInstructions(module) : 0;
    auto start = std::chrono::steady_clock::now();
    const bool dumpThis = shouldDumpIRForPass(dump_ir_, passName);
    if (dumpThis) {
        std::cerr << "; === IR Before " << passName << " ===\n"
                  << module->print() << "\n";
    }
    dumpSCEVSnapshot(module, "Before", passName);

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

    if (dumpThis) {
        std::cerr << "; === IR After " << passName << " ===\n"
                  << module->print() << "\n";
    }
    dumpSCEVSnapshot(module, "After", passName);
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
                    if ((g.trackAllChanges ||
                         passes[j]->convergenceRelevant()) &&
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
            if (g.verifyLoopForm)
                verifyRepeatGroupExit(module, completedNormally);
            i = g.end;
            gi++;
            continue;
        }
        runSinglePass(*passes[i], module);
        i++;
    }
}
