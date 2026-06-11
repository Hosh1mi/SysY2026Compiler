#pragma once
#include "pass.hpp"
#include "../analysis/loopInfo.hpp"

// LCSSA（Loop-Closed SSA）：循环内定义的值在循环外的每个 use 都改经
// exit 块顶部的单值 phi（%v.lcssa = phi [%v, <exit的循环内前驱>]）。
//
// 价值：循环变换（rotate/unroll/vectorize）改写循环体时，循环外的
// live-out 只需更新 exit phi 的 incoming，无需扫描全函数的 use。
//
// 前置：LoopSimplify 的 dedicated exits（exit 块前驱全在循环内，
// 否则单值 phi 不成立）。
//
// 维持策略（plan 2.2a）：只在 loop pipeline 内维持；pipeline 之后的
// 清理 pass（DCE eliminateTrivialPhis）会把单入边 phi 收缩掉，属预期。
//
// 处理顺序：由内向外（innermost first）。内层的 lcssa phi 本身是外层
// 循环内的定义，外层处理时按需再包一层。
//
// 局限：use 点不被任何已插 exit phi 支配时（多 exit 在 use 前汇合的
// 非 phi 使用），该 use 保持原样——LoopVerify L3 会以告警暴露，
// 此时再考虑完整 SSA 重建。
class LCSSA : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LCSSA"; }

private:
    bool runOnFunction(Function *func);
    bool runOnLoop(Loop *loop, LoopInfo &LI);
};
