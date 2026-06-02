#pragma once
// DependenceAnalysis: 内存访问之间的依赖方向分析
//
// 对一对访问 (acc1, acc2)（load 或 store）：
//   1. 检查是否可能别名（基址同一且 GEP 索引都是仿射）
//   2. 在共同嵌套的循环里求方向向量 direction[i] ∈ {<, =, >, *}
//      direction[i] 描述：从 acc1 的迭代点到 acc2 的迭代点，第 i 层 IV 的相对方向
//   3. 不能精确证明无依赖时保守返回 *（任意方向）
//
// 简化做法（适合本项目的方阵 matmul + 现有用例）：
//   - 只支持每层每个 GEP index 是一个 IV 的纯线性形式 c0 + c1·iv
//   - 用 GCD test：若两访问索引差 = 0 在循环范围内无整数解 → 无依赖
//   - 否则按系数符号给方向（含保守 *）

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
