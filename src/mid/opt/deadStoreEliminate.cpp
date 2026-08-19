// 典型示例：
//   优化前：store 1, %p；store 2, %p；%p 在两次写入之间未被读取。
//   优化后：只保留 store 2, %p。
// 前一次写入的值无法被观察，因此可以连同其纯计算依赖一起删除。

#include "../../include/mid/opt/deadStoreEliminate.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"

#include <queue>
#include <set>
#include <vector>

// DeadStoreEliminate 删除在任何可观察读取前都会失效的写入。
// 快速模式只检查块内覆盖和原值写回，完整模式再使用支配关系与双向可达区域跨块证明。

// 兼容旧式入口：创建临时分析管理器并执行完整 pass 协议。
void DeadStoreEliminate::execute(Module *module) {
    AnalysisManager AM;
    runPass(module, AM);
}

// 使用调用方提供的分析缓存，并返回仍然有效的分析集合。
PreservedAnalyses DeadStoreEliminate::execute(Module *module,
                                               AnalysisManager &AM) {
    return runPass(module, AM).preserved;
}

// 在模块级复用 BasicAA，逐函数运行删除并汇总 changed 状态。
PassRunResult DeadStoreEliminate::runPass(Module *module,
                                          AnalysisManager &AM) {
    BasicAliasAnalysis &AA = AM.getBasicAA(module);
    bool changed = false;
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            changed |= runOnFunction(func, AA, AM.getDominatorTree(func));
    }
    return {changed, changed ? PreservedAnalyses::cfgAnalyses()
                             : PreservedAnalyses::all()};
}

// 判断同一基本块内 a 是否严格出现在 b 之前。
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

// 检查 store 之后是否存在覆盖同一完整位置的写，且覆盖前没有任何可能读取。
bool DeadStoreEliminate::isLocallyOverwritten(
    StoreInst *store, const BasicAliasAnalysis &AA) {
    if (!store || !store->parent_)
        return false;

    Value *ptr = store->get_operand(1);
    MemoryLocation location = AA.getMemoryLocation(ptr);
    bool afterStore = false;
    for (auto *inst : store->parent_->instr_list_) {
        if (inst == store) {
            afterStore = true;
            continue;
        }
        if (!afterStore)
            continue;

        if (auto *laterStore = dynamic_cast<StoreInst *>(inst)) {
            MemoryLocation laterLocation =
                AA.getMemoryLocation(laterStore->get_operand(1));
            if (location.sizeBytes > 0 &&
                laterLocation.sizeBytes >= location.sizeBytes &&
                AA.alias(location, laterLocation) == AliasResult::MustAlias)
                return true;
        }

        if (isRefSet(AA.getModRefInfo(inst, ptr)))
            return false;
    }
    return false;
}

// 识别 `v = load p; store v, p`，要求 load 与 store 之间没有可能修改 p 的指令。
bool DeadStoreEliminate::isLocallyRedundantWriteback(
    StoreInst *store, const BasicAliasAnalysis &AA) {
    if (!store || !store->parent_)
        return false;
    auto *load = dynamic_cast<LoadInst *>(store->get_operand(0));
    if (!load || load->parent_ != store->parent_ ||
        AA.alias(load->get_operand(0), store->get_operand(1)) !=
            AliasResult::MustAlias)
        return false;

    bool afterLoad = false;
    for (auto *inst : store->parent_->instr_list_) {
        if (inst == load) {
            afterLoad = true;
            continue;
        }
        if (inst == store)
            return afterLoad;
        if (!afterLoad)
            continue;
        if (isModSet(AA.getModRefInfo(inst, store->get_operand(1))))
            return false;
    }
    return false;
}

// 计算从起点沿后继边可达的块，限定 load 之后可能经过的控制流区域。
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

// 计算能够沿前驱边到达终点的块，限定最终 store 之前可能经过的控制流区域。
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

// 证明跨块的 load 原值写回冗余：检查所有位于 load 到 store 路径交集中的指令。
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

    // 两个可达集合的交集覆盖所有可能位于 load 与 store 之间的块。
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

// 收集可删除 store 后统一擦除，避免遍历指令链表时使迭代器失效。
bool DeadStoreEliminate::runOnFunction(Function *func,
                                       const BasicAliasAnalysis &AA,
                                       const DominatorTreeAnalysis &DT) {
    std::vector<StoreInst *> toDelete;
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            auto *store = dynamic_cast<StoreInst *>(inst);
            if (!store)
                continue;
            bool dead = isLocallyOverwritten(store, AA) ||
                        isLocallyRedundantWriteback(store, AA);
            if (!dead && mode_ == DeadStoreEliminateMode::Full)
                dead = isRedundantStore(store, AA, DT);
            if (dead)
                toDelete.push_back(store);
        }
    }

    for (auto *store : toDelete) {
        if (store->parent_)
            store->parent_->delete_instr(store);
    }

    return !toDelete.empty();
}
