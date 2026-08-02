#pragma once
// 最小 libc 外部函数声明工具（memset / memcpy / memmove）。

#include "../ir/ir.hpp"

enum class LibFunc {
    Memset,
    Memcpy,
    Memmove,
};

Function *getOrInsertLibFunc(Module *module, LibFunc kind);
