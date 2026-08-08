#pragma once
#include "../analysis/preservedAnalyses.hpp"
#include "../ir/ir.hpp"

class AnalysisManager;

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
    virtual std::string name() const = 0;
    virtual ~Pass() = default;
};
