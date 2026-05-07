#pragma once
#include "pass.hpp"

class InlineExpand : public Pass {
public:
    void execute(Module *module) override;
};