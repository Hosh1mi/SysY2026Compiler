#include "pass.hpp"

class InstructionCombine : public Pass {
public:
    void execute(Module *module) override;
private:
    bool simplify(Function *func);
};