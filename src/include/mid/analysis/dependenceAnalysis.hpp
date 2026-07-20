#pragma once
// DependenceAnalysis: 内存访问之间的依赖方向分析
//
// 对一对访问 (acc1, acc2)（load 或 store）：
//   1. 检查是否可能别名（基址同一且 GEP 索引都是仿射）
//   2. 在共同嵌套的循环里求方向向量 direction[i] ∈ {<, =, >, *}
//      direction[i] 描述：从 acc1 的迭代点到 acc2 的迭代点，第 i 层 IV 的相对方向
//   3. 不能精确证明无依赖时保守返回 *（任意方向）
//
// 当前实现：
//   - 索引必须可仿射化为 c0 + Σ c_iv·iv
//   - 以两个独立迭代实例建立下标等式，并用 GCD test 排除无解情况
//   - 常量循环界使用 Banerjee 区间与三角迭代域顶点计算方向
//   - 缺少界、基址仅 MayAlias 或耦合关系无法求解时保守返回 ANY

#include "affineAnalysis.hpp"
#include "loopInfo.hpp"

#include <vector>

class ArgumentAliasAnalysis;

class DependenceAnalysis {
public:
    DependenceAnalysis(const LoopInfo &LI, AffineAnalysis &AA)
        : LI_(&LI), AA_(&AA) {}

    // 可选过程间参数别名 oracle：用于消除不同指针参数间的别名歧义。
    // 不设置时退回原保守行为（不同 global 才判不别名）。
    void setArgAlias(const ArgumentAliasAnalysis *argAA) { argAA_ = argAA; }

    enum Dir : char {
        DIR_EQ  = '=',
        DIR_LT  = '<',
        DIR_GT  = '>',
        DIR_ANY = '*',     // 不能精确判断
    };

    struct Result {
        bool             aliased  = false;  // 两个访问可能命中同一地址？
        bool             provably_independent = false;  // 已证明无任何依赖
        std::vector<Dir> direction;         // 每层共同嵌套的方向（从最外到最内）
        std::vector<Loop *> commonLoops;    // 共同嵌套的循环（最外在前）
    };

    // 主入口
    Result test(Instruction *acc1, Instruction *acc2);

    // 给一对访问 + 一对相邻 loop 判断 loop interchange 是否合法：
    // direction 向量在两个位置上不能形如 (>, <)，否则交换会反转依赖
    bool isInterchangeLegal(Loop *outer, Loop *inner,
                            const std::vector<Instruction *> &accesses);

    // 一组访问中，L 这一层是否携带依赖（loop-carried dependence）。
    // 携带依赖 ⇔ 存在一对相依访问，其在 L 这一层的方向不是 '='（即 L 的迭代之间
    // 互相影响）。无任何 L-携带依赖时该层完全并行（DOALL），可任意外提/下沉。
    // accesses 应当全部位于 L 内；任一相依对里求不出 L 的方向时保守判为"携带"。
    bool loopCarriesDependence(Loop *L,
                               const std::vector<Instruction *> &accesses);
    bool isLoopParallel(Loop *L, const std::vector<Instruction *> &accesses) {
        return !loopCarriesDependence(L, accesses);
    }

private:
    enum class BaseRelation {
        NoAlias,
        MustAlias,
        MayAlias,
    };

    Value             *gepBase(GetElementPtrInst *gep) const;
    GetElementPtrInst *accessGEP(Instruction *acc) const;
    BaseRelation       baseRelation(Value *a, Value *b) const;

    const LoopInfo  *LI_;
    AffineAnalysis  *AA_;
    const ArgumentAliasAnalysis *argAA_ = nullptr;
};
