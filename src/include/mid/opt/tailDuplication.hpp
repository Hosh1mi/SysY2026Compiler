#pragma once
#include "pass.hpp"

class TailDuplication : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "TailDuplication"; }
private:
    bool runOnFunction(Function *func);
};
