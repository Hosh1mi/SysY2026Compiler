#pragma once
#include "pass.hpp"

class DimArrayArgSimplify : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);
};