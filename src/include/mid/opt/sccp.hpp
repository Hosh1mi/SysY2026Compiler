#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"
class SCCP : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "SCCP"; }
private:
    bool runOnFunction(Function *func);
};
