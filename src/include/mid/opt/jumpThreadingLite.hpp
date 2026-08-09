#pragma once
// JumpThreadingLite —— 将前驱边穿线到已知后继。
//
// 当中段块形如 phi + icmp + 条件 br，且边事实可判定方向时，把前驱
// 直接接到对应后继，缩短控制路径。
//
// 典型支持形式：
//   A → M → T/F，M 为 phi + icmp + cond br，A 边上恒真 → A 直跳 T
//   同构的恒假边 → A 直跳 F
//   由此消除只为该边服务的中间跳转
//
// 不穿线经过 loop header。成功后减少中间块与冗余条件分支。

#include "pass.hpp"

class JumpThreadingLite : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "JumpThreadingLite"; }

private:
    bool runOnModule(Module *module);
    bool runOnFunction(Function *func, AnalysisManager *AM);
};
