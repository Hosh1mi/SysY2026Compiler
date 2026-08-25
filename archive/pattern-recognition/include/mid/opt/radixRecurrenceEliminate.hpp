#pragma once
// RadixRecurrenceEliminate —— 消除 radix-2 模乘累加式自递归。
//
// 将可证等价于 (a*b) srem M 的二分递推降为 MulMod 或按位行走。
//
// 典型支持形式：
//   F(a,0)=0; F(a,1)=a%srem M;
//   F(a,b)=(2*F(a,b/2))%M，奇数再 +(a)%M
//
// 结构识别，不依赖函数名。无法证明溢安全时走保持原运算次序的
// bit-walking；非正 b 保持原语义零结果。

#include "pass.hpp"

class RadixRecurrenceEliminate : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "RadixRecurrenceEliminate"; }
};
