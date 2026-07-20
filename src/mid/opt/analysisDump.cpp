#include "../../include/mid/opt/analysisDump.hpp"
#include "../../include/mid/analysis/affineAnalysis.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/module.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

const char *accessKind(Instruction *inst) {
    if (!inst) return "unknown";
    if (inst->is_load()) return "load";
    if (inst->is_store()) return "store";
    return "other";
}

std::string valueName(Value *value) {
    if (!value) return "<null>";
    if (!value->name_.empty()) return value->name_;
    return "<anon>";
}

std::string blockName(BasicBlock *bb) {
    return bb ? valueName(bb) : "<null>";
}

char dirChar(DependenceAnalysis::Dir dir) {
    return static_cast<char>(dir);
}

std::string directionString(const DependenceAnalysis::Result &result) {
    if (result.direction.empty()) return "-";
    std::string out;
    for (auto dir : result.direction)
        out.push_back(dirChar(dir));
    return out;
}

void collectLoopAccesses(Loop *loop, std::vector<Instruction *> &accesses) {
    accesses.clear();
    if (!loop) return;
    for (auto *bb : loop->blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load() || inst->is_store())
                accesses.push_back(inst);
        }
    }
}

void dumpFunction(Function *func, const ArgumentAliasAnalysis *argAA) {
    LoopInfo LI;
    LI.analyze(func);

    AffineAnalysis AA(LI);
    DependenceAnalysis DA(LI, AA);
    DA.setArgAlias(argAA);

    std::cerr << "[AnalysisDump] function=" << func->name_
              << " loops=" << LI.allLoops().size() << "\n";

    for (const auto &loopPtr : LI.allLoops()) {
        Loop *loop = loopPtr.get();
        std::vector<Instruction *> accesses;
        collectLoopAccesses(loop, accesses);

        std::cerr << "[AnalysisDump] loop function=" << func->name_
                  << " header=" << blockName(loop->header)
                  << " depth=" << loop->depth
                  << " canonicalIV=" << valueName(loop->canonicalIV)
                  << " preheader=" << blockName(loop->preheader)
                  << " latches=" << loop->latches.size()
                  << " exits=" << loop->exits.size()
                  << " accesses=" << accesses.size()
                  << " carries="
                  << (DA.loopCarriesDependence(loop, accesses) ? "yes" : "no")
                  << "\n";

        for (size_t i = 0; i < accesses.size(); i++) {
            for (size_t j = i; j < accesses.size(); j++) {
                Instruction *a = accesses[i];
                Instruction *b = accesses[j];
                if (!a->is_store() && !b->is_store())
                    continue;

                auto result = DA.test(a, b);
                std::cerr << "[AnalysisDump] dep function=" << func->name_
                          << " loop=" << blockName(loop->header)
                          << " a=" << accessKind(a) << "@" << blockName(a->parent_)
                          << " b=" << accessKind(b) << "@" << blockName(b->parent_)
                          << " aliased=" << (result.aliased ? "yes" : "no")
                          << " independent="
                          << (result.provably_independent ? "yes" : "no")
                          << " dirs=" << directionString(result)
                          << "\n";
            }
        }
    }
}

} // namespace

void AnalysisDump::execute(Module *module) {
    ArgumentAliasAnalysis argAA;
    argAA.analyze(module);
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            dumpFunction(func, &argAA);
    }
}

PreservedAnalyses AnalysisDump::execute(Module *module, AnalysisManager &AM) {
    (void)AM;
    execute(module);
    return PreservedAnalyses::all();
}

