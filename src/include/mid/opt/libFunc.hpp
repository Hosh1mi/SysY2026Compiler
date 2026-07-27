#pragma once
// 最小 libc 外部函数声明工具（memset / memcpy）。

#include "../ir/ir.hpp"

enum class LibFunc {
    Memset,
    Memcpy,
};

Function *getOrInsertLibFunc(Module *module, LibFunc kind);
