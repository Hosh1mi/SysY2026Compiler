#include "../../include/mid/opt/analysisDump.hpp"
#include "../../include/mid/analysis/affineAnalysis.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/analysis/dependenceAnalysis.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/analysis/scalarEvolution.hpp"
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

std::string valueDescription(Value *value) {
    if (auto *constant = dynamic_cast<ConstantInt *>(value))
        return std::to_string(constant->value_);
    return valueName(value);
}

const char *predicateName(ICmpInst::ICmpOp predicate) {
    switch (predicate) {
    case ICmpInst::ICMP_UGT: return "ugt";
    case ICmpInst::ICMP_UGE: return "uge";
    case ICmpInst::ICMP_ULT: return "ult";
    case ICmpInst::ICMP_ULE: return "ule";
    case ICmpInst::ICMP_SGT: return "sgt";
    case ICmpInst::ICMP_SGE: return "sge";
    case ICmpInst::ICMP_SLT: return "slt";
    case ICmpInst::ICMP_SLE: return "sle";
    case ICmpInst::ICMP_EQ: return "eq";
    case ICmpInst::ICMP_NE: return "ne";
    }
    return "unknown";
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

void dumpLiveOutRecurrences(Function *func, Loop *loop,
                            ScalarEvolution &scalarEvolution) {
    if (!func || !loop || !loop->header) return;
    for (auto *inst : loop->header->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) break;

        size_t outsideUses = 0;
        size_t insideUses = 0;
        for (const Use &use : phi->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user || !user->parent_) continue;
            if (loop->blocks.count(user->parent_))
                ++insideUses;
            else
                ++outsideUses;
        }
        if (outsideUses == 0) continue;

        const SCEV *scev = scalarEvolution.getSCEV(phi);
        auto *addRec = dynamic_cast<const SCEVAddRecExpr *>(scev);
        if (!addRec || addRec->loop() != loop) continue;

        std::cerr << "[AnalysisDump] liveout-addrec function=" << func->name_
                  << " header=" << blockName(loop->header)
                  << " phi=" << valueName(phi)
                  << " start=" << addRec->start()->print()
                  << " step=" << addRec->step()->print()
                  << " insideUses=" << insideUses
                  << " outsideUses=" << outsideUses
                  << " control="
                  << (loop->controlInduction.phi == phi ? "yes" : "no")
                  << "\n";
    }
}

void dumpFunction(Function *func, const ArgumentAliasAnalysis *argAA) {
    LoopInfo LI;
    LI.analyze(func);

    AffineAnalysis AA(LI);
    DependenceAnalysis DA(LI, AA);
    DA.setArgAlias(argAA);
    ScalarEvolution scalarEvolution(LI);

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
                  << " inductionIV=" << valueName(loop->inductionIV)
                  << " preheader=" << blockName(loop->preheader)
                  << " latches=" << loop->latches.size()
                  << " exits=" << loop->exits.size()
                  << " accesses=" << accesses.size()
                  << " carries="
                  << (DA.loopCarriesDependence(loop, accesses) ? "yes" : "no")
                  << "\n";

        if (const auto *induction = loop->getInductionDescriptor()) {
            std::cerr << "[AnalysisDump] induction function=" << func->name_
                      << " header=" << blockName(loop->header)
                      << " phi=" << valueName(induction->phi)
                      << " start=" << valueDescription(induction->start)
                      << " step="
                      << (induction->stepNegated ? "-" : "")
                      << valueDescription(induction->step)
                      << " constantStep=";
            if (induction->constantStep)
                std::cerr << *induction->constantStep;
            else
                std::cerr << "unknown";
            std::cerr << " predicate="
                      << predicateName(induction->predicate)
                      << " bound=" << valueDescription(induction->bound)
                      << " guard="
                      << (induction->guardPosition ==
                                  InductionGuardPosition::Header
                              ? "header"
                              : "latch")
                      << " compares="
                      << (induction->comparesUpdate ? "update" : "phi")
                      << " exactConstantTrips=";
            if (auto trips =
                    scalarEvolution.getConstantTripCount(loop))
                std::cerr << *trips;
            else
                std::cerr << "unknown";
            std::cerr
                      << "\n";
        }

        dumpLiveOutRecurrences(func, loop, scalarEvolution);

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
