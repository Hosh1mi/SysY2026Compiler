#include "../../include/mid/opt/passManager.hpp"
#include "../../include/mid/analysis/loopVerify.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/scalarEvolution.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/opt/lcssa.hpp"
#include "../../include/mid/opt/loopSimplify.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

void PassManager::beginFixedPointGroup(bool runOnClean) {
    if (building_group_)
        throw std::logic_error("nested pass groups are not supported");
    building_group_ = true;
    FixedPointGroup g;
    g.begin = passes.size();
    g.end   = passes.size();
    g.runOnClean = runOnClean;
    fixed_point_groups_.push_back(g);
}

void PassManager::endFixedPointGroup() {
    if (!building_group_)
        throw std::logic_error("pass group end without matching begin");
    fixed_point_groups_.back().end = passes.size();
    building_group_ = false;
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

static int loopVerifyLevelAfterPass(const Pass &pass,
                                    bool simplifiedBefore) {
    if (pass.establishedLoopForm() == LoopForm::LCSSA && simplifiedBefore)
        return 3;
    if (pass.establishedLoopForm() == LoopForm::Simplified)
        return 2;
    return 1;
}

static bool canStrictlyVerifyLoopFormAfterPass(const Pass &pass,
                                               bool simplifiedBefore) {
    return pass.establishedLoopForm() == LoopForm::Simplified ||
           (pass.establishedLoopForm() == LoopForm::LCSSA &&
            simplifiedBefore);
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

PassRunResult PassManager::runSinglePass(Pass &pass, Module *module) {
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

    PassRunResult result = pass.runPass(module, analyses_);
    const PreservedAnalyses &preserved = result.preserved;
    const bool simplifiedBefore =
        static_cast<int>(loop_form_) >=
        static_cast<int>(LoopForm::Simplified);

    if (verify_ir_) {
        const std::string context = "after " + passName;
        module->verify(context);
        if (isLoopTransformPass(passName)) {
            const bool strictLoopVerify = isLoopVerifyStrictEnabled();
            const int loopVerifyLevel =
                loopVerifyLevelAfterPass(pass, simplifiedBefore);
            std::cerr << "[VERIFY] " << context << ": ok\n";
            verifyLoopForms(module, loopVerifyLevel, context,
                            /*warnOnly=*/!strictLoopVerify ||
                                !canStrictlyVerifyLoopFormAfterPass(
                                    pass, simplifiedBefore),
                            /*reportClean=*/true);
        }
    }

    if (pass.establishedLoopForm() != LoopForm::None)
        loop_form_ = pass.establishedLoopForm();
    else if (result.changed)
        loop_form_ = LoopForm::None;

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
    return result;
}

void PassManager::ensureLoopForm(LoopForm required, Module *module) {
    if (static_cast<int>(loop_form_) >= static_cast<int>(required))
        return;
    if (static_cast<int>(loop_form_) <
        static_cast<int>(LoopForm::Simplified)) {
        LoopSimplify simplify;
        runSinglePass(simplify, module);
    }
    if (required == LoopForm::LCSSA && loop_form_ != LoopForm::LCSSA) {
        LCSSA lcssa;
        runSinglePass(lcssa, module);
    }
}

void PassManager::run(Module *module) {
    const bool tracePipeline =
        dump_ir_ || std::getenv("DEBUG_LOOP_PIPELINE") != nullptr ||
        std::getenv("TRACE_PASS_PIPELINE") != nullptr;

    bool irChangedSinceFixedPoint = false;
    size_t gi = 0; 
    for (size_t i = 0; i < passes.size();) {
        if (gi < fixed_point_groups_.size() &&
            fixed_point_groups_[gi].begin == i &&
            fixed_point_groups_[gi].end > fixed_point_groups_[gi].begin) {
            const FixedPointGroup &g = fixed_point_groups_[gi];
            if (!irChangedSinceFixedPoint && !g.runOnClean) {
                if (tracePipeline)
                    std::cerr << "[FixedPointPipeline] skipped: no pending IR "
                                 "changes\n";
                i = g.end;
                gi++;
                continue;
            }

            for (int round = 1;; round++) {
                bool roundChanged = false;
                std::string changedList;
                for (size_t j = g.begin; j < g.end; j++) {
                    ensureLoopForm(passes[j]->requiredLoopForm(), module);
                    PassRunResult result =
                        runSinglePass(*passes[j], module);
                    if (result.changed) {
                        roundChanged = true;
                        if (tracePipeline) {
                            if (!changedList.empty()) changedList += ", ";
                            changedList += passes[j]->name();
                        }
                    }
                }
                if (tracePipeline)
                    std::cerr << "[FixedPointPipeline] round " << round
                              << (roundChanged ? " changed: {" + changedList + "}"
                                               : " converged")
                              << "\n";
                if (!roundChanged)
                    break;
            }
            irChangedSinceFixedPoint = false;
            i = g.end;
            gi++;
            continue;
        }
        ensureLoopForm(passes[i]->requiredLoopForm(), module);
        PassRunResult result = runSinglePass(*passes[i], module);
        irChangedSinceFixedPoint |= result.changed;
        i++;
    }
}
