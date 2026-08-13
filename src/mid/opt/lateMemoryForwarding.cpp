// This file performs late, alias-aware store-to-load forwarding after loop
// transforms have exposed new straight-line memory dependences.
#include "../../include/mid/opt/lateMemoryForwarding.hpp"

#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

struct AvailableStore {
    Value *value = nullptr;
};

using StoreMap = std::unordered_map<Value *, AvailableStore>;

void invalidateMayAlias(StoreMap &stores, Value *ptr,
                        const BasicAliasAnalysis &BAA) {
    for (auto it = stores.begin(); it != stores.end();) {
        if (BAA.alias(it->first, ptr) == AliasResult::NoAlias)
            ++it;
        else
            it = stores.erase(it);
    }
}

void invalidateModifiedBy(StoreMap &stores, Instruction *instruction,
                          const BasicAliasAnalysis &BAA) {
    for (auto it = stores.begin(); it != stores.end();) {
        if (!isModSet(BAA.getModRefInfo(instruction, it->first)))
            ++it;
        else
            it = stores.erase(it);
    }
}

AvailableStore *findMustAliasStore(StoreMap &stores, LoadInst *load,
                                   const BasicAliasAnalysis &BAA) {
    Value *ptr = load->get_operand(0);
    auto exact = stores.find(ptr);
    if (exact != stores.end() && exact->second.value &&
        exact->second.value->type_ == load->type_)
        return &exact->second;

    for (auto &[storedPtr, available] : stores)
        if (available.value && available.value->type_ == load->type_ &&
            BAA.alias(storedPtr, ptr) == AliasResult::MustAlias)
            return &available;
    return nullptr;
}

bool forwardInBlock(BasicBlock *block, const BasicAliasAnalysis &BAA,
                    bool debug) {
    StoreMap stores;
    std::vector<Instruction *> instructions(block->instr_list_.begin(),
                                             block->instr_list_.end());
    std::vector<Instruction *> deadLoads;

    for (Instruction *instruction : instructions) {
        if (auto *load = dynamic_cast<LoadInst *>(instruction)) {
            AvailableStore *available = findMustAliasStore(stores, load, BAA);
            if (!available)
                continue;
            if (debug)
                std::cerr << "[LateMemoryForwarding] block=" << block->name_
                          << " load=%" << load->name_ << "\n";
            load->replace_all_use_with(available->value);
            deadLoads.push_back(load);
            continue;
        }

        if (auto *store = dynamic_cast<StoreInst *>(instruction)) {
            Value *ptr = store->get_operand(1);
            invalidateMayAlias(stores, ptr, BAA);
            stores.emplace(ptr, AvailableStore{store->get_operand(0)});
            continue;
        }

        if (instruction->is_call())
            invalidateModifiedBy(stores, instruction, BAA);
    }

    bool changed = false;
    for (Instruction *load : deadLoads)
        changed |= block->delete_instr(load);
    return changed;
}

bool runOnFunction(Function *function, const BasicAliasAnalysis &BAA,
                   bool debug) {
    bool changed = false;
    for (BasicBlock *block : function->basic_blocks_)
        changed |= forwardInBlock(block, BAA, debug);
    return changed;
}

} // namespace

void LateMemoryForwarding::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses LateMemoryForwarding::execute(Module *module,
                                                AnalysisManager &AM) {
    BasicAliasAnalysis &BAA = AM.getBasicAA(module);
    const bool debug = std::getenv("DEBUG_LATE_MEMORY_FORWARDING") != nullptr;
    bool changed = false;
    for (Function *function : module->function_list_)
        if (!function->is_declaration())
            changed |= runOnFunction(function, BAA, debug);
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}
