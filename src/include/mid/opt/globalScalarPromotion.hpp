#pragma once
// GlobalScalarPromotion —— 未逃逸整型标量全局提升为局部镜像。
//
// 用函数内 alloca 缓存全局标量，热路径上去掉直接全局 load/store，
// 在 return 处写回。
//
// 典型支持形式：
//   全局 int g; 函数内反复读写 g → 局部 alloca 镜像
//   ret 前 store 回全局，保持可观察语义
//
// 仅整型标量；地址逃逸或经不纯 call 可见则不提升。随后由 Mem2Reg
// 将镜像提升为 SSA。

#include "pass.hpp"

class GlobalScalarPromotion : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "GlobalScalarPromotion"; }
};
