#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

// GVN（支配树值编号，DVNT）：循环管线之后的全局冗余消除。
// 相对 EarlyCSE 的增量：phi 折叠/合并、内联后纯调用 CSE、
// 以及消除 unroll/IVSR 暴露的重复 GEP 与标量运算。
class GVN : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "GVN"; }
};
