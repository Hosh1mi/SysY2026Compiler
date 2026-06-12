#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"
class SCCP : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "SCCP"; }
    bool convergenceRelevant() const override { return false; }
private:
    bool runOnFunction(Function *func);
};