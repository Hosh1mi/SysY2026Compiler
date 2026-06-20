#include "include/frontend/ast/ast.hpp"

#include "include/frontend/parser.hpp"

#include "include/mid/ir/irGen.hpp"
#include "include/mid/opt/passManager.hpp"
#include "include/mid/opt/deadCodeEliminate.hpp"
#include "include/mid/opt/linearBlockMerge.hpp"
#include "include/mid/opt/deadStoreEliminate.hpp"
#include "include/mid/opt/correlatedValuePropagation.hpp"
#include "include/mid/opt/tailRecursionEliminate.hpp"
#include "include/mid/opt/mem2reg.hpp"
#include "include/mid/opt/earlyCSE.hpp"
#include "include/mid/opt/instCombine.hpp"
#include "include/mid/opt/sccp.hpp"
#include "include/mid/opt/localCopyPropagation.hpp"
#include "include/mid/opt/inlineExpand.hpp"
#include "include/mid/opt/bitFuncRecognize.hpp"
#include "include/mid/opt/loopSimplify.hpp"
#include "include/mid/opt/lcssa.hpp"
#include "include/mid/opt/simpleLoopUnswitch.hpp"
#include "include/mid/opt/loopRotate.hpp"
#include "include/mid/opt/inductiveRangeCheckElimination.hpp"
#include "include/mid/opt/phiOpSink.hpp"
#include "include/mid/opt/loopInvariantCodeMotion.hpp"
#include "include/mid/opt/loopDeletion.hpp"
#include "include/mid/opt/indVarStrengthReduce.hpp"
#include "include/mid/opt/loopRepFold.hpp"
#include "include/mid/opt/scalarExpandedInterchange.hpp"
#include "include/mid/opt/loopUnroll.hpp"
#include "include/mid/opt/reassociate.hpp"
#include "include/mid/opt/loopVectorize.hpp"
#include "include/mid/opt/loopInterchange.hpp"
#include "include/mid/opt/splitGEP.hpp"
#include "include/mid/opt/CFGSimplify.hpp"
#include "include/mid/opt/unifyExitNodes.hpp"
#include "include/mid/opt/globalScalarPromotion.hpp"
#include "include/mid/opt/gvn.hpp"
#include "include/mid/opt/lateValueCleanup.hpp"
#include "include/mid/opt/semanticMarkerStamp.hpp"
#include "include/mid/opt/codeSink.hpp"
#include "include/mid/opt/tailDuplication.hpp"

#include "include/backend/arm64/codegen.hpp"
#include "include/backend/arm64/parallelRuntime.hpp"
#include "include/backend/riscv/codegen.hpp"
#include "include/mid/opt/parallelizeLoops.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

// 目标后端架构：直接在源码中以变量指明（不经命令行参数）。切换后端只需改这里。
enum class TargetArch { Arm64, Riscv };
constexpr TargetArch kTargetArch = TargetArch::Riscv;

struct DriverOptions {
    char *input = nullptr;
    bool printIR = false;
    bool printAsm = false;
    std::string output = "-";
    int optLevel = 0;
    bool dumpIR = false;
    bool verifyIR = false;
    bool disablePeephole = false;
    bool disableSchedule = false;
    bool disablePreSchedule = false;
    bool dumpMachineInstr = false;
    bool dumpPreMachineInstr = false;
};

static bool parseOptLevel(const std::string &arg, int argc, char **argv,
                          int &index, int &optLevel) {
    std::string value;
    if (arg.size() > 2) {
        value = arg.substr(2);
    } else if (index + 1 < argc && argv[index + 1][0] != '-') {
        value = argv[++index];
    } else {
        optLevel = 1;
        return true;
    }

    if (value.size() != 1 || !std::isdigit(static_cast<unsigned char>(value[0]))) {
        std::cerr << "Invalid optimization level: -O" << value << "\n";
        return false;
    }
    optLevel = value[0] - '0';
    return true;
}

static bool parseArgs(int argc, char **argv, DriverOptions &options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-S") {
            options.printAsm = true;
            options.printIR = false;
        } else if (arg == "-c") {
            options.printIR = true;
            options.printAsm = false;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "-o requires a filename\n";
                return false;
            }
            options.output = argv[++i];
        } else if (arg.rfind("-O", 0) == 0) {
            if (!parseOptLevel(arg, argc, argv, i, options.optLevel))
                return false;
        } else if (arg == "--dump-ir") {
            options.dumpIR = true;
        } else if (arg == "--verify-ir") {
            options.verifyIR = true;
        } else if (arg == "--fno-peephole") {
            options.disablePeephole = true;
        } else if (arg == "--fno-schedule") {
            options.disableSchedule = true;
        } else if (arg == "--fno-pre-schedule") {
            options.disablePreSchedule = true;
        } else if (arg == "--dump-machine-instr") {
            options.dumpMachineInstr = true;
        } else if (arg == "--dump-pre-machine-instr") {
            options.dumpPreMachineInstr = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        } else {
            options.input = argv[i];
        }
    }

    if (!options.input) {
        std::cerr << "No input file provided.\n";
        return false;
    }
    return true;
}

static bool openOutput(const DriverOptions &options,
                       std::ofstream &fout,
                       std::ostream *&out) {
    out = &std::cout;
    if (options.output == "-")
        return true;

    fout.open(options.output);
    if (!fout.is_open()) {
        std::cerr << "failed to open output file: " << options.output << std::endl;
        return false;
    }
    out = &fout;
    return true;
}

static void addCanonicalCleanup(PassManager &pm) {
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<LinearBlockMerge>());
    pm.addPass(std::make_unique<SCCP>());
    pm.addPass(std::make_unique<InstCombine>());
    pm.addPass(std::make_unique<DeadStoreEliminate>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<LinearBlockMerge>());
}

static void addCfgCleanup(PassManager &pm) {
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<CFGSimplify>());
}

static void addCorrelatedCleanup(PassManager &pm) {
    pm.addPass(std::make_unique<CorrelatedValuePropagation>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
}

static void addDeepCleanup(PassManager &pm) {
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    addCanonicalCleanup(pm);
}

static void addSsaPreparation(PassManager &pm) {
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<Mem2Reg>());
    pm.addPass(std::make_unique<EarlyCSE>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<TailRecursionEliminate>());
}

static void addScalarNormalization(PassManager &pm) {
    pm.addPass(std::make_unique<ScalarExpandedInterchange>());
    pm.addPass(std::make_unique<Reassociate>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<LocalCopyPropagation>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CodeSink>());
}

static void addInterproceduralAndGlobals(PassManager &pm) {
    pm.addPass(std::make_unique<BitFuncRecognize>());
    pm.addPass(std::make_unique<InlineExpand>());
    pm.addPass(std::make_unique<LocalCopyPropagation>());
    // First stamp: make FnPure/FnReadOnly visible before the global and
    // scalar cleanups that can profit from post-inline call semantics.
    pm.addPass(std::make_unique<SemanticMarkerStamp>());
    // Re-run EarlyCSE after stamping so BasicAA can reuse the persisted
    // function semantics for pure/readonly call reasoning.
    pm.addPass(std::make_unique<EarlyCSE>());
    pm.addPass(std::make_unique<GlobalScalarPromotion>());
    pm.addPass(std::make_unique<Mem2Reg>());
    addDeepCleanup(pm);
}

// ARM64 后端中端管线。目标相关 pass 在这里显式列出，避免目标能力通过
// 布尔参数间接拼装而导致 ARM/RISC-V 管线错配。
static void buildArm64Pipeline(PassManager &pm, int optLevel) {
    if (optLevel < 1)
        return;

    addSsaPreparation(pm);
    addScalarNormalization(pm);
    addInterproceduralAndGlobals(pm);
    addCorrelatedCleanup(pm);
    pm.addPass(std::make_unique<SemanticMarkerStamp>());

    pm.addPass(std::make_unique<CFGSimplify>());
    pm.beginRepeatGroup(/*maxRounds=*/8);
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LCSSA>());
    pm.addPass(std::make_unique<SimpleLoopUnswitch>());
    pm.addPass(std::make_unique<LoopRotate>());
    pm.addPass(std::make_unique<PhiOpSink>());
    pm.addPass(std::make_unique<inductiveRangeCheckElimination>());
    pm.addPass(std::make_unique<LICM>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<LoopDeletion>());
    pm.endRepeatGroup();
    pm.addPass(std::make_unique<LoopInterchange>());
    pm.addPass(std::make_unique<ParallelizeLoops>());
    pm.addPass(std::make_unique<LoopVectorize>());
    pm.addPass(std::make_unique<IndVarStrengthReduce>());
    pm.addPass(std::make_unique<LoopRepFold>());
    pm.addPass(std::make_unique<LoopUnroll>());
    pm.addPass(std::make_unique<LoopVectorize>());
    pm.addPass(std::make_unique<SplitGEP>());
    addDeepCleanup(pm);

    pm.addPass(std::make_unique<GVN>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CodeSink>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<TailDuplication>());
    pm.addPass(std::make_unique<UnifyExitNodes>());
    addCorrelatedCleanup(pm);
    pm.addPass(std::make_unique<LateValueCleanup>());
}

// RISC-V 后端中端管线。BOOM v3 无 SIMD，且当前没有 RISC-V 并行
// runtime，因此本函数不加入 LoopVectorize 或 ParallelizeLoops。
static void buildRiscvPipeline(PassManager &pm, int optLevel) {
    if (optLevel < 1)
        return;

    addSsaPreparation(pm);
    addScalarNormalization(pm);
    addInterproceduralAndGlobals(pm);
    addCorrelatedCleanup(pm);
    pm.addPass(std::make_unique<SemanticMarkerStamp>());

    pm.addPass(std::make_unique<CFGSimplify>());
    pm.beginRepeatGroup(/*maxRounds=*/8);
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LCSSA>());
    pm.addPass(std::make_unique<SimpleLoopUnswitch>());
    pm.addPass(std::make_unique<LoopRotate>());
    pm.addPass(std::make_unique<PhiOpSink>());
    pm.addPass(std::make_unique<inductiveRangeCheckElimination>());
    pm.addPass(std::make_unique<LICM>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<LoopDeletion>());
    pm.endRepeatGroup();
    pm.addPass(std::make_unique<LoopInterchange>());
    pm.addPass(std::make_unique<IndVarStrengthReduce>());
    pm.addPass(std::make_unique<LoopRepFold>());
    pm.addPass(std::make_unique<LoopUnroll>());
    pm.addPass(std::make_unique<SplitGEP>());
    addDeepCleanup(pm);

    pm.addPass(std::make_unique<GVN>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CodeSink>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<TailDuplication>());
    pm.addPass(std::make_unique<UnifyExitNodes>());
    addCorrelatedCleanup(pm);
    pm.addPass(std::make_unique<LateValueCleanup>());
}

static void buildOptimizationPipeline(PassManager &pm, int optLevel) {
    if (kTargetArch == TargetArch::Riscv)
        buildRiscvPipeline(pm, optLevel);
    else
        buildArm64Pipeline(pm, optLevel);
}

template <class CodeGen>
static void configureBackend(CodeGen &codegen, const DriverOptions &options) {
    bool enableOptimizations = options.optLevel >= 1;
    codegen.setEnableRegAlloc(enableOptimizations);
    codegen.setNoPeephole(!enableOptimizations || options.disablePeephole);
    codegen.setNoSchedule(!enableOptimizations || options.disableSchedule);
    codegen.setNoPreSchedule(!enableOptimizations || options.disablePreSchedule);
    codegen.setDumpMachineInstr(options.dumpMachineInstr);
    codegen.setDumpPreMachineInstr(options.dumpPreMachineInstr);
}

static Function *calledFunction(Instruction *inst) {
    auto *call = dynamic_cast<CallInst *>(inst);
    if (!call || call->num_ops_ == 0)
        return nullptr;
    return dynamic_cast<Function *>(call->get_operand(call->num_ops_ - 1));
}

static bool hasParallelForCall(Module *module) {
    for (auto *func : module->function_list_) {
        if (func->is_declaration())
            continue;
        for (auto *bb : func->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                Function *callee = calledFunction(inst);
                if (callee && callee->name_ == "__sysy_parallel_for")
                    return true;
            }
        }
    }
    return false;
}

static std::vector<int> parallelBodyIds(Module *module) {
    std::vector<int> ids;
    const std::string prefix = "__sysy_par_body_";
    for (auto *func : module->function_list_) {
        if (func->name_.rfind(prefix, 0) != 0)
            continue;
        std::string suffix = func->name_.substr(prefix.size());
        if (suffix.empty())
            continue;
        bool numeric = true;
        for (char ch : suffix) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                numeric = false;
                break;
            }
        }
        if (numeric)
            ids.push_back(std::stoi(suffix));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

extern unique_ptr<CompUnitAST> root;
extern int yyparse();
extern FILE *yyin;

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "No input file.\n";
        return -1;
    }

    DriverOptions options;
    if (!parseArgs(argc, argv, options))
        return -1;

    yyin = fopen(options.input, "r");
    if (!yyin) {
        std::cerr << "Failed to open input file: " << options.input << "\n";
        return -1;
    }

    yyparse();

    GenIR genIR;
    root->accept(genIR);
    std::unique_ptr<Module> m = genIR.getModule();

    PassManager pm;
    pm.setDumpIR(options.dumpIR);
    pm.setVerifyIR(options.verifyIR);
    buildOptimizationPipeline(pm, options.optLevel);
    pm.run(m.get());

    std::ofstream fout;
    std::ostream *out = nullptr;
    if (!openOutput(options, fout, out))
        return -1;

    if (options.printAsm || options.dumpMachineInstr) {
        if (kTargetArch == TargetArch::Riscv) {
            RiscvCodeGen codegen(m.get(), *out);
            configureBackend(codegen, options);
            codegen.generate();
        } else {
            Arm64CodeGen codegen(m.get(), *out);
            configureBackend(codegen, options);
            codegen.generate();
            // 并行 runtime + 手写 dispatch（见 parallelizeLoops.cpp 说明）
            bool hasParallel = hasParallelForCall(m.get());
            std::vector<int> bodyIds = parallelBodyIds(m.get());
            if (hasParallel) {
                *out << "\n\t.text\n\t.align 2\n"
                     << "\t.global __sysy_par_dispatch\n"
                     << "__sysy_par_dispatch:\n";
                for (int id : bodyIds)
                    *out << "\tcmp w0, #" << id << "\n"
                         << "\tb.eq .Lsysy_disp_" << id << "\n";
                *out << "\tret\n";
                for (int id : bodyIds)
                    *out << ".Lsysy_disp_" << id << ":\n"
                         << "\tmov w0, w1\n"
                         << "\tmov w1, w2\n"
                         << "\tb __sysy_par_body_" << id << "\n";
                *out << kSysyParallelRuntimeAsm;
            }
        }
    } else if (options.printIR) {
        *out << m->print();
    }

    return 0;
}
