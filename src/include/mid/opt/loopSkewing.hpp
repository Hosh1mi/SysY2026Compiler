#pragma once
// LoopSkewing —— 将仿射内层起点变换到 distance 坐标。
//
// 对完美嵌套做坐标变换，得到更规则的矩形波前迭代空间。
//
// 典型支持形式：
//   for (i) for (j = i + c; j < n; ++j) ...
//   for (i) for (j = a*i + b; j < n; ++j) ...
//     → distance 坐标下的矩形波前嵌套
//
// 仅仿射可分析的完美嵌套；依赖或形状无法证明时不变换。

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

#include <optional>
#include <string>

struct LoopSkewPlan {
    Loop *outer = nullptr;
    Loop *inner = nullptr;
    PhiInst *outerIV = nullptr;
    PhiInst *innerIV = nullptr;
    BinaryInst *innerUpdate = nullptr;
    ICmpInst *innerCompare = nullptr;
    Value *innerStart = nullptr;
    Value *innerBound = nullptr;
    BasicBlock *preheader = nullptr;
    BasicBlock *latch = nullptr;
    long long outerCoefficient = 0;
    long long offset = 0;
};

std::optional<LoopSkewPlan> analyzeLoopSkew(Loop &inner,
                                            std::string *reason = nullptr);
bool applyLoopSkew(const LoopSkewPlan &plan, Module *module);

class LoopSkewing : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopSkewing"; }

private:
    bool runOnFunction(Function *function, AnalysisManager *AM);
};
