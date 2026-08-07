#include "../../include/mid/opt/deadStoreEliminate.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"

#include <queue>
#include <set>
#include <vector>

void DeadStoreEliminate::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses DeadStoreEliminate::execute(Module *module,
                                               AnalysisManager &AM) {
    BasicAliasAnalysis &AA = AM.getBasicAA(module);
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AA, AM.getDominatorTree(func));
    }
    return changed ? PreservedAnalyses::cfgAnalyses()
                   : PreservedAnalyses::all();
}

static bool instBefore(Instruction *a, Instruction *b) {
    if (!a || !b || a->parent_ != b->parent_)
        return false;
    for (auto *inst : a->parent_->instr_list_) {
        if (inst == a)
            return true;
        if (inst == b)
            return false;
    }
    return false;
}

static std::set<BasicBlock *> forwardReachableFrom(BasicBlock *start) {
    std::set<BasicBlock *> visited;
    std::queue<BasicBlock *> worklist;
    visited.insert(start);
    worklist.push(start);

    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        for (auto *succ : bb->succ_bbs_) {
            if (visited.insert(succ).second)
                worklist.push(succ);
        }
    }
    return visited;
}

static std::set<BasicBlock *> backwardReachableFrom(BasicBlock *start) {
    std::set<BasicBlock *> visited;
    std::queue<BasicBlock *> worklist;
    visited.insert(start);
    worklist.push(start);

    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();
        for (auto *pred : bb->pre_bbs_) {
            if (visited.insert(pred).second)
                worklist.push(pred);
        }
    }
    return visited;
}

bool DeadStoreEliminate::isRedundantStore(StoreInst *store,
                                          const BasicAliasAnalysis &AA,
                                          const DominatorTreeAnalysis &DT) {
    if (!store)
        return false;

    auto *load = dynamic_cast<LoadInst *>(store->get_operand(0));
    if (!load)
        return false;

    Value *storePtr = store->get_operand(1);
    Value *loadPtr = load->get_operand(0);
    if (AA.alias(storePtr, loadPtr) != AliasResult::MustAlias)
        return false;

    BasicBlock *loadBB = load->parent_;
    BasicBlock *storeBB = store->parent_;
    Function *func = storeBB->parent_;
    if (!DT.dominates(loadBB, storeBB))
        return false;
    if (loadBB == storeBB && !instBefore(load, store))
        return false;

    auto fromLoad = forwardReachableFrom(loadBB);
    auto toStore = backwardReachableFrom(storeBB);
    for (auto *bb : func->basic_blocks_) {
        if (!fromLoad.count(bb) || !toStore.count(bb))
            continue;

        bool afterLoad = bb != loadBB;
        for (auto *inst : bb->instr_list_) {
            if (inst == load) {
                afterLoad = true;
                continue;
            }
            if (inst == store)
                break;
            if (!afterLoad)
                continue;
            if (isModSet(AA.getModRefInfo(inst, storePtr))) {
                if (auto *interStore = dynamic_cast<StoreInst *>(inst)) {
                    if (interStore->get_operand(0) == store->get_operand(0))
                        continue;
                }
                return false;
            }
        }
    }

    return true;
}

bool DeadStoreEliminate::runOnFunction(Function *func,
                                       const BasicAliasAnalysis &AA,
                                       const DominatorTreeAnalysis &DT) {
    std::vector<StoreInst *> toDelete;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            auto *store = dynamic_cast<StoreInst *>(inst);
            if (store && isRedundantStore(store, AA, DT))
                toDelete.push_back(store);
        }
    }

    for (auto *store : toDelete) {
        if (store->parent_)
            store->parent_->delete_instr(store);
    }

    return !toDelete.empty();
}
