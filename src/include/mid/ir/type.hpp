#pragma once
// IR 类型系统：定义编译器中间表示中的类型层次

#include <string>
#include <vector>

// 基础类型，所有 IR 类型的基类
class Type {
public:
    enum TypeID {
        VoidTyID,      // void
        LabelTyID,     // 基本块标签
        IntegerTyID,   // 整数（i1 / i32）
        FloatTyID,     // 浮点（float32）
        FunctionTyID,  // 函数类型
        ArrayTyID,     // 数组类型
        PointerTyID,   // 指针类型
    };
    explicit Type(TypeID tid) : tid_(tid) {}
    ~Type() = default;
    virtual std::string print();
    TypeID tid_;
};

// i32 / i1
class IntegerType : public Type {
public:
    explicit IntegerType(unsigned num_bits) : Type(Type::IntegerTyID), num_bits_(num_bits) {}
    unsigned num_bits_;
};

// [2 x [3 x i32]]: num_elements_ = 2, contained_ = [3 x i32]
class ArrayType : public Type {
public:
    ArrayType(Type* contained, unsigned num_elements) : Type(Type::ArrayTyID), num_elements_(num_elements), contained_(contained) {}
    Type* contained_;         // 元素类型
    unsigned num_elements_;   // 元素个数
};

// [2 x [3 x i32]]*
class PointerType : public Type {
public:
    PointerType(Type* contained) : Type(Type::PointerTyID), contained_(contained) {}
    Type* contained_;  // 所指类型
};

// declare i32 @putarray(i32, i32*)
class FunctionType : public Type {
public:
    FunctionType(Type* result, std::vector<Type*> params) : Type(Type::FunctionTyID) {
        result_ = result;
        for (Type* p : params) {
            args_.push_back(p);
        }
    }
    Type* result_;             // 返回类型
    std::vector<Type*> args_;  // 形参类型列表
};
