// GlobalVariable::print() —— 全局变量/常量的 IR 输出

#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/constant.hpp"

#include <string>

// @a = global [5 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4]
// @c = constant [4 x i32] [i32 6, i32 7, i32 8, i32 9]
std::string GlobalVariable::print() {
    std::string global_val_ir;
    global_val_ir += print_as_op(this, false);
    global_val_ir += " = ";
    global_val_ir += (this->is_const_ ? "constant " : "global ");
    global_val_ir += static_cast<PointerType*>(this->type_)->contained_->print();
    global_val_ir += " ";
    global_val_ir += this->init_val_->print();
    return global_val_ir;
}
