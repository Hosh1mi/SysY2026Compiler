#pragma once
// LoopFusion —— 融合边界相同的相邻同级规范 while。
//
// 在依赖与别名安全时，将两次相同 trip 的相邻循环并为一次遍历。
//
// 典型支持形式：
//   for (i) S1; for (i) S2;（同 bound、canonical while）→ 单循环含 S1;S2
//
// 要求 trip 恒等、无危险内存/SSA 依赖、无 call。若融合会破坏后续
// LoopInterchange 的可交换形态则拒绝。成功后仍保持 simplified + LCSSA。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/argumentAliasAnalysis.hpp"
#include "../analysis/loopInterchangeAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class BasicAliasAnalysis;

class LoopFusion : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopFusion"; }

private:
    // 规范 while 循环形态：header 做 iv<bound 出口测试，latch 无条件跳回。

    struct Shape {
        BasicBlock *preheader = nullptr;
        BasicBlock *header    = nullptr;
        BasicBlock *latch     = nullptr;
        BasicBlock *exit      = nullptr;
        BasicBlock *bodyEntry = nullptr;  // header 的循环内后继
        PhiInst    *iv        = nullptr;
        Value      *bound     = nullptr;
        Value      *backedge  = nullptr;  // IV phi 在 latch 的 incoming 值
        ICmpInst   *guard     = nullptr;
        bool ok = false;
    };

    bool runOnFunction(Function *func, AnalysisManager *AM);

    Shape analyzeShape(Loop *L) const;
    // 从 L1.exit 沿单后继链走到某个同级循环的 preheader；chain 收集途经块
    // （含 L1.exit 与 L2.preheader）。找不到同级后继循环时返回 nullptr。
    Loop *walkToSibling(const Shape &s1, Loop *L1, LoopInfo &LI,
                        std::vector<BasicBlock *> &chain) const;
    bool boundsEqual(const Shape &s1, const Shape &s2) const;
    bool planChainMotion(
        Loop *L1, Loop *L2, const Shape &s2,
        const std::vector<BasicBlock *> &chain,
        std::vector<PhiInst *> &bypassPhis,
        std::vector<Instruction *> &hoist,
        std::vector<Instruction *> &sink) const;
    bool callsArePure(Loop *L1, Loop *L2) const;
    bool noScalarCrossUse(Loop *L1, Loop *L2) const;
    bool headerContentSimple(const Shape &s2) const;
    // L2.header 中 phi 的初值在融合后必须在 L1.preheader 可用。
    bool phiInitsAvailable(Loop *L1, const Shape &s1, Loop *L2,
                           const Shape &s2,
                           const std::vector<BasicBlock *> &chain) const;
    // E2 / L2.bodyEntry 中 phi 的入边值在改接前驱后仍支配新前驱。
    bool exitUsesAvailable(Loop *L1, const Shape &s1, Loop *L2,
                           const Shape &s2,
                           const std::vector<BasicBlock *> &chain) const;
    bool memoryLegal(Loop *L1, Loop *L2, AffineAnalysis &AA) const;
    const char *profitabilityRejection(
        Loop *L1, Loop *L2, LoopInterchangeAnalysis &IA) const;

    void applyFusion(Function *func, const Shape &s1, const Shape &s2,
                     const std::vector<BasicBlock *> &chain,
                     const std::vector<PhiInst *> &bypassPhis,
                     const std::vector<Instruction *> &hoist,
                     const std::vector<Instruction *> &sink);

    const ArgumentAliasAnalysis *argAA_ = nullptr;
    const BasicAliasAnalysis *basicAA_ = nullptr;
};
