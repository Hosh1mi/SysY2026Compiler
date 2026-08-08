#pragma once
#include "pass.hpp"

enum class CFGSimplifyMode {
    Lite,
    Full,
};

class CFGSimplify : public Pass {
public:
    explicit CFGSimplify(CFGSimplifyMode mode = CFGSimplifyMode::Full)
        : mode_(mode) {}
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override {
        return mode_ == CFGSimplifyMode::Full ? "CFGSimplify"
                                              : "CFGSimplifyLite";
    }
private:
    CFGSimplifyMode mode_;
    bool runOnModule(Module *module);
    bool convertDiamondsToSelect(Function *func);
    bool hoistLoopInvariantBranch(Function *func);
};
