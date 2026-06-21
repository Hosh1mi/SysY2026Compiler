#pragma once

#include "machine.hpp"

namespace riscv {

bool removeSelfMoves(MFunction &func);
bool forwardAdjacentStoreLoads(MFunction &func);

// 基本块内拷贝传播：跟踪 `mv`/`fmv.s`/`fmv.d` 建立的寄存器副本，把对副本目标
// 的使用改写回源寄存器（随后死代码消除可删去多余的 mv）。遇标签/终结符清空，
// 遇定义（含 call 对 caller-saved 的钳制）失效相关条目，保证只在副本仍有效时改写。
bool propagateCopies(MFunction &func);

}  // namespace riscv
