#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"

class RemoveRedundantPhis : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "RemoveRedundantPhis"; }

private:
    void runOnFunction(Function *func);
    bool eliminateTrivialPhi(PhiInst *phi);
};