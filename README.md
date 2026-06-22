# Compiler2026-NO_COMPILE_NO_LIFE

## 进度

### TODO

<table>
  <tr style="background-color:#f6f8fa;">
    <th style="width:25%">Module</th>
    <th>TODO</th>
  </tr>
  
  <tr>
    <td style="vertical-align:top;"><b>Mid-opt</b></td>
    <td>实现更彻底的死代码消除（DCE）</td>
  </tr>
  
  <tr>
    <td><b>Pass 管理</b></td>
    <td>建立合理的 pass 顺序管理机制（可能需要使某些pass存在返回值）</td>
  </tr>
  
  <tr>
    <td style="vertical-align:top;"><b>Backend</b></td>
    <td>完善 NEON 指令支持</td>
  </tr>

  <tr>
    <td><b>特性支持</b></td>
    <td>完整支持 Vec 向量类型</td>
  </tr>
</table>

## 项目运行

```bash
# 项目代码会放在 /workspace 下
docker build --platform linux/arm64 -t sysy-dev .; docker run -it --platform linux/arm64 -v $(pwd):/workspace sysy-dev

# 在根目录下构建：
mkdir build && cd build && cmake .. && make -j8

# 生成IR:
./compiler -c -o out.ll <testpath>

# 生成汇编:
./compiler -S -o out.s <.sy path>

# 编译运行:
gcc out.s ../lib/libsysy.a -o out
./out < <testinput>
```

## 调试选项

| Flag | 作用 |
|------|------|
| `-O0` / `-O1` / `-O2` | 优化等级（`-O2`仅用于调试） |
| `--dump-ir` | 每个 pass 前后 dump IR |
| `--verify-ir` | 每个 pass 后校验 IR 完整性（TODO:目前无作用） |
| `--fno-peephole` | 在 `-O1` 下禁用 peephole 汇编后优化 |
| `--fno-pre-schedule` | 在 `-O1` 下禁用 preRA 虚拟机器指令调度 |
| `--fno-schedule` | 在 `-O1` 下禁用 MachineInstr 调度 |
| `--dump-pre-machine-instr` | 输出 preRA 虚拟 MachineInstr（vreg defs/uses、latency），dump 到 stderr |
| `--dump-machine-instr` | 输出每个函数的 MachineInstr 详细信息（opcode类型、defs/uses、latency、标志位），dump 到 stderr |

注:`--dump-ir`为stderr实现，输出极长，使用`2>`重定向到文件里查看

`--dump-machine-instr` 同样输出到 stderr，可单独使用或配合 `-S`：
```bash
./compiler test.sy --dump-machine-instr 2> dump.txt   # 只 dump MachineInstr
./compiler -S test.sy --dump-machine-instr 2> dump.txt # 同时输出汇编
```

## ARM64 后端流水线

ARM64 后端接收中端已经完成优化的 `Module`，目标为 ARMv8-A/AArch64，调度模型面向 Cortex-A53。后端不会按函数名、测试名或输入模式选择优化，也不进行投机执行。当前流水线按以下顺序运行：

1. **重建 IR CFG**：以每个基本块的 terminator 为事实来源，重新生成 `pre_bbs_` 和 `succ_bbs_`。寄存器分配、支配关系和后端控制流变换不使用中端可能遗留的陈旧边。
2. **ARM64 IR 准备**：
   - `-O1` 下，把只在条件分支单侧使用的全局 load 下沉到对应边；变换要求所有 use 都被同一后继支配，并在拆边时同步 PHI 入边。
   - 把仅被本块条件分支使用的整数 compare 移到 terminator 前，使指令选择可以直接生成 compare + conditional branch。该移动要求 compare 单 use，且不会跨越其操作数定义。
3. **pre-RA 调度（仅 `-O1`）**：将可调度的 IR 临时降低为带虚拟寄存器、defs/uses、latency 和内存属性的 MachineInstr，在基本块内建立依赖图。只调度含长延迟指令且没有 store 的安全片段；若调度后寄存器压力超过限制，则保留原顺序。
4. **全局对象分类**：常量进入 `.rodata`，有非零初始化的可写对象进入 `.data`，零初始化对象进入 `.bss`；同时输出外部函数声明。
5. **逐函数 ARM64 lowering**：各函数可以并行处理，但最终按 IR 中的函数顺序输出。
   - 查找统一 epilogue，准备 PHI 边 copy。
   - `-O1` 下计算 live interval、调用穿越信息和冲突图，分别为整数、标量浮点及合法 NEON 值着色；无法分配的值保留栈槽。调用穿越值只使用 ABI 允许的 callee-saved 寄存器。
   - 选择有利于 fallthrough 的基本块布局。
   - 生成 prologue、参数搬运、标量/浮点/NEON 指令、地址计算、分支、调用、PHI copy 和 epilogue。常数强度削减只使用对全部输入成立的等价 ARM64 序列，并检查立即数编码范围。
6. **Machine IR 优化（仅 `-O1`）**：`Arm64MachineOptimizationPipeline` 调度命名 pass，并重复到所有 pass 均无改动。每次可能产生死定义的迭代后运行 Machine DCE。
   - `arm64-copy-propagation`：copy forwarding、copy destination retarget 和受限 copy propagation；依赖物理寄存器别名和活跃性。
   - `arm64-instruction-combine`：立即数、shifted add/sub 和 multiply-add 等 ARM64 合并；检查寄存器宽度及编码合法性。
   - `arm64-code-motion`：在完整 CFG 活跃性证明下，把纯 add/sub 延迟到条件分支的 fallthrough 路径。
   - `arm64-memory-optimization`：零值 store、store-load forwarding、死 store、pair load/store 和 post-index；无法证明地址、宽度和屏障安全时不变换。
   - `arm64-branch-optimization`：位测试分支合并、返回块折叠、fallthrough branch 删除、跳转穿透和无用 forwarder 清理；删除 NZCV 定义前必须证明 flags 在后继不活跃。
   - `arm64-canonicalization`：把等价的单寄存器 NEON load/store 规范化为统一形式。
   - `arm64-local-cse`：消除局部重复的 ADRP 和 frame-address 计算。
   - `arm64-peephole`：只保留无需数据流或 CFG 分析的自搬移删除。
   - `arm64-machine-dce`：依据 Machine liveness 删除结果不活跃且无内存、调用或控制流副作用的指令。
7. **post-RA 调度（仅 `-O1`）**：在物理 MachineInstr 上按 RAW/WAR/WAW、NZCV、barrier 和保守内存依赖构图，以 Cortex-A53 延迟隐藏为目标重排基本块内指令。未知地址一律视为可能别名。调度是最后一个变换阶段；其后不再运行会改变依赖关系或指令延迟的完整 peephole pipeline。
8. **汇编输出**：把结构化 MachineInstr 打印为 `.text`，再按 `.data`、`.bss`、`.rodata` 输出全局数据。

`-O0` 不运行寄存器分配、pre/post-RA 调度或 Machine IR 优化，使用栈槽和保守 lowering。`--fno-pre-schedule`、`--fno-peephole`、`--fno-schedule` 可分别关闭上述阶段；两个 dump 选项可观察 pre-RA 和最终 Machine IR。

NEON lowering 只对目标平台支持的向量类型启用。Cortex-A53 的双精度浮点使用标量 FP 指令，不生成双精度 NEON 向量运算。

批量测试脚本在 `test/` 下，命名规则：
- `arm_` 前缀：ARM 原生环境（容器内 `gcc` 直接编译运行）
- `amd_` 前缀：交叉编译环境（`aarch64-linux-gnu-gcc` + `qemu-aarch64`）
