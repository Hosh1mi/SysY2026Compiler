#pragma once
#include "../../mid/ir/ir.hpp"
#include <ostream>

class Arm64CodeGen {
public:
    Arm64CodeGen(Module *m, std::ostream &os) : m_(m), os_(os) {}
    void generate();

private:
    void emitGlobal(GlobalVariable *gv);
    void emitFunction(Function *f);
    void emitExtern(Function *f);

    Module *m_;
    std::ostream &os_;
};
