#pragma once
// IdiomRecognize：识别 memset 语义的纯 store 计数循环，改写为 libc memset 调用。

#include "../analysis/analysisManager.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class IdiomRecognize : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "IdiomRecognize"; }
    bool convergenceRelevant() const override { return true; }

private:
    bool runOnFunction(Function *func, AnalysisManager &AM);

    Function *memsetDecl_ = nullptr;
};
