#pragma once
// IR 值基类：所有 IR 实体（常量、指令、基本块、函数等）的基类，维护 use-def 链

#include "type.hpp"

#include <list>
#include <string>

class Value;
class Instruction;

// Use 边：记录某个 Value 被哪个指令的第几个操作数引用
struct Use {
    Value* val_;          // 使用者（某条指令）
    unsigned int arg_no_; // 该 Value 在指令中的操作数序号，如 func(a,b) 中 a=0, b=1
    Use(Value* val, unsigned int no) : val_(val), arg_no_(no) {}
};

// 所有 IR 值的基类，维护 use-def 信息
class Value {
public:
    explicit Value(Type* ty, const std::string& name = "") : type_(ty), name_(name) {}
    ~Value() = default;
    virtual std::string print() = 0;

    // 移除某个 Value 对 this 的所有引用
    void remove_use(Value* val) {
        auto is_val = [val](const Use& use) { return use.val_ == val; };
        use_list_.remove_if(is_val);
    }

    // 添加 use 记录，返回迭代器供后续删除
    std::list<Use>::iterator add_use(Value* val, unsigned arg_no) {
        use_list_.emplace_back(Use(val, arg_no));
        std::list<Use>::iterator re = use_list_.end();
        return --re;
    }

    // 删除迭代器指出的 use
    void remove_use(std::list<Use>::iterator it) {
        use_list_.erase(it);
    }

    // user 的第 i 个操作数不再使用 this
    bool remove_used(Instruction* user, unsigned int i);

    // 将所有使用 this 的地方替换为 new_val
    void replace_all_use_with(Value* new_val);
    Type* type_;
    std::string name_;
    std::list<Use> use_list_;  // 所有引用该 Value 的 Use 集合
};

// 将 Value 按操作数格式打印（带 @ 或 % 前缀）
std::string print_as_op(Value* v, bool print_ty);
