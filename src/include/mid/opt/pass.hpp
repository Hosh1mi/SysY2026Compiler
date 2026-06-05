#pragma once
#include "../ir/ir.hpp"

class Pass {
public:
    virtual void execute(Module *module) = 0;
    virtual std::string name() const = 0;
    virtual ~Pass() = default;
};
