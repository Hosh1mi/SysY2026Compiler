#pragma once

#include "pass.hpp"

// Delay loop-carried signed remainder operations by accumulating independent
// contributions in i64 and reducing once on the loop exit.  It runs after
// LoopRepFold has consumed closed forms and before LoopUnroll destroys the
// canonical modular-recurrence shape.
class LoopModuloDelay : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopModuloDelay"; }
};
