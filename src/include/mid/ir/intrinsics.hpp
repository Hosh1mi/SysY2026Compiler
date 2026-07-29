#pragma once

#include "ir.hpp"

enum class SignedMinMaxIntrinsic {
    SMin,
    SMax,
};

Function *getOrInsertSignedMinMaxIntrinsic(Module *module,
                                           SignedMinMaxIntrinsic kind,
                                           Type *type);
bool isSignedMinMaxIntrinsicName(const std::string &name,
                                 SignedMinMaxIntrinsic *kind = nullptr);
bool isSignedMinMaxIntrinsic(Function *function,
                             SignedMinMaxIntrinsic *kind = nullptr);
bool isSupportedSignedMinMaxType(Type *type);
bool matchSignedMinMaxSelect(SelectInst *select,
                             SignedMinMaxIntrinsic &kind,
                             Value *&lhs,
                             Value *&rhs);
