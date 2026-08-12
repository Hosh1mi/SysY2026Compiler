#pragma once
// IR 值基类：所有 IR 实体（常量、指令、基本块、函数等）的基类，维护 use-def 链

#include "type.hpp"
#include "semFlags.hpp"

#include <cstdint>
#include <iterator>
#include <list>
#include <string>

class Value;
class Instruction;

// Use 边：记录某个 Value 被哪个指令的第几个操作数引用
struct Use {
    Instruction* user_;          // 使用者
    unsigned int operand_index_; // 该 Value 在使用者中的操作数序号
    Use(Instruction* user, unsigned int index)
        : user_(user), operand_index_(index) {}
};

// 所有 IR 值的基类，维护 use-def 信息
class Value {
public:
    explicit Value(Type* ty, const std::string& name = "") : type_(ty), name_(name) {}
    ~Value() = default;
    virtual std::string print() = 0;

    // 添加 use 记录，返回迭代器供后续删除
    std::list<Use>::iterator add_use(Instruction* user, unsigned operand_index) {
        use_list_.emplace_back(user, operand_index);
        return std::prev(use_list_.end());
    }

    // 删除迭代器指出的 use
    void remove_use(std::list<Use>::iterator it) {
        use_list_.erase(it);
    }

    // 将所有使用 this 的地方替换为 new_val
    void replace_all_use_with(Value* new_val);

    // 语义标记（见 SemFlag）：源级/分析事实，随 IR 对象持久并在 print() 中输出
    void setSemFlag(SemFlag f)   { sem_flags_ |=  static_cast<uint32_t>(f); }
    void clearSemFlag(SemFlag f) { sem_flags_ &= ~static_cast<uint32_t>(f); }
    bool hasSemFlag(SemFlag f) const { return (sem_flags_ & static_cast<uint32_t>(f)) != 0; }
    void copySemFlagsFrom(const Value *other) {
        if (other) sem_flags_ = other->sem_flags_;
    }

    Type* type_;
    std::string name_;
    std::list<Use> use_list_;  // 所有引用该 Value 的 Use 集合
    uint32_t sem_flags_ = 0;   // 语义标记位集合，见 SemFlag
};

// 将 Value 按操作数格式打印（带 @ 或 % 前缀）
std::string print_as_op(Value* v, bool print_ty);
