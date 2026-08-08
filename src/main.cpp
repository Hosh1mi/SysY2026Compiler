#include "include/frontend/ast/ast.hpp"
#include "include/frontend/parser.hpp"

#include "include/mid/ir/irGen.hpp"
#include "include/mid/runtime/summableModSumRuntime.hpp"
#include "include/mid/opt/passManager.hpp"
#include "include/mid/opt/optPasses.hpp"

#include "include/backend/arm64/parallelRuntime.hpp"
#include "include/backend/arm64/rewrite/codegen.hpp"

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
    pm.addPass(std::make_unique<CanonicalCleanup>());
}

static void buildArm64Pipeline(PassManager &pm, int optLevel, Module *m) {
    if (optLevel < 1)
        return;

    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<Mem2Reg>());
    pm.addPass(std::make_unique<RadixRecurrenceEliminate>());
    pm.addPass(std::make_unique<EarlyCSE>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<TailRecursionEliminate>());
    pm.addPass(std::make_unique<ScalarExpansion>());

    pm.addPass(std::make_unique<Reassociate>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<LocalCopyPropagation>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CodeSink>());

    pm.addPass(std::make_unique<BitFuncRecognize>());
    pm.addPass(std::make_unique<LastIterationElimination>());
    pm.addPass(std::make_unique<InlineExpand>());
    pm.addPass(std::make_unique<LocalCopyPropagation>());
    pm.addPass(std::make_unique<SemanticMarkerStamp>());
    pm.addPass(std::make_unique<EarlyCSE>());
    pm.addPass(std::make_unique<GlobalScalarPromotion>());
    pm.addPass(std::make_unique<Mem2Reg>());
    pm.addPass(std::make_unique<Reassociate>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<Reassociate>());
    addCanonicalCleanup(pm);

    pm.addPass(std::make_unique<CorrelatedValuePropagation>());
    pm.addPass(std::make_unique<JumpThreadingLite>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<SemanticMarkerStamp>());

    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addRepeatGroup(/*maxRounds=*/8, [](PassManager &pm) {
        pm.addPass(std::make_unique<LoopSimplify>());
        pm.addPass(std::make_unique<LCSSA>());
        pm.addPass(std::make_unique<IndVarSimplify>());
        pm.addPass(std::make_unique<SimpleLoopUnswitch>());
        pm.addPass(std::make_unique<LoopRotate>());
        pm.addPass(std::make_unique<PhiOpSink>());
        pm.addPass(std::make_unique<inductiveRangeCheckElimination>());
        pm.addPass(std::make_unique<LICM>());
        addCanonicalCleanup(pm);
        pm.addPass(std::make_unique<LCSSA>());
        pm.addPass(std::make_unique<LoopDeletion>());
    });
    pm.addPass(std::make_unique<LoopFixedPointEliminate>());
    addCanonicalCleanup(pm);
    if (std::getenv("DEBUG_DUMP_ANALYSIS"))
        pm.addPass(std::make_unique<AnalysisDump>());
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
    pm.addPass(std::make_unique<LCSSA>());
    pm.addPass(std::make_unique<LoopPeel>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LCSSA>());
    pm.addPass(std::make_unique<LoopUnroll>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LCSSA>());
    pm.addPass(std::make_unique<LoopVectorize>());
    pm.addPass(std::make_unique<SLPVectorize>());
    pm.addPass(std::make_unique<SplitGEP>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    addCanonicalCleanup(pm);

    pm.addPass(std::make_unique<GVN>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CodeSink>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<TailDuplication>());
    pm.addPass(std::make_unique<UnifyExitNodes>());
    pm.addPass(std::make_unique<CorrelatedValuePropagation>());
    pm.addPass(std::make_unique<JumpThreadingLite>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.addPass(std::make_unique<DeadCodeEliminate>());
    pm.addPass(std::make_unique<LateValueCleanup>());
    pm.addPass(std::make_unique<LoopMemoryScalarPromotion>());
    if (AutoMemoization::moduleHasAnyCandidate(m))
        pm.addPass(std::make_unique<AutoMemoization>());
    pm.addPass(std::make_unique<TailCallOpt>());
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
        backendOptions.disablePeephole =
            options.disablePeephole;
        backendOptions.disableSchedule =
            options.disableSchedule;
        backendOptions.disablePreSchedule =
            options.disablePreSchedule;
        backend::aarch64::AArch64Backend codegen(
            m.get(), *out, backendOptions);
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
    } else if (options.printIR) {
        *out << m->print();
    }

    return 0;
}
