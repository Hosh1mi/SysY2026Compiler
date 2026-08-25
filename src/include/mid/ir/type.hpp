#pragma once
// IR 类型系统：定义编译器中间表示中的类型层次

#include <string>
#include <utility>
#include <vector>

// 基础类型，所有 IR 类型的基类。Type 对象只描述形状，不拥有 Value；Module 对常用
// 派生类型做驻留，因此同一 Module 内等价类型通常也是同一指针。
class Type {
public:
    enum TypeID {
        VoidTyID,      // void
        LabelTyID,     // 基本块标签
        IntegerTyID,   // 整数（i1 / i32）
        FloatTyID,     // 浮点（float32）
        FunctionTyID,  // 函数类型
        ArrayTyID,     // 数组类型
        VectorTyID,    // 向量（<4 x i32> / <4 x float>）
        PointerTyID,   // 指针类型
    };
    // 构造一个给定类别的类型；派生类负责补充位宽、元素类型等参数。
    explicit Type(TypeID tid) : tid_(tid) {}
    ~Type() = default;
    // 返回文本 IR 中的完整类型拼写；聚合类型由本函数递归读取派生类参数。
    virtual std::string print();
    // 运行时类型标签；需要结构参数时仍应转为相应派生类型。
    TypeID tid_;
};

// 任意固定位宽整数类型。当前公共实例主要是 i1、i8、i32 和内部 i64。
class IntegerType : public Type {
public:
    // num_bits 是 IR 位宽，不表示源语言的 signedness。
    explicit IntegerType(unsigned num_bits) : Type(Type::IntegerTyID), num_bits_(num_bits) {}
    unsigned num_bits_;
};

// [2 x [3 x i32]]: num_elements_ = 2, contained_ = [3 x i32]
class ArrayType : public Type {
public:
    // 构造 num_elements 个 contained 元素组成的定长聚合类型。
    ArrayType(Type* contained, unsigned num_elements) : Type(Type::ArrayTyID), num_elements_(num_elements), contained_(contained) {}
    Type* contained_;         // 元素类型
    unsigned num_elements_;   // 元素个数
};

// <4 x i32> : num_elements_ = 4, contained_ = i32
class VectorType : public Type {
public:
    VectorType(Type* contained, unsigned num_elements) : Type(Type::VectorTyID), num_elements_(num_elements), contained_(contained) {}
    Type* contained_;
    unsigned num_elements_;
};

// [2 x [3 x i32]]*
class PointerType : public Type {
public:
    // contained 是解引用或 load 后得到的类型。
    PointerType(Type* contained) : Type(Type::PointerTyID), contained_(contained) {}
    Type* contained_;  // 所指类型
};

// declare i32 @putarray(i32, i32*)
class FunctionType : public Type {
public:
    // params 保持 ABI 参数顺序；variadic 只允许固定参数之后继续传递实参。
    FunctionType(Type* result, std::vector<Type*> params, bool variadic = false)
        : Type(Type::FunctionTyID), result_(result), args_(std::move(params)),
          is_variadic_(variadic) {}
    Type* result_;             // 返回类型
    std::vector<Type*> args_;  // 形参类型列表
    bool is_variadic_ = false;
};

// 返回该 IR 类型采用紧凑布局时的存储字节数。无存储表示或大小溢出时返回 -1。
long long typeStorageBytes(Type *type);
