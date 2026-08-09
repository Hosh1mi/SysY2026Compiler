#pragma once
// IdiomRecognize —— 将 memset/memcpy 语义循环识别为库调用。
//
// 匹配填充/拷贝语义的循环并改写为 libc 调用。
//
// 典型支持形式：
//   for (i) A[i] = 0/c → memset
//   for (i) A[i] = B[i] → memcpy
//
// 仅在语义与别名可完全证明时改写；否则保留原循环。

#include "../analysis/analysisManager.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class IdiomRecognize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IdiomRecognize"; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);
};
