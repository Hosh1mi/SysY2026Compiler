#pragma once
// TriangleInterchange —— 将三角波前巢交换为 distance 外层 + 独立 lane 内层。
//
// 变换迭代坐标，使内层 lane 相互独立，便于后续并行/向量化。
//
// 典型支持形式：
//   for (i) for (j = 0; j <= i; ++j) 依赖波前
//     → outer: distance；inner: 相互独立的 lane
//
// 仅在依赖与仿射形状可证时变换；否则保持原巢。

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

#include <optional>
#include <string>

struct TriangleSchedulePlan {
    Loop *outer = nullptr;
    Loop *inner = nullptr;
    PhiInst *outerIV = nullptr;
    PhiInst *distanceIV = nullptr;
    Value *extent = nullptr;
    Value *innerStart = nullptr;
    Value *innerBound = nullptr;
    Value *bodyIndex = nullptr;
    BasicBlock *outerPreheader = nullptr;
    BasicBlock *outerExit = nullptr;
    BasicBlock *innerPreheader = nullptr;
    BasicBlock *innerLatch = nullptr;
    long long offset = 0;
};

class TriangleInterchange : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangleInterchange"; }

private:
    bool runOnFunction(Function *function, AnalysisManager *AM);
};
