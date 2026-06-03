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
//   - GCD test：每维 diff = c_const + Σ c_iv·iv，若 gcd(c_iv) 不整除 c_const
//     则整对无依赖（包含 "纯常数非零" 这条特例）
//   - 方向判定保留为粗粒度（含 ANY 的保守模式），尚未实现 Banerjee bounds

#include "affineAnalysis.hpp"
#include "loopInfo.hpp"

#include <vector>

class DependenceAnalysis {
public:
    DependenceAnalysis(const LoopInfo &LI, AffineAnalysis &AA)
        : LI_(&LI), AA_(&AA) {}

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

private:
    Value             *gepBase(GetElementPtrInst *gep) const;
    GetElementPtrInst *accessGEP(Instruction *acc) const;
    bool               sameBase(Value *a, Value *b) const;

    const LoopInfo  *LI_;
    AffineAnalysis  *AA_;
};
