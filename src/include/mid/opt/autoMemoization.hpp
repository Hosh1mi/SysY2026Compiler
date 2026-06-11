#pragma once
#include "../opt/pass.hpp"

class BasicAliasAnalysis;
class GlobalVariable;
class BasicBlock;
class Function;
class Argument;

// 通用记忆化：将 (纯 + 自递归 + i32 参数) 模式的函数自动加上结果缓存。
// 入口处按参数查表，命中即返回；出口处将结果写回。
//
// 两条变换路径：
//   ① 数组路径（transform）：bound 可推导且乘积 ≤ ARRAY_PRODUCT_LIMIT 时
//      使用多维全局数组，单次查表 = 1 次内存访问，最快。
//   ② 哈希路径（transformHash）：bound 推不出或过大时使用固定大小直接映射
//      哈希表，槽内存 key 做碰撞比较，永不破坏正确性。
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

    // 哈希记忆化的核心变换；公开以便 AccumElim 等 pass 在 pipeline 早期
    // 直接对刚生成的 aux 函数加缓存（绕过 array/hash 选路决策）。
    static void transformHash(Function *f);

    // ── 哈希参数 / 槽布局常量；公开以便共享同一槽格式 ──
    static constexpr unsigned HASH_BITS = 16;
    static constexpr unsigned HASH_SLOTS = 1u << HASH_BITS;
    static constexpr unsigned HASH_MASK  = HASH_SLOTS - 1;

private:
    static constexpr unsigned MAX_ARGS = 2;
    static constexpr unsigned MIN_SELF_CALLS = 2;
    static constexpr unsigned BOUND_MARGIN = 5;
    static constexpr unsigned MAX_BOUND = 16384;
    // 单参数推不出 bound 时的本地兜底（如 knapsack 的 w：不作为 GEP 下标，
    // 但实际取值通常很小）。最终是否走数组路径，仍由总乘积 ≤ ARRAY_PRODUCT_LIMIT
    // 决定；本兜底不会让 BSS 失控。
    static constexpr unsigned DEFAULT_BOUND = 1024;

    // 数组路径乘积上限：超过则改走哈希。
    // 1.5M 元素 × i32 × 2 张表（flag+val）= 12 MB BSS 上限。
    // 选取理由：knapsack 现行 product ≈ 0.98M，留余量但不浪费 BSS。
    static constexpr unsigned ARRAY_PRODUCT_LIMIT = 1500000;

    bool isCandidate(Function *f, BasicAliasAnalysis &baa,
                     unsigned &selfCallCount, unsigned &externalCallCount);
    bool functionReadsMemory(Function *f);
    unsigned deriveArgBound(Function *f, Argument *arg);
    void transform(Function *f, const std::vector<unsigned> &bounds);
};
