#pragma once
#include "../opt/pass.hpp"

class BasicAliasAnalysis;
class GlobalVariable;
class BasicBlock;
class Function;
class Argument;

// 通用记忆化：将 (纯 + 自递归 + i32 参数) 模式的函数自动加上结果缓存。
// 入口处按参数查表，命中即返回；出口处将结果写回。
// 表大小由 IR 中数组维度静态推导，无法推导时使用保守默认值。
class AutoMemoization : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "AutoMemoization"; }

    // 轻量结构扫描，用于决定是否要把本 pass 加入 pipeline：仅看返回类型 /
    // 参数类型 / 静态自调用数。不查 BAA、不变换 IR、不依赖 mem2reg。
    //
    // 即使本 pass 实现完全空操作，仅 make_unique<AutoMemoization>() 的
    // 一次堆分配也会推移后续 IR 对象指针值，下游基于 std::set<pointer*>
    // 的迭代会改变次序，实测让 huffman 等无关用例变慢 ~100ms。
    // 模块无候选时直接不加 pass，pipeline 与原状完全一致。
    static bool moduleHasAnyCandidate(Module *m);

private:
    static constexpr unsigned MAX_ARGS = 2;
    static constexpr unsigned MIN_SELF_CALLS = 2;
    static constexpr unsigned DEFAULT_BOUND = 1024;
    static constexpr unsigned BOUND_MARGIN = 5;
    static constexpr unsigned MAX_BOUND = 16384;

    bool isCandidate(Function *f, BasicAliasAnalysis &baa,
                     unsigned &selfCallCount, unsigned &externalCallCount);
    unsigned deriveArgBound(Function *f, Argument *arg);
    void transform(Function *f, const std::vector<unsigned> &bounds);
};
