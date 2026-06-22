#pragma once
// RISC-V 机器层 pass manager。把原先在 codegen 中手写的固定迭代链收拢到这里，
// 并在 lowering 之后、各优化前后维护 CFG 与结构验证。调度类 pass 以后接入同一
// 管线（当前留空，由 BOOM 资源模型实现时填充）。

#include "machine.hpp"

namespace riscv {

struct MachinePassOptions {
    bool runPeephole = true;   // 低风险窥孔/DCE 链
    bool verify = true;        // 每阶段后跑结构验证
    bool dumpPre = false;      // 优化前 dump
    bool dumpPost = false;     // 优化后 dump
};

// 在已 lowering 的函数上运行机器 pass 管线（就地修改 func）。
void runMachinePasses(MFunction &func, const MachinePassOptions &opt);

}  // namespace riscv
