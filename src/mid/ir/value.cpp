// Value::replace_all_use_with —— 维护 use-def 链的核心方法

#include "../../include/mid/ir/value.hpp"
#include "../../include/mid/ir/instruction.hpp"

// 将所有引用 this 的指令的操作数替换为 new_val
void Value::replace_all_use_with(Value* new_val) {
    if (new_val == this)
        return;
    while (!use_list_.empty()) {
        Use use = use_list_.front();
        use.user_->set_operand(use.operand_index_, new_val);
    }
}
