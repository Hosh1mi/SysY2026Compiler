#pragma once
// AutoMemoization —— 为纯自递归 i32 函数自动加记忆化包装。
//
// 将重复子问题的自递归计算改为查表 / 哈希缓存后再返回。
//
// 典型支持形式：
//   int f(int n) { if (n < 2) return n; return f(n-1) + f(n-2); }
//   多参数纯自递归 DP（参数域可推导上界时用稠密表）
//
// 要求返回 i32、少量 i32 参数、无 store、外呼均为 pure。参数界可推导
// 且足够小时用稠密表，否则用定长哈希表。非纯 / 非自递归不处理。
// 成功后对外为薄包装：命中直接返回，未命中计算并写回缓存。

#include "../opt/pass.hpp"

class BasicAliasAnalysis;
class GlobalVariable;
class BasicBlock;
class Function;
class Argument;

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
    // 的迭代会改变次序。模块无候选时直接不加 pass，pipeline 与原状完全一致。
    static bool moduleHasAnyCandidate(Module *m);

private:
    static constexpr unsigned MAX_ARGS = 3;
    static constexpr unsigned MIN_SELF_CALLS = 2;
    static constexpr unsigned BOUND_MARGIN = 5;
    static constexpr unsigned MAX_BOUND = 16384;
    // 推不出 bound 时的占位；若任一参数 underived，直接走哈希路径，
    // 不再用本值撑起可能过大的稠密数组。
    static constexpr unsigned DEFAULT_BOUND = 1024;

    // 数组路径乘积上限：超过则改走哈希。
    // 1.5M 槽 × [2 x i32] 打包表 = 12 MB BSS 上限。
    static constexpr unsigned ARRAY_PRODUCT_LIMIT = 1500000;

    static constexpr unsigned HASH_BITS = 16;
    static constexpr unsigned HASH_SLOTS = 1u << HASH_BITS;
    static constexpr unsigned HASH_MASK  = HASH_SLOTS - 1;

    bool isCandidate(Function *f, BasicAliasAnalysis &baa, AnalysisManager &AM,
                     unsigned &selfCallCount, unsigned &externalCallCount);
    bool functionReadsMemory(Function *f);
    // 读到的全局是否存在“调用点之间仍可能被改写”的 store。
    // 若每个相关 store 都支配全部外部调用点，则全局在首次调用前已冻结，
    // 跨调用点缓存安全。
    bool readsUnfrozenGlobal(Function *f, AnalysisManager &AM);
    unsigned deriveArgBound(Function *f, Argument *arg);
    // 把 f 的全部基本块迁到新函数，形参 use 改写到新函数；f 变空壳供建包装。
    Function *outlineBody(Function *f);
    void transform(Function *f, const std::vector<unsigned> &bounds);
    void transformHash(Function *f);
};
