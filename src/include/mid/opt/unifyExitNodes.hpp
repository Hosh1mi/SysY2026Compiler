#pragma once

#include "pass.hpp"
#include "../ir/ir.hpp"

class UnifyExitNodes : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "UnifyExitNodes"; }

private:
    bool runOnFunction(Function *func);
};
