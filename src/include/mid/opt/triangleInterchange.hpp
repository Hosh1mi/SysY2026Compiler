#pragma once

#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

#include <optional>
#include <string>

struct TriangleSchedulePlan {
    Loop *outer = nullptr;
    Loop *inner = nullptr;
    PhiInst *outerIV = nullptr;
    PhiInst *distanceIV = nullptr;
    Value *extent = nullptr;
    Value *innerStart = nullptr;
    Value *innerBound = nullptr;
    Value *bodyIndex = nullptr;
    BasicBlock *outerPreheader = nullptr;
    BasicBlock *outerExit = nullptr;
    BasicBlock *innerPreheader = nullptr;
    BasicBlock *innerLatch = nullptr;
    long long offset = 0;
};

class TriangleInterchange : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "TriangleInterchange"; }

private:
    bool runOnFunction(Function *function);
};
