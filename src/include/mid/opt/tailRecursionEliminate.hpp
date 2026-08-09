// TailRecursionEliminate —— 自递归尾调用转为参数 phi 循环。
//
// 将本函数对自己的尾调用改写为循环，避免递归栈增长。
//
// 典型支持形式：
//   return f(a', b');                    // call + ret
//   ...; br ret_bb; ret_bb: r = phi(...); ret r  // call + br ret_bb
//
// 仅处理自调用。成功后形成 header 上的参数 phi + 回边更新。
// 异函数尾调用的规范化与标记由 TailCallOpt 负责。

#include "pass.hpp"

class TailRecursionEliminate : public Pass {
    public:
        void execute(Module *module) override;
    std::string name() const override { return "TailRecursionEliminate"; }
    
    private:
        bool isTailRecursive(Function *func);
        void eliminateTailRecursion(Function *func, Module *module);
    };
