#pragma once
// 最小 runtime memory helper 声明工具。

#include "../ir/ir.hpp"

enum class LibFunc {
    Memset,
    Memcpy,
    Memmove,
};

Function *getOrInsertLibFunc(Module *module, LibFunc kind);
