#pragma once

#include "pass.hpp"

class DeadCodeDelete : public Pass {
public:
    void execute(Module *module) override;
private:
    bool removeDeadInstructions(Function *func);
    bool isCriticalInstruction(Instruction *inst);
};
