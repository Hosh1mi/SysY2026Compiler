#pragma once
#include "pass.hpp"
#include "../ir/ir.hpp"

class CSE : public Pass {
public:
    void execute(Module *module) override;
};