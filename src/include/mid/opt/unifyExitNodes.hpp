#pragma once
// UnifyExitNodes —— 多出口 return 合并为单一统一出口。
//
// 将函数内多个 ret 汇合到统一返回块，便于后续出口相关变换。
//
// 典型支持形式：
//   if (c) return a; else return b; → 统一 ret_bb + phi + ret
//   多条早期 return 路径汇合到单一出口
//
// 成功后函数仅保留一个返回点。逆向拆分（有收益时）由 TailDuplication
// 负责。

#include "pass.hpp"
#include "../ir/ir.hpp"

class UnifyExitNodes : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "UnifyExitNodes"; }

private:
    bool runOnFunction(Function *func);
};
