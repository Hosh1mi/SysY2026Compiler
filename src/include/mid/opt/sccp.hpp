#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"
class SCCP : public Pass {
public:
    void execute(Module *module) override;
private:
    bool runOnFunction(Function *func);
};