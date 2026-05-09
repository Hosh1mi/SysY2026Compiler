#pragma once
#include "../ir/ir.hpp"
#include "pass.hpp"

class RemoveRedundantPhis : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);
    bool eliminateTrivialPhi(PhiInst *phi);
};