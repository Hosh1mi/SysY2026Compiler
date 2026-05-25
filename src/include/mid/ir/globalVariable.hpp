#pragma once
// IR 全局变量/常量：对应 @a = global / @c = constant

#include "value.hpp"
#include "module.hpp"

class Constant;

// @c = global [4 x i32] [i32 6, i32 7, i32 8, i32 9]
// @a = constant [5 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4]
class GlobalVariable : public Value {
public:
    GlobalVariable(std::string name, Module* m, Type* ty, bool is_const, Constant* init = nullptr)
        : Value(m->get_pointer_type(ty), name), is_const_(is_const), init_val_(init) { m->add_global_variable(this); }
    virtual std::string print() override;
    bool is_const_;        // 是否为常量
    Constant* init_val_;   // 初始值
};
