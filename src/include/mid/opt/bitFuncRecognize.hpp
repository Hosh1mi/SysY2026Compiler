#pragma once
// BitFuncRecognize —— 将按位模拟函数识别为原生位运算。
//
// 对候选函数体做按位抽象解释，在调用点改写为 and/or/xor/移位等 IR。
//
// 典型支持形式：
//   逐 bit 拼装的 AND / OR / XOR
//   常量或参数化的 SHL / LSHR（结构可证）
//
// 不依赖函数名。无法从位向量消歧的模式（如常量 LSHR 与有符号除法
// 歧义）不改写。成功后调用点变为对应原生运算。

#include "pass.hpp"

class BitFuncRecognize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "BitFuncRecognize"; }
};
