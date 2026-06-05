#pragma once
#include "pass.hpp"

class CFGSimplify : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "CFGSimplify"; }
private:
    bool convertDiamondsToSelect(Function *func);
};