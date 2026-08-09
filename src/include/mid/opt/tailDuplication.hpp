#pragma once
// TailDuplication —— 拆散多前驱的 phi+ret 返回块。
//
// 将汇合返回块复制回各前驱，消除出口 phi（在有收益时）。
//
// 典型支持形式：
//   A/B → ret_bb: r = phi(a, b); ret r → A/B 各自 ret
//   多前驱汇合的纯 phi+ret 出口块被溶解回前驱
//
// 是 UnifyExitNodes 在有利情形下的逆变换。

#include "pass.hpp"

class TailDuplication : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "TailDuplication"; }
private:
    bool runOnFunction(Function *func);
};
