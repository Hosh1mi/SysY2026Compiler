// LateMemoryForwarding 在线性基本块内执行别名感知的 store-to-load 转发。
// 它位于循环变换之后，用于收集变换新暴露的局部内存依赖，同时避免跨 CFG 推测。
// 典型示例：
//   优化前：store %v, @global；call @readonly_fn()；%x = load @global。
//   优化后：store %v, @global；call @readonly_fn()；%x 的使用直接采用 %v。
// 该 Pass 面向全局、数组元素及其它任意指针，在同一基本块内用 BasicAA 证明
// 最近 store 与 load 必然指向同一位置，并用 ModRef 证明中间调用不会覆盖它。
// 它保留内存对象和可观察的 store，只删除能够从最近写入取得结果的 load。

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

// 新 store 写入 ptr 时，移除所有可能与 ptr 重叠的旧可用写。
void invalidateMayAlias(StoreMap &stores, Value *ptr,
                        const BasicAliasAnalysis &BAA) {
    for (auto it = stores.begin(); it != stores.end();) {
        if (BAA.alias(it->first, ptr) == AliasResult::NoAlias)
            ++it;
        else
            it = stores.erase(it);
    }
}

// call 等指令可能修改内存时，按 ModRef 结果失效受影响的可用写。
void invalidateModifiedBy(StoreMap &stores, Instruction *instruction,
                          const BasicAliasAnalysis &BAA) {
    for (auto it = stores.begin(); it != stores.end();) {
        if (!isModSet(BAA.getModRefInfo(instruction, it->first)))
            ++it;
        else
            it = stores.erase(it);
    }
}

// 先按相同指针快速查询，再用 BasicAA 搜索 MustAlias 且类型一致的最近写入。
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

// 单次前向扫描一个基本块，用最新可用 store 值替换 load，并延迟删除旧 load。
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

// 对函数的每个基本块独立执行转发，块边界处清空可用写集合。
bool runOnFunction(Function *function, const BasicAliasAnalysis &BAA,
                   bool debug) {
    bool changed = false;
    for (BasicBlock *block : function->basic_blocks_)
        changed |= forwardInBlock(block, BAA, debug);
    return changed;
}

} // namespace

// 兼容旧式入口。
void LateMemoryForwarding::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

// 模块级入口：共享 BasicAA，变换只改 use-def 和指令列表，因此保留 CFG 分析。
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
