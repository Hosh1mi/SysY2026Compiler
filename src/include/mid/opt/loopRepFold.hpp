#pragma once
// LoopRepFold —— 折叠纯重复累加与可闭式求和的循环。
//
// 将“重复加不变式”或仿射 SCEV 求和等改为闭式，安全时也折叠模递推。
//
// 典型支持形式：
//   total += f;（f 相对 IV 不变）→ total += f * R
//   total += a*i+b → SCEV 闭式求和后删循环
//   可证安全的模递推折叠
//
// 有副作用或依赖无法闭式化时不折叠。

#include "../analysis/loopInfo.hpp"
#include "../analysis/scalarEvolution.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>

enum class LoopRepFoldMode {
    Lite,
    Aggressive,
};

class LoopRepFold : public Pass {
public:
    explicit LoopRepFold(LoopRepFoldMode mode = LoopRepFoldMode::Aggressive)
        : mode_(mode) {}
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override {
        return mode_ == LoopRepFoldMode::Lite ? "LoopRepFoldLite"
                                             : "LoopRepFold";
    }

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
    bool tryFoldSummableModularRecurrence(Loop &loop, Module *module);
    Function *getSummableModSumDeclaration(Module *module);
    std::set<BasicBlock *> modFolded_; // 已折叠的 header，防止重扫无限折叠
    Function *summableModSumDecl_ = nullptr;
    LoopRepFoldMode mode_;
};
