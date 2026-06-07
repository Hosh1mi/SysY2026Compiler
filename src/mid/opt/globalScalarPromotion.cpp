#include "../../include/mid/opt/globalScalarPromotion.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/type.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

// A scalar global is a GlobalVariable whose pointee is an integer type.
// Float globals and array globals are deliberately excluded — array globals
// have non-trivial aliasing through GEP, and float globals are rare enough
// in SysY that the extra type dispatch isn't worth the added surface area.
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
    // Phase 1: collect scalar globals in *first-seen* IR order, and snapshot
    // pre-existing load/store instructions for later redirection.
    //
    // First-seen order matters for reproducibility: a std::set<GlobalVariable*>
    // would order by pointer address, which varies between runs and produces
    // different alloca ordering — hence different register allocation and
    // assembly output for the same source program.
    std::vector<GlobalVariable *>                          usedGlobals;
    std::unordered_set<GlobalVariable *>                   seenGlobals;
    std::vector<std::pair<LoadInst *, GlobalVariable *>>   origLoads;
    std::vector<std::pair<StoreInst *, GlobalVariable *>>  origStores;

    auto noteGlobal = [&](GlobalVariable *gv) {
        if (seenGlobals.insert(gv).second) usedGlobals.push_back(gv);
    };

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load()) {
                auto *gv = dynamic_cast<GlobalVariable *>(inst->get_operand(0));
                if (gv && isScalarIntGlobal(gv)) {
                    noteGlobal(gv);
                    origLoads.push_back({static_cast<LoadInst *>(inst), gv});
                }
            }
            if (inst->is_store()) {
                auto *gv = dynamic_cast<GlobalVariable *>(inst->get_operand(1));
                if (gv && isScalarIntGlobal(gv)) {
                    noteGlobal(gv);
                    origStores.push_back({static_cast<StoreInst *>(inst), gv});
                }
            }
        }
    }
    if (usedGlobals.empty()) return;

    auto *entry = func->basic_blocks_.front();
    Instruction *anchor = entry->instr_list_.front();  // stays first; we insert before it.

    std::unordered_map<GlobalVariable *, AllocaInst *> gvAlloca;

    // Phase 2: for each used global, prepend at entry:
    //   %a = alloca <elemTy>
    //   %v = load <elemTy>, gv
    //   store <elemTy> %v, %a
    for (auto *gv : usedGlobals) {
        auto *elemTy = static_cast<PointerType *>(gv->type_)->contained_;

        auto *alloca = new AllocaInst(elemTy, entry, /*no_insert=*/true);
        entry->add_instruction_before_inst(alloca, anchor);

        // LoadInst has no no_insert ctor: it always appends to the supplied
        // BB.  Insert at end, immediately unlink, then re-insert before the
        // anchor.  BasicBlock::remove_instr preserves use-list edges
        // (CLAUDE.md "IR key invariants") so the use of `gv` survives.
        auto *initLoad = new LoadInst(gv, entry);
        entry->remove_instr(initLoad);
        entry->add_instruction_before_inst(initLoad, anchor);

        auto *initStore = new StoreInst(initLoad, alloca, entry, /*no_insert=*/true);
        entry->add_instruction_before_inst(initStore, anchor);

        gvAlloca[gv] = alloca;
    }

    // Phase 3: before every ret, write the alloca back to the global so the
    // function's observable side-effects on globals match the unpromoted
    // source.  Newly inserted instructions land before `inst`, so the outer
    // range-for does not revisit them.
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

    // Phase 4: redirect every pre-existing load/store off the global onto the
    // alloca.  A subsequent Mem2Reg will lift the alloca into SSA form.
    for (auto &[load, gv] : origLoads)
        load->set_operand(0, gvAlloca[gv]);
    for (auto &[store, gv] : origStores)
        store->set_operand(1, gvAlloca[gv]);
}

void GlobalScalarPromotion::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        // Only promote in call-free functions.  A surviving call could reach
        // into the global via another function; promoting locally would then
        // break the value the callee observes.  In practice this gate fires
        // almost exclusively on the post-inline `main`.
        if (hasAnyCall(func)) continue;
        promoteInFunction(func);
    }
}
