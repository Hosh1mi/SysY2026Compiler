// Module::print / getMainFunc —— 顶层模块的 IR 输出

#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"

#include <iostream>
#include <string>

// IR 完整性验证：实现见 verify.cpp

// 输出整个模块的 IR 文本（未引用的外部声明不打印）
std::string Module::print() {
    std::string module_ir;
    for (auto global_val : this->global_list_) {
        module_ir += global_val->print();
        module_ir += "\n";
    }
    for (auto func : this->function_list_) {
        if (func->is_declaration() && func->use_list_.empty())
            continue;
        module_ir += func->print();
        module_ir += "\n";
    }
    return module_ir;
}

// 查找名为 "main" 的入口函数
Function* Module::getMainFunc() {
    for (auto f : function_list_) {
        if (f->name_ == "main") {
            return f;
        }
    }
    return nullptr;
}
