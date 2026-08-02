#pragma once

#include "ir.hpp"

enum class SignedMinMaxIntrinsic {
    SMin,
    SMax,
};

Function *getOrInsertSignedMinMaxIntrinsic(Module *module,
                                           SignedMinMaxIntrinsic kind,
                                           Type *type);
bool isSignedMinMaxIntrinsic(Function *function,
                             SignedMinMaxIntrinsic *kind = nullptr);
bool isSupportedSignedMinMaxType(Type *type);
bool matchSignedMinMaxSelect(SelectInst *select,
                             SignedMinMaxIntrinsic &kind,
                             Value *&lhs,
                             Value *&rhs);

// i32 (a * b) % m with a 64-bit intermediate product.
Function *getOrInsertMulModIntrinsic(Module *module);
bool isMulModIntrinsic(Function *function);
