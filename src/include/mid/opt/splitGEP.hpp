#pragma once
// SplitGEP —— 剥离多维 GEP 中循环不变的行前缀。
//
// 将不变的外维地址计算外提到 preheader，内层相对行基址索引。
//
// 典型支持形式：
//   A[i][j]（i 对外层不变）→ row = &A[i][0]; row[j]
//
// 在向量化/并行化之后运行，以免改变那些 Pass 依赖的平坦 GEP 形态。

#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

class SplitGEP : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SplitGEP"; }

private:
    bool runOnLoop(Loop *loop, LoopInfo &LI,
                   const DominatorTreeAnalysis &DT);
};
