#pragma once
#include "../analysis/preservedAnalyses.hpp"
#include "../ir/ir.hpp"

class AnalysisManager;

enum class LoopForm {
    None,
    Simplified,
    LCSSA,
};

struct PassRunResult {
    bool changed = false;
    PreservedAnalyses preserved = PreservedAnalyses::all();
};

class Pass {
public:
    virtual void execute(Module *module) = 0;
    // A pass that leaves the IR unchanged returns all(). Legacy passes use
    // the conservative default and are treated as changing the IR.
    virtual PreservedAnalyses execute(Module *module, AnalysisManager &AM) {
        (void)AM;
        execute(module);
        return PreservedAnalyses::none();
    }
    virtual PassRunResult runPass(Module *module, AnalysisManager &AM) {
        PreservedAnalyses preserved = execute(module, AM);
        return {!preserved.preservesAll(), preserved};
    }
    virtual LoopForm requiredLoopForm() const { return LoopForm::None; }
    virtual LoopForm establishedLoopForm() const { return LoopForm::None; }
    virtual std::string name() const = 0;
    virtual ~Pass() = default;
};
