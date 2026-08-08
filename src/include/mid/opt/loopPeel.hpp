#pragma once
// LoopPeel: one-iteration peel for canonical 2-BB loops that may have a
// latch side-exit.  Unlike LoopUnroll's fast path, side exits are preserved.

#include "pass.hpp"

#include <string>

class LoopPeel : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopPeel"; }
    LoopForm requiredLoopForm() const override { return LoopForm::LCSSA; }
};
