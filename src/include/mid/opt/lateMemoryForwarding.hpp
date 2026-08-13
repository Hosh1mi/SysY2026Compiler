#pragma once

// LateMemoryForwarding removes loads whose value is supplied by a preceding
// must-alias store.  It is intentionally memory-only: expression CSE remains
// the responsibility of EarlyCSE and GVN.

#include "pass.hpp"

class LateMemoryForwarding : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LateMemoryForwarding"; }
};
