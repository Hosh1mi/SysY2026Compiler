#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"

#include <iostream>
#include <string>

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

Function* Module::getMainFunc() {
    for (auto f : function_list_) {
        if (f->name_ == "main") {
            return f;
        }
    }
    return nullptr;
}
