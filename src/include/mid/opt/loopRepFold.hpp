#pragma once
#include "../analysis/loopInfo.hpp"
#include "../analysis/scalarEvolution.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>

// LoopRepFold: 循环重复折叠
// 识别并消除"纯重复计数循环"：while (r < R) { total += f(data); r++; }
// 其中 f(data) 不依赖 r 且不修改 data，变换为: total += f(data) * R（单次执行+乘法）
// 主要目标：many_mat_cal 中 157 亿次迭代 → T² 次计算 + 1 次乘法
// 仿射路径：total += a*i+b（i 为常量初值/正步长 IV，界为常量）经 SCEV
// 识别后直接闭式求和为常量并整体删除循环。
// 循环结构统一来自 LoopInfo（plan 阶段 3.1）。
class LoopRepFold : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopRepFold"; }

private:
    void runOnFunction(Function *func, AnalysisManager *AM);
    bool isLoopInvariant(Value *val, const std::set<BasicBlock *> &blocks);
    bool isCountingIV(PhiInst *phi, const Loop &loop, BasicBlock *latch,
                      long long *init, long long *stride);
    bool tryFold(Loop &loop, Module *module, ScalarEvolution *SE);
    bool tryFoldAffineSum(Loop &loop, Module *module, ScalarEvolution *SE,
                          BasicBlock *latch, PhiInst *ivPhi, PhiInst *totalPhi,
                          BasicBlock *loopExit, Value *bound, Value *totalInit,
                          Value *totalLatch, long long ivInit, long long ivStride);
    // 模仿射递推折叠：total = (total + c) % m（c>0,m>0 常量，IV 0/1）。
    // 闭式 (total0 + c*N) % m 仅在 total0>=0 且无 i32 溢出时与逐次截断取模相等，
    // 故插入"非负 + 不溢出"运行时守卫，守卫不成立时回退原循环（永远正确）。
    bool tryFoldModularRecurrence(Loop &loop, Module *module, BasicBlock *latch,
                                  PhiInst *ivPhi, PhiInst *totalPhi,
                                  BasicBlock *loopExit, Value *bound,
                                  Value *totalInit, Value *totalLatch,
                                  long long ivInit, long long ivStride);
    std::set<BasicBlock *> modFolded_; // 已折叠的 header，防止重扫无限折叠
};
