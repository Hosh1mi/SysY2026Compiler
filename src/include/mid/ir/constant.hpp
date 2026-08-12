#pragma once
// IR 常量：编译期已知的值，无名（name == ""）

#include "value.hpp"

#include <cstdint>
#include <vector>

// 常量基类。常量没有所属基本块，可被多个指令或聚合常量共享。
class Constant : public Value {
public:
    // 常量不需要 SSA 名字，name 参数只为保持 Value 构造接口一致。
    Constant(Type* ty, const std::string& name = "") : Value(ty, name) {}
    virtual ~Constant() = default;
};

// 整数常量，如 i32 42 / i64 4294967296
class ConstantInt : public Constant {
public:
    // val 用统一的 int64_t 承载，实际解释宽度由 ty 决定。
    ConstantInt(Type* ty, std::int64_t val) : Constant(ty, ""), value_(val) {}
    // 输出数值部分；i1 会按布尔值打印。
    virtual std::string print() override;
    std::int64_t value_;  // 未截断的宿主存储，解释位宽来自 type_
};

// float 常量，如 0x4057C21FC0000000
class ConstantFloat : public Constant {
public:
    // 保存单精度值；print() 负责生成稳定的 IR 十六进制表示。
    ConstantFloat(Type* ty, float val) : Constant(ty, ""), value_(val) {}
    // 输出保持单精度比特语义的十六进制浮点文本。
    virtual std::string print() override;
    float value_;  // 源级/IR 单精度值
};

// 数组常量，如 [3 x i32] [i32 42, i32 11, i32 74]
class ConstantArray : public Constant {
public:
    // val 必须与 ty 的元素数和递归元素类型一致。
    ConstantArray(ArrayType* ty, const std::vector<Constant*>& val)
        : Constant(ty, ""), const_array(val) {}
    // 全零聚合会缩写为 zeroinitializer，否则递归打印各元素。
    virtual std::string print() override;
    std::vector<Constant*> const_array;  // 数组元素列表
};

// 向量常量，如 <4 x i32> <0, 1, 2, 3>
class ConstantVector : public Constant {
public:
    // elements 按 lane 顺序保存，元素类型必须匹配 ty->contained_。
    ConstantVector(VectorType* ty, const std::vector<Constant*>& elements)
        : Constant(ty, ""), elements_(elements) {}
    // 按 lane 顺序输出定宽向量常量。
    virtual std::string print() override;
    std::vector<Constant*> elements_; // 按 lane 顺序保存
};

// 零初始化常量，如 zeroinitializer
class ConstantZero : public Constant {
public:
    // 表示任意标量或聚合类型的全零值，不展开保存子元素。
    ConstantZero(Type* ty) : Constant(ty, "") {}
    // 输出 zeroinitializer。
    virtual std::string print() override;
};

// 带类型的未定义值，只用于逐 lane 构造聚合 SSA 值的起点。
class UndefValue : public Constant {
public:
    // ty 决定 undef 的完整类型。
    explicit UndefValue(Type* ty) : Constant(ty, "") {}
    // 输出 undef。
    virtual std::string print() override;
};
