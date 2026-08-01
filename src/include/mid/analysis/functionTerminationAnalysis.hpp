#pragma once

// Prove that an internal function returns on every defined execution.  The
// result is deliberately conservative: recursive calls, unknown callees and
// loops without a recognized finite control recurrence are rejected.

#include "loopInfo.hpp"

#include <map>

class FunctionTerminationAnalysis {
public:
    explicit FunctionTerminationAnalysis(Module *module) : module_(module) {}

    bool mustReturn(Function *function);

private:
    enum class State {
        Unknown,
        Visiting,
        Returns,
        MayNotReturn,
    };

    bool analyzeFunction(Function *function);
    bool loopIsFinite(const Loop &loop) const;

    Module *module_ = nullptr;
    std::map<Function *, State> states_;
};
