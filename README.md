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

## 后端架构

### 总览

后端接收中端已优化的 `Module`，按函数 lowering 为结构化机器 IR，经寄存器分配与栈帧物化后输出汇编。

```
中端 IR Module
      │
      ▼
 SelectionDAG（按基本块）
      │  legalize / combine
      ▼
 指令选择 → SSA Machine IR（含 Machine PHI）
      │
      ├─ Pre-RA 机器优化 / 调度（-O1）
      ▼
 PHI 消除 → 图着色寄存器分配 → 并行拷贝解析
      │
      ▼
 栈帧 lowering（prologue / epilogue / FI 物化）
      │
      ├─ Post-RA 优化 / 块布局 / 调度（-O1）
      ▼
 汇编打印（.text）+ 全局数据段（.data / .bss / .rodata）
```

### 两层中间表示

**SelectionDAG**  
按基本块将中端 IR 降为目标无关节点：算术、内存、调用、分支、PHI、向量 insert/extract 等。随后做类型合法化；`-O1` 下再做有限 combine（如乘加合并、向量归约识别）。

**Machine IR**  
指令选择产物。主要结构：

| 结构 | 作用 |
|------|------|
| MachineOperand | 虚拟/物理寄存器、立即数、符号、栈槽、条件码、regmask 等 |
| MachineInstr | opcode + 有序操作数 + 可选内存操作数；可标记并行拷贝组与伪指令 |
| MachineBasicBlock | 指令序列与 CFG 前驱/后继 |
| MachineRegisterInfo | 虚拟寄存器及其寄存器类、类型、定义点 |
| MachineFrameInfo | 局部对象、spill 槽、固定调用帧、callee-saved 集合 |
| MachineFunction | 上述集合，并带阶段属性位（SSA、含 PHI、已选指令、无 vreg、栈帧已定稿等） |

指令选择后 MIR 保持 **SSA + Machine PHI**；PHI 消除后再进入全局分配。伪指令（帧建立/销毁、帧地址、调用栈调整、spill load/store 等）延迟到栈帧阶段物化。

### 指令选择与目标描述

指令选择为 DAG 模式匹配，手写规则，产出虚拟寄存器与目标 opcode。目标描述集中提供：

- 值类型：`i1` / `i32` / `f32` / 指针 / `v4i32` / `v4f32` / flags（无双精度向量）
- 寄存器类：GPR32、GPR64、FPR32、NEON128、条件码
- 指令描述：助记符、副作用、延迟、调度资源类别
- ABI：整数/指针参数与返回走通用寄存器，浮点/向量走向量寄存器；调用带 clobber/regmask

常量除法在选择阶段做强度削减（magic number），仅使用对全部输入成立的等价序列。NEON 仅覆盖平台支持的单精度/整型四路向量；双精度走标量浮点路径。

DAG combine（`-O1`，指令选择前）另含：`mul+add/sub` → `madd/msub`；四路 `extract` 求和 → 向量归约。

### 寄存器分配与调用约定

PHI 消除将 Machine PHI 转为前驱边 COPY（必要时拆临界边）。随后做活跃区间分析，按寄存器类建立冲突图，使用 **Chaitin-Briggs 乐观图着色**：

- COPY 亲和性影响偏色
- 跨调用存活值倾向 callee-saved
- 着色失败则插入 spill 伪指令并多轮重试
- 参数寄存器是偏好色，而非全程保留色；死亡后可被复用

分配后解析 ABI 并行拷贝，并展开仍依赖 tied COPY 的少量伪形态。`-O0` 仍跑图着色，但跳过多数机器级优化与调度。

### 栈帧

在无虚拟寄存器之后布局栈帧：确定 callee-saved、固定 outgoing 调用区、将 frame index 改为基于 SP/FP 的寻址，并插入 prologue / epilogue。固定 outgoing 区使传参时局部与 spill 地址仍有效，避免调用点抖动 SP。

### 机器级优化

`-O1` 下分 Pre-RA / Post-RA。`--fno-peephole` / `--fno-pre-schedule` / `--fno-schedule` 可分别关闭 peephole 与两段调度。

#### Pre-RA

**常量 CSE（块内）**  
同一基本块内相同 `mov` 立即数合并；遇 call 清空表，避免凭空制造跨调用活跃区间。

```
mov  %v1, #42
...
mov  %v2, #42      →  删除后者，后续 use 改写为 %v1
add  %v3, %v2, %v0
```

**零恒等 → COPY / 自搬移除**  
`add/sub Rd, Rn, #0` 收成 `COPY`；物理寄存器自搬直接删。

```
add  %v1, %v0, #0   →  copy %v1, %v0
mov  x9, x9         →  删除
```

**向量 pair load/store**  
同基址、偏移相差 16、中间无内存屏障/call 的两条 `ldr q`/`str q`，且较低偏移 16 对齐且立即数可编码时，合成 `ldp q`/`stp q`。

```
ldr  q0, [%base, #0]
ldr  q1, [%base, #16]   →  ldp q0, q1, [%base, #0]
```

**常量下沉**  
PHI 消除后：单 use、无副作用的物化指令（`mov` 立即数、零向量、`fmov`、宽度扩展等），可沿**单前驱边**沉到 use 所在块、紧挨 use 之前。

**条件/flags 折叠**  
去掉「比较结果整数化后再测零」的中间层。

```
cmp  a, b
cset %p, lt
...（中间不改 NZCV）...
cmp  %p, #0
csel %r, %t, %f, ne     →  csel %r, %t, %f, lt
                            （删掉 cset 与第二次 cmp）
```

```
cmp  a, b
cset %p, eq
cbnz %p, L              →  b.eq L
```

**死指令删除**  
SSA 上结果无 use、且无 load/store/call/控制流副作用的指令迭代删除。

**常量 LICM**  
有规范 preheader 时，把循环不变的 `mov` 立即数/`movi #0` 提到 preheader；嵌套环的 preheader 不再向外层反复提升，避免拉长活跃区间。

**CFG：位测试分支**  
`and %t, %x, #1` + `cmp %t, #0` + `b.eq/ne`，且 mask/结果均单 use → `tbz`/`tbnz`（测 bit 0）。

```
mov  %one, #1
and  %t, %x, %one
cmp  %t, #0
b.eq L                  →  tbz %x, #0, L
```

**CFG：连续右移批处理**  
结构匹配「按最低位分支；偶数边仅 `asr #1`；latch 仅 `count+=1` 并与奇数哨兵比较」时，用 `rbit`+`clz` 一次跳过多步偶数减半，并按步数累加计数；奇数边若可证下一步必偶，可同样批处理。只认该指令/CFG 形态。

#### Post-RA

**指令展开**  
向量 `ins` 在目标≠源时先插 `COPY`，再把源改成 tied use，满足 destructive 语义。

**拷贝传播（三类）**

1. **结果回写消除**：运算写临时寄存器，紧接着拷回输入且中间不冲突 → 直接写输入并删 COPY。

```
add  w9, w0, #1
mov  w0, w9             →  add w0, w0, #1
```

2. **前向替换（可证杀死）**：`COPY Rd, Rs` 后，局部 use 改写为 `Rs`，直到 `Rd` 被重定义或经 call/出口可证死亡；call 的参数寄存器与 caller-saved 按 ABI 保守处理。

```
mov  s16, s0
fadd s17, s16, s1
fmov s16, s2            →  fadd s17, s0, s1
                            （删掉首条 mov；s16 已被重定义）
```

3. **自搬删除**：`mov xN, xN`（含别名视图）删除。

**寻址：post-index**  
`ldr/str` 偏移为 0，随后同基址 `add Xn, Xn, #imm`（imm∈[-256,255]，基址非 SP/FP）→ 合成 post-index 并删掉 `add`。

```
ldr  w0, [x1]
add  x1, x1, #4         →  ldr w0, [x1], #4
```

**块布局**  
先穿透「仅含无条件跳」的 forwarder；再按环深度与 fallthrough 偏好重排，使热路径少跳。

**调度**  
Pre-RA / Post-RA 共用 list scheduler：按指令延迟与资源，以及 RAW/WAR/WAW、条件码与保守内存依赖构图；未知地址按可能别名处理。Post-RA 调度为终端变换，其后只做校验与打印。

### Peephole 规则表

这里的 peephole 指 `--fno-peephole` 控制的局部/近邻变换（含 Pre-RA peephole 与 Post-RA 拷贝/寻址相关 pass），不含调度与块布局。DAG combine 与指令选择内的常量除法削减不在该开关范围内。

| 阶段 | 规则 | 条件 / 边界 |
|------|------|-------------|
| Pre-RA | 常量 CSE | 同块；`MOVi32/64`、`MOVIv4Zero`；call 清表 |
| Pre-RA | `±#0` → COPY | `ADD/SUB` 立即数为 0 |
| Pre-RA | 物理自搬删除 | `COPY` 且 defs/uses 同物理寄存器（含别名） |
| Pre-RA | `ldp`/`stp` 合成 | 同 root；偏移差 16；低偏移 16 对齐且 `#imm/16∈[-64,63]`；中间无 call/冲突访存；非 volatile |
| Pre-RA | 物化下沉 | 仅 PHI 消除后；单 use；单前驱边；可下沉 opcode 白名单 |
| Pre-RA（条件） | cset+cmp0+csel 折叠 | `%p` 单 use；中间不改 NZCV；csel 条件为 eq/ne |
| Pre-RA（条件） | cset+cbz/cbnz → bcc | `%p` 单 use；中间不改 NZCV |
| Pre-RA（CFG） | and1+cmp0+bcc → tbz/tbnz | mask 与结果均单 use；条件 eq/ne |
| Pre-RA（CFG） | 连续 `asr #1` 批处理 | 偶数边/latch/continuation 指令形态严格匹配；用 `rbit+clz` |
| Post-RA | 运算+回写 COPY 合并 | 窗口 ≤6；中间不碰临时/输入；无 call/terminator |
| Post-RA | COPY 前向替换 | 物理寄存器；同寄存器类；可证 kill；保守处理 call/出口活跃 |
| Post-RA | 自搬删除 | 同 Post-RA 物理 COPY |
| Post-RA | post-index 合成 | 基址偏移先为 0；后续同基址 `add`；非 SP/FP |
| 各阶段 | Machine DCE | SSA；无副作用且 defs 全死 |

### 按优化等级的差异

```
                    -O0              -O1
DAG → ISel           ✓                ✓
Pre-RA 机器优化      ✗                ✓
Pre-RA 调度          ✗             可关
PHI 消除 + 图着色    ✓                ✓
栈帧 lowering        ✓                ✓
Post-RA 优化/布局    ✗                ✓
Post-RA 调度         ✗             可关
```

### 模块级收尾

各函数 `.text` 输出后，按属性将全局对象分到 `.data` / `.bss` / `.rodata`。若中端插入了并行循环入口，由驱动在汇编末尾追加并行 dispatch / runtime 片段（不在上述 per-function 管线内）。

