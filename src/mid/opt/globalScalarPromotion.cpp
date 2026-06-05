#include "../../include/mid/opt/globalScalarPromotion.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/type.hpp"
#include <set>
#include <unordered_map>
#include <vector>

// GlobalVariable::type_ = PointerType(element_type); scalar if element is integer.
static bool isScalarIntGlobal(GlobalVariable *gv) {
    auto *pt = dynamic_cast<PointerType *>(gv->type_);
    if (!pt) return false;
    return pt->contained_->tid_ == Type::IntegerTyID;
}

static bool hasAnyCall(Function *func) {
    for (auto *bb : func->basic_blocks_)
        for (auto *inst : bb->instr_list_)
            if (inst->is_call()) return true;
    return false;
}

static void promoteInFunction(Function *func) {
    // Phase 1: find scalar globals used, and snapshot original load/store instructions.
    std::set<GlobalVariable *> usedGlobals;
    std::vector<std::pair<LoadInst *, GlobalVariable *>>  origLoads;
    std::vector<std::pair<StoreInst *, GlobalVariable *>> origStores;

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load()) {
                auto *gv = dynamic_cast<GlobalVariable *>(inst->get_operand(0));
                if (gv && isScalarIntGlobal(gv)) {
                    usedGlobals.insert(gv);
                    origLoads.push_back({static_cast<LoadInst *>(inst), gv});
                }
            }
            if (inst->is_store()) {
                auto *gv = dynamic_cast<GlobalVariable *>(inst->get_operand(1));
                if (gv && isScalarIntGlobal(gv)) {
                    usedGlobals.insert(gv);
                    origStores.push_back({static_cast<StoreInst *>(inst), gv});
                }
            }
        }
    }
    if (usedGlobals.empty()) return;

    auto *entry = func->basic_blocks_.front();
    Instruction *anchor = entry->instr_list_.front(); // insertion anchor (stays first)

    std::unordered_map<GlobalVariable *, AllocaInst *> gvAlloca;

    // Phase 2: for each global, insert alloca + load-from-global + store-to-alloca at entry.
    for (auto *gv : usedGlobals) {
        auto *elemTy = static_cast<PointerType *>(gv->type_)->contained_;

        auto *alloca = new AllocaInst(elemTy, entry, /*no_insert=*/true);
        entry->add_instruction_before_inst(alloca, anchor);

        // LoadInst has no no_insert ctor: create inserting (→ end), then move.
        auto *initLoad = new LoadInst(gv, entry);
        entry->remove_instr(initLoad);
        entry->add_instruction_before_inst(initLoad, anchor);

        auto *initStore = new StoreInst(initLoad, alloca, entry, /*no_insert=*/true);
        entry->add_instruction_before_inst(initStore, anchor);

        gvAlloca[gv] = alloca;
    }

    // Phase 3: at each ret, store alloca back to the global.
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_ret()) continue;
            for (auto *gv : usedGlobals) {
                auto *alloca = gvAlloca[gv];
                auto *exitLoad = new LoadInst(alloca, bb);
                bb->remove_instr(exitLoad);
                bb->add_instruction_before_inst(exitLoad, inst);
                auto *exitStore = new StoreInst(exitLoad, gv, bb, /*no_insert=*/true);
                bb->add_instruction_before_inst(exitStore, inst);
            }
        }
    }

    // Phase 4: redirect the original (pre-existing) loads/stores to go through the alloca.
    for (auto &[load, gv] : origLoads)
        load->set_operand(0, gvAlloca[gv]);
    for (auto &[store, gv] : origStores)
        store->set_operand(1, gvAlloca[gv]);
}

void GlobalScalarPromotion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        if (hasAnyCall(func)) continue;
        promoteInFunction(func);
    }
}
