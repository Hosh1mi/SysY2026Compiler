#include "include/frontend/ast/ast.hpp"
#include "include/frontend/ast/astPrinter.hpp"
#include "include/frontend/parser.hpp"
#include "include/frontend/validation.hpp"

#include "include/mid/ir/irGen.hpp"
#include "include/mid/runtime/summableModSumRuntime.hpp"
#include "include/mid/opt/passManager.hpp"
#include "include/mid/opt/optPasses.hpp"

#include "include/backend/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

struct DriverOptions {
    char *input = nullptr;
    bool printIR = false;
    bool printAsm = false;
    std::string output = "-";
    int optLevel = 0;
    bool dumpIR = false;
    bool dumpSCEV = false;
    bool dumpAST = false;
    bool verifyIR = false;
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
        } else if (arg == "--dump-scev") {
            options.dumpSCEV = true;
        } else if (arg == "--dump-ast") {
            options.dumpAST = true;
        } else if (arg == "--verify-ir") {
            options.verifyIR = true;
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

static void addScalarSimplifyPasses(PassManager &pm) {
    pm.addPass(std::make_unique<InstCombine>());
    pm.addPass(std::make_unique<SCCP>());
    pm.addPass(std::make_unique<CFGSimplify>(CFGSimplifyMode::Lite));
    pm.addPass(std::make_unique<DeadCodeEliminate>());
}

static void addScalarSimplifyClosure(PassManager &pm,
                                     bool runOnClean = false) {
    pm.addFixedPointGroup(
        [](PassManager &group) { addScalarSimplifyPasses(group); },
        runOnClean);
}

static void addLightweightMemoryClosure(PassManager &pm) {
    pm.addFixedPointGroup([](PassManager &group) {
        group.addPass(std::make_unique<DeadStoreEliminate>(
            DeadStoreEliminateMode::Lite));
        addScalarSimplifyPasses(group);
    });
}

static void addMemoryCleanup(PassManager &pm, bool runOnClean = false) {
    pm.addPass(std::make_unique<DeadStoreEliminate>());
    addScalarSimplifyClosure(pm, runOnClean);
}

static void buildArm64Pipeline(PassManager &pm, int optLevel, Module *m) {
    if (optLevel < 1)
        return;

    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<Mem2Reg>());
    pm.addPass(std::make_unique<RadixRecurrenceEliminate>());
    pm.addPass(std::make_unique<EarlyCSE>());
    addMemoryCleanup(pm, /*runOnClean=*/true);
    pm.addPass(std::make_unique<TailRecursionEliminate>());
    pm.addPass(std::make_unique<ScalarExpansion>());

    pm.addPass(std::make_unique<Reassociate>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<CodeSink>());

    pm.addPass(std::make_unique<BitFuncRecognize>());
    pm.addPass(std::make_unique<LastIterationElimination>());
    pm.addPass(std::make_unique<InlineExpand>());
    pm.addPass(std::make_unique<EarlyCSE>());
    pm.addPass(std::make_unique<GlobalScalarPromotion>());
    pm.addPass(std::make_unique<Mem2Reg>());
    pm.addPass(std::make_unique<Reassociate>());
    addMemoryCleanup(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<Reassociate>());
    addScalarSimplifyClosure(pm);

    pm.addPass(std::make_unique<CorrelatedValuePropagation>());
    pm.addPass(std::make_unique<JumpThreadingLite>());
    pm.addPass(std::make_unique<CFGSimplify>());
    addScalarSimplifyClosure(pm);

    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<IndVarSimplify>());
    pm.addPass(std::make_unique<SimpleLoopUnswitch>());
    pm.addPass(std::make_unique<LoopRotate>());
    pm.addPass(std::make_unique<PhiOpSink>());
    pm.addPass(std::make_unique<inductiveRangeCheckElimination>());
    pm.addPass(std::make_unique<LICM>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<LoopDeletion>());
    pm.addPass(std::make_unique<LoopRepFold>(LoopRepFoldMode::Lite));
    pm.addPass(std::make_unique<LoopFixedPointEliminate>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<TriangularRemapSourceCompose>());
    pm.addPass(std::make_unique<TriangularPanelize>());
    pm.addPass(std::make_unique<LinearRecurrenceFold>());
    pm.addPass(std::make_unique<LoopFusion>());
    pm.addPass(std::make_unique<LoopInvariantReduction>());
    pm.addPass(std::make_unique<LoopSkewing>());
    pm.addPass(std::make_unique<TriangleInterchange>());
    pm.addPass(std::make_unique<LoopInterchange>());
    pm.addPass(std::make_unique<LoopResetPointElimination>());
    pm.addPass(std::make_unique<ParallelizeLoops>());
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<IndVarSimplify>());
    pm.addPass(std::make_unique<LICM>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<IfConversion>());
    pm.addPass(std::make_unique<IdiomRecognize>());
    pm.addPass(std::make_unique<LoopVectorize>());
    pm.addPass(std::make_unique<IndVarStrengthReduce>());
    pm.addPass(std::make_unique<IdiomRecognize>());
    pm.addPass(std::make_unique<IfConversion>());
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LoopRepFold>());
    pm.addPass(std::make_unique<LoopModuloDelay>());
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LoopPeel>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LoopUnroll>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<LateMemoryForwarding>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LoopVectorize>());
    pm.addPass(std::make_unique<SLPVectorize>());
    pm.addPass(std::make_unique<VectorCombine>());
    pm.addPass(std::make_unique<SplitGEP>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<LateMemoryForwarding>());
    addMemoryCleanup(pm);

    pm.addPass(std::make_unique<GVN>());
    addLightweightMemoryClosure(pm);
    pm.addPass(std::make_unique<CodeSink>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<TailDuplication>());
    pm.addPass(std::make_unique<UnifyExitNodes>());
    pm.addPass(std::make_unique<CorrelatedValuePropagation>());
    pm.addPass(std::make_unique<JumpThreadingLite>());
    pm.addPass(std::make_unique<CFGSimplify>());
    addScalarSimplifyClosure(pm);
    pm.addPass(std::make_unique<LateValueCleanup>());
    pm.addPass(std::make_unique<LoopMemoryScalarPromotion>());
    if (AutoMemoization::moduleHasAnyCandidate(m))
        pm.addPass(std::make_unique<AutoMemoization>());
    pm.addPass(std::make_unique<TailCallOpt>());
}

} // namespace

extern std::unique_ptr<CompUnitAST> root;
extern int yyparse();
extern FILE *yyin;
extern void initFileName(const char *name);

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

    initFileName(options.input);
    if (yyparse() != 0 || !root) {
        std::cerr << "Parse failed.\n";
        return -1;
    }

    if (options.dumpAST)
        dumpAST(*root, std::cerr);

    std::string frontendError;
    if (!validateFrontend(*root, frontendError)) {
        std::cerr << options.input << ": semantic error: "
                  << frontendError << '\n';
        return -1;
    }

    GenIR genIR;
    root->accept(genIR);
    std::unique_ptr<Module> m = genIR.getModule();

    PassManager pm;
    pm.setDumpIR(options.dumpIR);
    pm.setDumpSCEV(options.dumpSCEV);
    pm.setVerifyIR(options.verifyIR);
    buildArm64Pipeline(pm, options.optLevel, m.get());
    pm.run(m.get());

    materializeSummableModSumRuntime(m.get());
    if (options.verifyIR)
        m->verify("summable-runtime-materialization");

    std::ofstream fout;
    std::ostream *out = nullptr;
    if (!openOutput(options, fout, out))
        return -1;

    if (options.printAsm || options.dumpMachineInstr) {
        backend::aarch64::BackendOptions backendOptions;
        backendOptions.optimizationLevel = options.optLevel;
        backendOptions.verifyMachineIR = true;
        backendOptions.dumpSelectionDAG =
            options.dumpPreMachineInstr;
        backendOptions.dumpMachineIR = options.dumpMachineInstr;
        backend::aarch64::AArch64Backend codegen(
            *m, *out, backendOptions);
        codegen.generate();
    } else if (options.printIR) {
        *out << m->print();
    }

    return 0;
}
