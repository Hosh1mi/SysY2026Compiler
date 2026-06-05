#pragma once

#include "pass.hpp"

class LocalCopyPropagation : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "LocalCopyPropagation"; }
private:
    bool runOnFunction(Function *func);
    bool isIdentityCopy(Instruction *inst);
};
