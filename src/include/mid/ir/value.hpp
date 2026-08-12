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

// use-def 图中的反向边：它存放在被引用 Value 的 use_list_ 中，指出哪个指令的
// 哪个操作数正在使用该值。Use 不拥有 user_。
struct Use {
    Instruction* user_;          // 使用者
    unsigned int operand_index_; // 该 Value 在使用者中的操作数序号
    // 建立一条从 Value 到 user[index] 的反向索引。
    Use(Instruction* user, unsigned int index)
        : user_(user), operand_index_(index) {}
};

// 所有可作为 IR 操作数的对象基类。类型和名字描述值本身，use_list_ 维护全部使用者；
// Value 不负责释放类型、使用者或派生对象。
class Value {
public:
    // name 为空表示打印前尚未命名，常量则通常始终无名。
    explicit Value(Type* ty, const std::string& name = "") : type_(ty), name_(name) {}
    ~Value() = default;
    // 返回完整定义或声明文本；操作数位置应使用 print_as_op()。
    virtual std::string print() = 0;

    // 注册 user 的一个操作数引用，并返回稳定迭代器供 Instruction O(1) 删除。
    std::list<Use>::iterator add_use(Instruction* user, unsigned operand_index) {
        use_list_.emplace_back(user, operand_index);
        return std::prev(use_list_.end());
    }

    // 删除由 add_use 返回的那条反向边；调用方须保证迭代器属于本 Value。
    void remove_use(std::list<Use>::iterator it) {
        use_list_.erase(it);
    }

    // 把所有 user 的对应操作数改为 new_val；通过 set_operand 同步两侧 use-def。
    void replace_all_use_with(Value* new_val);

    // 语义标记（见 SemFlag）：源级/分析事实，随 IR 对象持久并在 print() 中输出
    // 增加一项已经证明且仍有效的语义事实。
    void setSemFlag(SemFlag f)   { sem_flags_ |=  static_cast<uint32_t>(f); }
    // 在变换使某项事实失效时清除它。
    void clearSemFlag(SemFlag f) { sem_flags_ &= ~static_cast<uint32_t>(f); }
    // 查询某项事实是否存在；返回 false 只表示未知，不表示其反命题成立。
    bool hasSemFlag(SemFlag f) const { return (sem_flags_ & static_cast<uint32_t>(f)) != 0; }
    // 克隆等价值时复制整组语义位；调用方负责判断每项事实是否仍成立。
    void copySemFlagsFrom(const Value *other) {
        if (other) sem_flags_ = other->sem_flags_;
    }

    Type* type_;
    std::string name_;
    std::list<Use> use_list_;  // 所有引用该 Value 的 Use 集合
    uint32_t sem_flags_ = 0;   // 语义标记位集合，见 SemFlag
};

// 将 Value 按操作数格式打印。print_ty 控制是否带类型，并自动选择 @、% 或常量文本。
std::string print_as_op(Value* v, bool print_ty);
