#pragma once

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

#include <optional>
#include <string>

struct LoopSkewPlan {
    Loop *outer = nullptr;
    Loop *inner = nullptr;
    PhiInst *outerIV = nullptr;
    PhiInst *innerIV = nullptr;
    BinaryInst *innerUpdate = nullptr;
    ICmpInst *innerCompare = nullptr;
    Value *innerStart = nullptr;
    Value *innerBound = nullptr;
    BasicBlock *preheader = nullptr;
    BasicBlock *latch = nullptr;
    long long outerCoefficient = 0;
    long long offset = 0;
};

std::optional<LoopSkewPlan> analyzeLoopSkew(Loop &inner,
                                            std::string *reason = nullptr);
bool applyLoopSkew(const LoopSkewPlan &plan, Module *module);

class LoopSkewing : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopSkewing"; }

private:
    bool runOnFunction(Function *function, AnalysisManager *AM);
};
