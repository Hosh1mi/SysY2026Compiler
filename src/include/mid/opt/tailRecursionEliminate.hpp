#include "pass.hpp"

class TailRecursionEliminate : public Pass {
    public:
        void execute(Module *module) override;
    std::string name() const override { return "TailRecursionEliminate"; }
    
    private:
        bool isTailRecursive(Function *func);
        void eliminateTailRecursion(Function *func, Module *module);
    };
