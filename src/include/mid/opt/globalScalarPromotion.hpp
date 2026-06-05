#pragma once
// Promotes scalar global variables to alloca'd locals within functions that
// have no remaining function calls (i.e., after full inlining). The allocas
// are then promoted to SSA registers by a subsequent Mem2Reg run.
#include "pass.hpp"
class GlobalScalarPromotion : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "GlobalScalarPromotion"; }
};
