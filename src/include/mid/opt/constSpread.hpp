#include "pass.hpp"

class ConstSpread : public Pass {
public:
    void execute(Module *module) override;
private:
    bool foldConstants(Function *func);
    Constant *computeConstOp(Instruction::OpID op, Constant *lhs, Constant *rhs, Type *ty);
};
