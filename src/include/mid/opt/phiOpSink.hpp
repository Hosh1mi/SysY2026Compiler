#pragma once

#include "pass.hpp"

class PhiOpSink : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "PhiOpSink"; }

private:
    bool runOnFunction(Function *func);
    bool trySinkPhi(PhiInst *phi, Function *func);
};
