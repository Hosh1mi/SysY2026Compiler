// Module::print / getMainFunc —— 顶层模块的 IR 输出

#include "../../include/mid/ir/module.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"

#include <iostream>
#include <string>

// IR 完整性验证
void Module::verify() {
    // TODO: 完整验证 use-def 链、SSA 属性、phi 节点前驱一致性、基本块完整性
    // 当前为占位实现，后续迭代中会补充具体检查逻辑
    for (auto func : function_list_) {
        if (func->is_declaration()) continue;
        for (auto bb : func->basic_blocks_) {
            // 基本块必须有终结指令
            auto term = bb->get_terminator();
            if (!term) {
                std::cerr << "[VERIFY] ERROR: basic block without terminator in "
                          << func->name_ << "\n";
                std::abort();
            }
        }
    }
}

// 输出整个模块的 IR 文本
std::string Module::print() {
    std::string module_ir;
    for (auto global_val : this->global_list_) {
        module_ir += global_val->print();
        module_ir += "\n";
    }
    for (auto func : this->function_list_) {
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
