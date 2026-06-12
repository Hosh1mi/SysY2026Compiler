#include "include/frontend/ast/ast.hpp"

#include "include/frontend/parser.hpp"

#include "include/mid/ir/irGen.hpp"
#include "include/mid/opt/passManager.hpp"
#include "include/mid/opt/deadCodeDelete.hpp"
#include "include/mid/opt/deadStoreEliminate.hpp"
#include "include/mid/opt/constFold.hpp"
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
#include "include/mid/opt/phiOpSink.hpp"
#include "include/mid/opt/loopInvariantCodeMotion.hpp"
#include "include/mid/opt/loopDeletion.hpp"
#include "include/mid/opt/indVarStrengthReduce.hpp"
#include "include/mid/opt/loopRepFold.hpp"
#include "include/mid/opt/scalarExpandedInterchange.hpp"
#include "include/mid/opt/loopUnroll.hpp"
#include "include/mid/opt/reassociate.hpp"
#include "include/mid/opt/loopVectorize.hpp"
#include "include/mid/opt/CFGSimplify.hpp"
#include "include/mid/opt/unifyExitNodes.hpp"
#include "include/mid/opt/globalScalarPromotion.hpp"

#include "include/backend/arm64/codegen.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

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
    bool dumpMachineInstr = false;
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
        } else if (arg == "--dump-machine-instr") {
            options.dumpMachineInstr = true;
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
    pm.addPass(std::make_unique<DeadCodeDelete>());
    pm.addPass(std::make_unique<SCCP>());
    pm.addPass(std::make_unique<ConstantFold>());
    pm.addPass(std::make_unique<InstCombine>());
    pm.addPass(std::make_unique<DeadStoreEliminate>());
    pm.addPass(std::make_unique<DeadCodeDelete>());
}

static void addCfgCleanup(PassManager &pm) {
    pm.addPass(std::make_unique<DeadCodeDelete>());
    pm.addPass(std::make_unique<CFGSimplify>());
}

static void addDeepCleanup(PassManager &pm) {
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<CFGSimplify>());
    addCanonicalCleanup(pm);
}

static void addSsaPreparation(PassManager &pm) {
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
}

static void addInterproceduralAndGlobals(PassManager &pm) {
    pm.addPass(std::make_unique<BitFuncRecognize>());
    pm.addPass(std::make_unique<InlineExpand>());
    pm.addPass(std::make_unique<LocalCopyPropagation>());
    pm.addPass(std::make_unique<GlobalScalarPromotion>());
    pm.addPass(std::make_unique<Mem2Reg>());
    addDeepCleanup(pm);
}

static void addLoopPipeline(PassManager &pm) {
    // pm.addPass(std::make_unique<UnifyExitNodes>());
    pm.addPass(std::make_unique<CFGSimplify>());
    pm.beginRepeatGroup(/*maxRounds=*/3);
    pm.addPass(std::make_unique<LoopSimplify>());
    pm.addPass(std::make_unique<LCSSA>());
    pm.addPass(std::make_unique<SimpleLoopUnswitch>());
    pm.addPass(std::make_unique<LoopRotate>());
    pm.addPass(std::make_unique<PhiOpSink>());
    pm.addPass(std::make_unique<LICM>());
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<LoopDeletion>());
    pm.endRepeatGroup();
    pm.addPass(std::make_unique<LoopVectorize>());
    pm.addPass(std::make_unique<IndVarStrengthReduce>());
    pm.addPass(std::make_unique<LoopRepFold>());
    pm.addPass(std::make_unique<LoopUnroll>());
    addDeepCleanup(pm);
}

static void buildOptimizationPipeline(PassManager &pm, int optLevel) {
    if (optLevel < 1)
        return;

    addSsaPreparation(pm);
    addScalarNormalization(pm);
    addInterproceduralAndGlobals(pm);
    addLoopPipeline(pm);
    addCanonicalCleanup(pm);
    pm.addPass(std::make_unique<UnifyExitNodes>());
    addCfgCleanup(pm);

    if (optLevel >= 2) {
        
    }
}

static void configureBackend(Arm64CodeGen &codegen, const DriverOptions &options) {
    bool enableOptimizations = options.optLevel >= 1;
    codegen.setEnableRegAlloc(enableOptimizations);
    codegen.setNoPeephole(!enableOptimizations || options.disablePeephole);
    codegen.setNoSchedule(!enableOptimizations || options.disableSchedule);
    codegen.setDumpMachineInstr(options.dumpMachineInstr);
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
        Arm64CodeGen codegen(m.get(), *out);
        configureBackend(codegen, options);
        codegen.generate();
    } else if (options.printIR) {
        *out << m->print();
    }

    return 0;
}
