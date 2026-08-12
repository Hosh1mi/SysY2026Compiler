#pragma once
// IR 全局变量/常量：对应 @a = global / @c = constant

#include "value.hpp"
#include "module.hpp"

class Constant;

// 模块级存储对象。它作为 Value 时的类型是 ty*，init_val_ 的类型则是 ty。
// 例如 @c = global [4 x i32] [...]；@a = constant [5 x i32] [...]。
class GlobalVariable : public Value {
public:
    // 构造后立即注册到 m；init 为空仅用于尚未提供初始化值的内部场景。
    GlobalVariable(std::string name, Module* m, Type* ty, bool is_const, Constant* init = nullptr)
        : Value(m->get_pointer_type(ty), name), is_const_(is_const), init_val_(init) { m->add_global_variable(this); }
    // 输出一条带 @name、global/constant 和初始化器的模块级定义。
    virtual std::string print() override;
    bool is_const_;        // 是否为常量
    Constant* init_val_;   // 初始值
};
