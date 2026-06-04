#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

class InstCombine : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);
};
