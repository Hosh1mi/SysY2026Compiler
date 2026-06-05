#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

class GVN : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "GVN"; }
};
