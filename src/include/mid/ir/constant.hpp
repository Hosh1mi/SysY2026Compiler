#pragma once
// IR 常量：编译期已知的值，无名（name == ""）

#include "value.hpp"

#include <cstdint>
#include <vector>

// 常量基类
class Constant : public Value {
public:
    Constant(Type* ty, const std::string& name = "") : Value(ty, name) {}
    virtual ~Constant() = default;
};

// 整数常量，如 i32 42 / i64 4294967296
class ConstantInt : public Constant {
public:
    ConstantInt(Type* ty, std::int64_t val) : Constant(ty, ""), value_(val) {}
    virtual std::string print() override;
    std::int64_t value_;
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

// 向量常量，如 <4 x i32> <0, 1, 2, 3>
class ConstantVector : public Constant {
public:
    ConstantVector(VectorType* ty, const std::vector<Constant*>& elements)
        : Constant(ty, ""), elements_(elements) {}
    virtual std::string print() override;
    std::vector<Constant*> elements_;
};

// 零初始化常量，如 zeroinitializer
class ConstantZero : public Constant {
public:
    ConstantZero(Type* ty) : Constant(ty, "") {}
    virtual std::string print() override;
};

// A typed undefined value used while constructing aggregate SSA values.
class UndefValue : public Constant {
public:
    explicit UndefValue(Type* ty) : Constant(ty, "") {}
    virtual std::string print() override;
};
