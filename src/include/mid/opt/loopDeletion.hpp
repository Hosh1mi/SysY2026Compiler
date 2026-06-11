#pragma once
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

// LoopDeletion: 删除"什么都不算"的循环（plan 阶段 3.3.1）。
// 可删条件（全部满足）：
//   1. 有 preheader、单一 exit；
//   2. 可证终止：有 canonicalIV（init=0, +1, slt 不变上界——while 形，
//      与 LoopRotate 的规范 IV 门控天然兼容，这类循环不会被旋转）；
//   3. 循环内无 store、无 call（含纯 call，保守）；
//   4. 循环内定义的值没有任何循环外使用（exit phi 引用循环内定义值也算）；
//   5. exit 块 phi 中来自循环的入边值全部循环不变且彼此相等
//      （删除后收拢为一条来自 preheader 的入边）。
// 变换：preheader 直跳 exit，整体移除循环块。
class LoopDeletion : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LoopDeletion"; }

private:
    bool runOnFunction(Function *func);
    bool tryDelete(Loop &loop, Function *func);
};
