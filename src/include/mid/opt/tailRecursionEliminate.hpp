#include "pass.hpp"

class TailRecursionEliminate : public Pass {
    public:
        void execute(Module *module) override;
    
    private:
        bool isTailRecursive(Function *func); // 检查函数是否所有对自身的调用都是尾调用
        void eliminateTailRecursion(Function *func, Module *module);
    };