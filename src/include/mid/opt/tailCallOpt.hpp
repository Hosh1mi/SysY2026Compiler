#pragma once

#include "pass.hpp"
#include "../ir/ir.hpp"

// 识别尾调用位点并在 CallInst 上打 tail 标记，供后端在 ABI 允许时发 b。
// 模式 2（call + br ret_bb）会先规范化为模式 1（call + ret），便于后端同块处理。
class TailCallOpt : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TailCallOpt"; }

private:
    bool runOnFunction(Function *func);
};
