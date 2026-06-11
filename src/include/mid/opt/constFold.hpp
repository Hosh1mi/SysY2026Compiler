#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

class ConstantFold : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "ConstantFold"; }
    bool convergenceRelevant() const override { return false; }

private:
    // 对单个函数执行一遍折叠，返回是否有改动
    bool runOnFunction(Function *func, Module *module);

    // 尝试折叠单条指令，若成功返回新常量，否则返回 nullptr
    Constant* tryFold(Instruction *instr, Module *module);

    // 检查两个操作数是否都是 ConstantInt 或 ConstantFloat，并保证类型一致
    bool bothConstant(Value *op0, Value *op1);

    // 折叠二元运算
    Constant* foldBinary(BinaryInst *bin, Module *module);

    // 折叠整型比较
    Constant* foldICmp(ICmpInst *icmp, Module *module);

    // 折叠浮点比较（使用有序比较语义）
    Constant* foldFCmp(FCmpInst *fcmp, Module *module);

    // 折叠类型转换指令（zext / fptosi / sitofp）
    Constant* foldUnary(UnaryInst *unary, Module *module);
};