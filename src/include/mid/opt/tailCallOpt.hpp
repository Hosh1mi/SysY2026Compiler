#pragma once
// TailCallOpt —— 规范化尾调用形态并为后端打 tail 标记。
//
// 识别可复用当前栈帧的尾调用位点，统一形状后在 CallInst 上标记 tail。
//
// 典型支持形式：
//   call g(...); ret                  // 已是标准尾调用
//   call g(...); br ret_bb → 规范化为 call + ret
//
// 不改写 callee 本体。自递归消环由 TailRecursionEliminate 负责。

#include "pass.hpp"
#include "../ir/ir.hpp"

class TailCallOpt : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TailCallOpt"; }

private:
    bool runOnFunction(Function *func);
};
