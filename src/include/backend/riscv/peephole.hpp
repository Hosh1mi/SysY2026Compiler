#pragma once

#include "machine.hpp"

namespace riscv {

bool removeSelfMoves(MFunction &func);
bool forwardAdjacentStoreLoads(MFunction &func);

// 基本块内拷贝传播：跟踪 `mv`/`fmv.s`/`fmv.d` 建立的寄存器副本，把对副本目标
// 的使用改写回源寄存器（随后死代码消除可删去多余的 mv）。遇标签/终结符清空，
// 遇定义（含 call 对 caller-saved 的钳制）失效相关条目，保证只在副本仍有效时改写。
bool propagateCopies(MFunction &func);

// 生产者结果寄存器重定向：`<producer> S, ...` 紧随其后被 `mv D, S` 取走且 S 之后
// 不再活跃时，直接让生产者写入 D 并删除该 mv。要求二者间 S/D 均未被使用或重定义
// （call 对 caller-saved 的钳制计入重定义），保证不缩短任何活跃区间。
bool redirectProducers(MFunction &func);

// 分支转发：把跳向“仅含一条无条件 j”的蹦床块的分支直接改向其最终目标（解析链、
// 防环）。蹦床块随后无人引用，由死块消除清理。
bool forwardBranches(MFunction &func);

// 死块消除：从入口块按 CFG（分支目标 + 条件分支贯穿）做可达性分析，删除不可达块。
bool removeDeadBlocks(MFunction &func);

// 贯穿跳转消除：`j L` 的下一条恰为标签 L 时删除该跳转（直接贯穿）。
bool removeFallthroughJumps(MFunction &func);

}  // namespace riscv
