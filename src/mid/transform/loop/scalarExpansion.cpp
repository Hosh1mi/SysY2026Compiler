#include "../../../include/mid/opt/scalarExpansion.hpp"
#include "../../../include/mid/ir/constant.hpp"
#include "../../../include/mid/ir/instruction.hpp"

namespace {

bool hasScalarExpansionScratch(Function *func, int size) {
    if (!func || func->basic_blocks_.empty()) return false;
    for (auto *inst : func->basic_blocks_.front()->instr_list_) {
        auto *alloca = dynamic_cast<AllocaInst *>(inst);
        if (!alloca || !alloca->isLoopExpansionScratch())
            continue;
        auto *arr = dynamic_cast<ArrayType *>(alloca->alloca_ty_);
        if (arr && static_cast<int>(arr->num_elements_) == size)
            return true;
    }
    return false;
}

} // namespace

void ScalarExpansion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) runOnFunction(func);
    }
}

void ScalarExpansion::runOnFunction(Function *func) {
    LoopInfo LI;
    LI.analyze(func);
    if (LI.allLoops().empty()) return;

    AffineAnalysis     AA(LI);
    DependenceAnalysis DA(LI, AA);
    CostModel          CM(AA);
    ReductionAnalysis  RA(AA);
    LoopAccessAnalysis LA(AA);
    LoopInterchangeAnalysis IA(DA, LA, CM);

    for (auto &L_ptr : LI.allLoops()) {
        Loop *L = L_ptr.get();
        if (!L->children.empty()) continue;

        ScalarReductionNestInfo info{};
        if (!RA.detectScalarExpandableNest(L, info)) continue;
        if (!RA.isScalarExpansionMemoryLegal(info)) continue;
        if (!isLegalAndProfitable(info, IA)) continue;

        for (auto &reduction : info.reductions) {
            if (!hasScalarExpansionScratch(func, reduction.inner_dim))
                createTempBuffer(func, reduction.inner_dim);
        }
    }
}

AllocaInst *ScalarExpansion::createTempBuffer(Function *func, int size) {
    if (!func || func->basic_blocks_.empty() || size <= 0) return nullptr;
    Module *module = func->parent_;
    auto *arr = module->get_array_type(module->int32_ty_, size);
    auto *entry = func->basic_blocks_.front();
    auto *alloca = new AllocaInst(arr, entry, true);
    alloca->markLoopExpansionScratch();
    alloca->name_ = "scalar.expansion.tmp." + std::to_string(tmp_counter_++);
    entry->add_instruction_front(alloca);
    return alloca;
}

bool ScalarExpansion::isLegalAndProfitable(const ScalarReductionNestInfo &info,
                                           LoopInterchangeAnalysis &IA) {
    PhiInst *L_iv = info.inner_loop->getInductionIV();
    PhiInst *P_iv = info.parent_loop->getInductionIV();
    std::vector<GetElementPtrInst *> geps = info.body_geps;
    for (auto &r : info.reductions) geps.push_back(r.gep_store);

    LoopInterchangeCost cost = IA.estimateCost(geps, L_iv, P_iv);
    return cost.profitable();
}
