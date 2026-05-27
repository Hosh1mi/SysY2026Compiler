#pragma once
// IR 常量：编译期已知的值，无名（name == ""）

#include "value.hpp"

#include <vector>

// 常量基类
class Constant : public Value {
public:
    Constant(Type* ty, const std::string& name = "") : Value(ty, name) {}
    virtual ~Constant() = default;
};

// i32 常量，如 i32 42
class ConstantInt : public Constant {
public:
    ConstantInt(Type* ty, int val) : Constant(ty, ""), value_(val) {}
    virtual std::string print() override;
    int value_;
};

// float 常量，如 0x4057C21FC0000000
class ConstantFloat : public Constant {
public:
    ConstantFloat(Type* ty, float val) : Constant(ty, ""), value_(val) {}
    virtual std::string print() override;
    float value_;
};

// 数组常量，如 [3 x i32] [i32 42, i32 11, i32 74]
class ConstantArray : public Constant {
public:
    ConstantArray(ArrayType* ty, const std::vector<Constant*>& val) : Constant(ty, "") { this->const_array.assign(val.begin(), val.end()); }
    ~ConstantArray() = default;
    virtual std::string print() override;
    std::vector<Constant*> const_array;  // 数组元素列表
};

// 零初始化常量，如 zeroinitializer
class ConstantZero : public Constant {
public:
    ConstantZero(Type* ty) : Constant(ty, "") {}
    virtual std::string print() override;
};
