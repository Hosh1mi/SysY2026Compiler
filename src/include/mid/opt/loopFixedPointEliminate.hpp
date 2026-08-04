#pragma once

#include "../analysis/basicAliasAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../ir/ir.hpp"
#include "pass.hpp"

// Stop a counted loop once all of its non-control loop-carried SSA state
// reaches a runtime fixed point.
//
// The transform is deliberately conservative:
//   - the unit-stride count recurrence must be finite and used only by the
//     trip-count guard (in the latch for do-while, or in the header for
//     while);
//   - adding an early latch exit to a header-tested while is limited to leaf
//     loops, because nested-loop LCSSA boundaries would otherwise change;
//   - the loop may not contain calls;
//   - loop stores must not alias any loop load;
//   - every live non-control header phi is compared with its backedge value;
//   - header phis that only feed their own update (dead self-recurrences)
//     are ignored — they cannot affect observable state.
//
// If the state is unchanged, another iteration has identical scalar inputs
// and cannot observe memory written by the previous iteration. It therefore
// repeats the same stores and live-out values, so the remaining counted
// iterations are redundant.
class LoopFixedPointEliminate : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override {
        return "LoopFixedPointEliminate";
    }

private:
    bool runOnFunction(Function *func, BasicAliasAnalysis &AA);
    bool tryTransform(Loop &loop, Function *func, BasicAliasAnalysis &AA);
};
