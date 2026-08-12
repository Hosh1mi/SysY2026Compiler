#pragma once

#include "ir.hpp"

// signed min/max 两种语义身份，由 Function::IntrinsicID 持久化到函数对象。
enum class SignedMinMaxIntrinsic {
    SMin,
    SMax,
};

// 返回指定标量/向量类型的 signed min/max 声明；不存在时插入 module。
Function *getOrInsertSignedMinMaxIntrinsic(Module *module,
                                           SignedMinMaxIntrinsic kind,
                                           Type *type);
// 判断函数是否为 signed min/max；kind 非空时同时返回具体种类。
bool isSignedMinMaxIntrinsic(Function *function,
                             SignedMinMaxIntrinsic *kind = nullptr);
// 判断类型能否直接表达为当前 signed min/max intrinsic。
bool isSupportedSignedMinMaxType(Type *type);
// 识别 cmp+select 组成的 signed min/max，并返回参与比较的两个值。
bool matchSignedMinMaxSelect(SelectInst *select,
                             SignedMinMaxIntrinsic &kind,
                             Value *&lhs,
                             Value *&rhs);

// 返回或插入 i32 mulmod 声明，其乘积使用 i64 中间值避免窄位溢出。
Function *getOrInsertMulModIntrinsic(Module *module);
// 通过结构化 IntrinsicID 判断函数是否为 mulmod。
bool isMulModIntrinsic(Function *function);
