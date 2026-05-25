// Value::replace_all_use_with / remove_used —— 维护 use-def 链的核心方法

#include "../../include/mid/ir/value.hpp"
#include "../../include/mid/ir/instruction.hpp"

// 将所有引用 this 的指令的操作数替换为 new_val
void Value::replace_all_use_with(Value* new_val) {
    auto use_list_copy = use_list_;  // 拷贝避免迭代中修改
    for (auto use : use_list_copy) {
        auto val = dynamic_cast<Instruction*>(use.val_);
        val->set_operand(use.arg_no_, new_val);
    }
}

// user 的第 i 个操作数不再引用 this，清除对应 use 记录
bool Value::remove_used(Instruction* user, unsigned int i) {
    if (this != user->operands_[i]) {
        return false;
    }
    auto pos = user->use_pos_[i];
    use_list_.erase(pos);
    user->operands_[i] = nullptr;  // 标记失效，防止 set_operand 再次删除
    return true;
}
