#pragma once
#include "pass.hpp"

class CFGSimplify : public Pass {
public:
    void execute(Module *module) override;
};