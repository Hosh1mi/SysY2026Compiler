# ARM后端完整说明

## 0 目标描述

### 0.1 ValueType — 值类型

| 枚举 | 含义 |
|------|------|
| `Invalid` | 无效类型 |
| `I1` | 1 位整数（布尔） |
| `I32` | 32 位有符号整数 |
| `I64` | 64 位有符号整数 |
| `F32` | 32 位 IEEE 754 浮点 |
| `Ptr` | 指针（逻辑上 64 位地址） |
| `V4I32` | 128 位向量，4 个 32 位整数 lane |
| `V4F32` | 128 位向量，4 个 32 位浮点 lane |
| `Flags` | 条件标志（NZCV） |

### 0.2 RegClass — 寄存器类

| 枚举 | 含义 | 典型物理视图 |
|------|------|--------------|
| `Invalid` | 无效 | — |
| `GPR32` | 32 位通用寄存器 | W 视图（w0–w30, wsp, wzr） |
| `GPR64` | 64 位通用寄存器 | X 视图（x0–x30, sp, xzr） |
| `FPR32` | 32 位标量浮点 | S 视图（s0–s31） |
| `NEON128` | 128 位 NEON 向量 | V 视图（v0–v31） |
| `CCR` | 条件码寄存器 | nzcv |

**说明**：NEON128 与 FPR32 共享同一组 V 物理寄存器，通过 `RegClass` 区分打印为 `vN` 或 `sN`。

### 0.3 PhysReg — 物理寄存器

| 范围 | 寄存器 | AArch64 角色 |
|------|--------|--------------|
| `NoReg` | — | 无寄存器 |
| `X0`–`X7` | x0–x7 | 参数/返回值 |
| `X8` | x8 | 间接结果 / 结构体返回辅助 |
| `X9`–`X15` | x9–x15 | 调用者保存临时 |
| `X16`–`X17` | x16–x17 | IP0/IP1，过程链接 / PLT |
| `X18` | x18 | 平台保留（**不可分配**） |
| `X19`–`X28` | x19–x28 | 被调用者保存 |
| `X29` | x29 | 帧指针 FP（**保留**） |
| `X30` | x30 | 链接寄存器 LR（**保留**） |
| `SP` | sp | 栈指针 |
| `XZR` | xzr/wzr | 零寄存器 |
| `V0`–`V31` | v0–v31 | NEON / 浮点（低 32 位为 sN） |
| `NZCV` | nzcv | 条件标志 |

### 0.4 CondCode — 条件码

与 AArch64 条件后缀对应：

| 枚举 | 典型含义 |
|------|----------|
| `EQ` | 相等 |
| `NE` | 不等 |
| `HS` | 无符号 ≥（C set） |
| `LO` | 无符号 < |
| `MI` | 负数 |
| `PL` | 非负 |
| `VS` | 溢出 |
| `VC` | 无溢出 |
| `HI` | 无符号 > |
| `LS` | 无符号 ≤ |
| `GE` | 有符号 ≥ |
| `LT` | 有符号 < |
| `GT` | 有符号 > |
| `LE` | 有符号 ≤ |
| `AL` | 总是（无条件） |

### 0.5 SchedResource — 调度资源

| 枚举 | 含义 |
|------|------|
| `None` | 无 / 不参与调度 |
| `ALU` | 整数 ALU |
| `MAC` | 乘加单元（MUL/MADD/SMULL 等） |
| `Divide` | 除法单元 |
| `LoadStore` | 访存单元 |
| `Branch` | 分支 / 调用 |
| `FPALU` | 浮点 / NEON ALU |
| `FPMulDiv` | 浮点乘除 / 部分 NEON 乘除 |

### 0.6 InstrDesc — 指令描述结构

```cpp
struct InstrDesc {
    Opcode opcode = Opcode::Invalid;
    std::string_view mnemonic;
    unsigned explicitDefs = 0;
    unsigned explicitOperands = 0;
    bool terminator = false;
    bool branch = false;
    bool call = false;
    bool returnInstruction = false;
    bool mayLoad = false;
    bool mayStore = false;
    bool hasSideEffects = false;
    bool pseudo = false;
    bool setsFlags = false;
    bool usesFlags = false;
    unsigned latency = 1;
    SchedResource resource = SchedResource::ALU;
};
```

布尔标志语义见下表各 Opcode 章节。


### 0.7 RegisterInfo

#### classForType

| ValueType | RegClass |
|-----------|----------|
| `I1`, `I32` | `GPR32` |
| `I64`, `Ptr` | `GPR64` |
| `F32` | `FPR32` |
| `V4I32`, `V4F32` | `NEON128` |
| `Flags` | `CCR` |
| 其他 | `Invalid` |

#### aliases

仅当 `lhs != NoReg` 且 `lhs == rhs` 时为真。**不**建模 W/X 或 S/V 的同编号别名。

#### isReserved — 不可用于寄存器分配

`NoReg`, `SP`, `XZR`, `X18`, `X29`, `X30`, `NZCV`

#### isCallerSaved

- `X0`–`X17`（**不含** `X18`，虽在范围内但被 reserved）
- `V0`–`V7`
- `V16`–`V31`

#### isCalleeSaved

- `X19`–`X28`
- `V8`–`V15`

#### name(reg, view) — 命名视图

| PhysReg 类别 | view | 输出示例 |
|--------------|------|----------|
| X0–X30 | `GPR64` | x0–x30 |
| X0–X30 | `GPR32` | w0–w30 |
| SP | `GPR64` | sp |
| SP | `GPR32` | wsp |
| XZR | `GPR64` | xzr |
| XZR | `GPR32` | wzr |
| V0–V31 | `NEON128` | v0–v31 |
| V0–V31 | `FPR32` | s0–s31 |
| NZCV | 任意 | nzcv |
| 其他 | — | `<noreg>` |

#### allocationOrder — 分配优先级（先尝试列表前端）

**GPR32 / GPR64（同一列表，31 个 X 寄存器）：**

```
X9, X10, X11, X12, X13, X14, X15,
X16, X17,
X19, X20, X21, X22, X23, X24, X25, X26, X27, X28,
X8, X7, X6, X5, X4, X3, X2, X1, X0
```

（不含 SP/XZR/X18/X29/X30）

**FPR32 / NEON128（32 个 V 寄存器）：**

```
V16, V17, V18, V19, V20, V21, V22, V23,
V24, V25, V26, V27, V28, V29, V30, V31,
V8, V9, V10, V11, V12, V13, V14, V15,
V7, V6, V5, V4, V3, V2, V1, V0
```

**CCR / Invalid**：空列表。

#### calleeSaved 列表

**GPR32 / GPR64：**

```
X19, X20, X21, X22, X23, X24, X25, X26, X27, X28
```

**FPR32 / NEON128：**

```
V8, V9, V10, V11, V12, V13, V14, V15
```

### 0.8 全 Opcode 描述表

命名约定（操作数形态，由发射端约定，非 InstrDesc 字段）：

- **W/X**：32/64 位 GPR
- **S/D/Q**：标量 float / 向量
- **ui**：基址 + 无符号立即偏移
- **lo**：低 12 位立即（常与 ADRP 配对）
- **ro**：寄存器偏移 + 扩展/移位
- **post**：后变址
- **pre**：前变址（如 STPXpre）
- **rr/ri/rs**：寄存器-寄存器 / 立即 / 移位寄存器

表中：**Defs** = explicitDefs，**Ops** = explicitOperands，**Term/Branch/Call/Ret/Load/Store/Side/Pseudo/SetF/UseF** 为对应 InstrDesc 布尔字段，**Lat** = latency，**Res** = SchedResource。

#### 伪指令与框架

| Opcode | Mnemonic | Defs | Ops | Term | Branch | Call | Ret | Load | Store | Side | Pseudo | SetF | UseF | Lat | Res |
|--------|----------|------|-----|------|--------|------|-----|------|-------|------|--------|------|------|-----|-----|
| `Invalid` | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — |
| `PHI` | PHI | 1 | 0 | F | F | F | F | F | F | F | **T** | F | F | 0 | None |
| `COPY` | COPY | 1 | 2 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `IMPLICIT_DEF` | IMPLICIT_DEF | 1 | 1 | F | F | F | F | F | F | F | **T** | F | F | 0 | None |
| `ADJCALLSTACKDOWN` | ADJCALLSTACKDOWN | 0 | 1 | F | F | F | F | F | F | **T** | **T** | F | F | 1 | ALU |
| `ADJCALLSTACKUP` | ADJCALLSTACKUP | 0 | 1 | F | F | F | F | F | F | **T** | **T** | F | F | 1 | ALU |
| `FRAME_SETUP` | FRAME_SETUP | 0 | 0 | F | F | F | F | F | F | **T** | **T** | F | F | 1 | ALU |
| `FRAME_DESTROY` | FRAME_DESTROY | 0 | 0 | F | F | F | F | F | F | **T** | **T** | F | F | 1 | ALU |
| `SPILL_LOAD` | SPILL_LOAD | 1 | 2 | F | F | F | F | **T** | F | F | **T** | F | F | 4 | LoadStore |
| `SPILL_STORE` | SPILL_STORE | 0 | 2 | F | F | F | F | F | **T** | F | **T** | F | F | 1 | LoadStore |

#### 控制流

| Opcode | Mnemonic | Defs | Ops | Term | Branch | Call | Ret | Load | Store | Side | Pseudo | SetF | UseF | Lat | Res |
|--------|----------|------|-----|------|--------|------|-----|------|-------|------|--------|------|------|-----|-----|
| `CALL` | bl | 0 | 0 | F | F | **T** | F | F | F | **T** | F | F | F | 3 | Branch |
| `TAILCALL` | b | 0 | 0 | **T** | **T** | **T** | **T** | F | F | **T** | F | F | F | 1 | Branch |
| `RET` | ret | 0 | 0 | **T** | **T** | F | **T** | F | F | **T** | F | F | F | 1 | Branch |
| `B` | b | 0 | 1 | **T** | **T** | F | F | F | F | F | F | F | F | 1 | Branch |
| `Bcc` | b.cond | 0 | 2 | **T** | **T** | F | F | F | F | F | F | F | **T** | 1 | Branch |
| `CBZ` | cbz | 0 | 2 | **T** | **T** | F | F | F | F | F | F | F | F | 1 | Branch |
| `CBNZ` | cbnz | 0 | 2 | **T** | **T** | F | F | F | F | F | F | F | F | 1 | Branch |
| `TBZ` | tbz | 0 | 3 | **T** | **T** | F | F | F | F | F | F | F | F | 1 | Branch |
| `TBNZ` | tbnz | 0 | 3 | **T** | **T** | F | F | F | F | F | F | F | F | 1 | Branch |

#### 条件选择与标志

| Opcode | Mnemonic | Defs | Ops | Term | Branch | Call | Ret | Load | Store | Side | Pseudo | SetF | UseF | Lat | Res |
|--------|----------|------|-----|------|--------|------|-----|------|-------|------|--------|------|------|-----|-----|
| `CSELW` | csel | 1 | 4 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `CSELX` | csel | 1 | 4 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `FCSELS` | fcsel | 1 | 4 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `CSETW` | cset | 1 | 2 | F | F | F | F | F | F | F | F | F | **T** | 1 | ALU |
| `CNEGW` | cneg | 1 | 3 | F | F | F | F | F | F | F | F | F | **T** | 1 | ALU |

#### 常量与地址

| Opcode | Mnemonic | Defs | Ops | Term | Branch | Call | Ret | Load | Store | Side | Pseudo | SetF | UseF | Lat | Res |
|--------|----------|------|-----|------|--------|------|-----|------|-------|------|--------|------|------|-----|-----|
| `MOVi32` | MOVi32 | 1 | 2 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `MOVi64` | MOVi64 | 1 | 2 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `MOVZ` | movz | 1 | 3 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `MOVK` | movk | 1 | 4 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `MOVIv4Zero` | movi | 1 | 1 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `MOVIv4s` | movi | 1 | 3 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `MOVIv4sMsl` | movi | 1 | 3 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `MVNIv4s` | mvni | 1 | 3 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `MOVIv16b` | movi | 1 | 2 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `FMOVv4s` | fmov | 1 | 2 | F | F | F | F | F | F | F | F | F | F | 2 | FPALU |
| `ADRP` | adrp | 1 | 2 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `ADDlow` | add | 1 | 3 | F | F | F | F | F | F | F | F | F | F | 1 | ALU |
| `LEA_FRAME` | LEA_FRAME | 1 | 2 | F | F | F | F | F | F | F | **T** | F | F | 1 | ALU |

**MOVZ/MOVK 操作数语义**（target.hpp 注释）：PostRA 将 `MOVi32/MOVi64` 展开为：
- `MOVZ`：dst(def), imm16, shift
- `MOVK`：dst(def), dst(use), imm16, shift

#### 32 位整数 ALU

| Opcode | Mnemonic | Defs | Ops | Lat | Res | SetF | UseF |
|--------|----------|------|-----|-----|-----|------|------|
| `ADDWrr` | add | 1 | 3 | 1 | ALU | | |
| `ADDWri` | add | 1 | 3 | 1 | ALU | | |
| `ADDWrs` | add | 1 | 4 | 1 | ALU | | |
| `ADDWrsX` | add | 1 | 4 | 1 | ALU | | |
| `ADDWlsl` | add | 1 | 4 | 1 | ALU | | |
| `SUBWrr` | sub | 1 | 3 | 1 | ALU | | |
| `SUBWri` | sub | 1 | 3 | 1 | ALU | | |
| `NEGW` | neg | 1 | 2 | 1 | ALU | | |
| `MULWrr` | mul | 1 | 3 | 3 | MAC | | |
| `MADDWrrr` | madd | 1 | 4 | 3 | MAC | | |
| `MSUBWrrr` | msub | 1 | 4 | 3 | MAC | | |
| `SDIVWrr` | sdiv | 1 | 3 | 12 | Divide | | |
| `UDIVWrr` | udiv | 1 | 3 | 12 | Divide | | |
| `ANDWrr` | and | 1 | 3 | 1 | ALU | | |
| `ANDWri` | and | 1 | 3 | 1 | ALU | | |
| `ORRWrr` | orr | 1 | 3 | 1 | ALU | | |
| `EORWrr` | eor | 1 | 3 | 1 | ALU | | |
| `LSLWrr` | lsl | 1 | 3 | 1 | ALU | | |
| `LSLWri` | lsl | 1 | 3 | 1 | ALU | | |
| `LSRWrr` | lsr | 1 | 3 | 1 | ALU | | |
| `LSRWri` | lsr | 1 | 3 | 1 | ALU | | |
| `ASRWrr` | asr | 1 | 3 | 1 | ALU | | |
| `ASRWri` | asr | 1 | 3 | 1 | ALU | | |
| `CLZW` | clz | 1 | 2 | 1 | ALU | | |
| `RBITW` | rbit | 1 | 2 | 1 | ALU | | |

（上表 Term/Branch/Call/Ret/Load/Store/Side/Pseudo 均为 F）

#### 64 位整数 ALU

| Opcode | Mnemonic | Defs | Ops | Lat | Res | SetF | UseF | Side |
|--------|----------|------|-----|-----|-----|------|------|------|
| `MULXrr` | mul | 1 | 3 | 3 | MAC | | | |
| `MSUBXrrr` | msub | 1 | 4 | 3 | MAC | | | |
| `SDIVXrr` | sdiv | 1 | 3 | 12 | Divide | | | |
| `UDIVXrr` | udiv | 1 | 3 | 12 | Divide | | | |
| `SMULLXrr` | smull | 1 | 3 | 3 | MAC | | | |
| `SMADDLXrrr` | smaddl | 1 | 4 | 3 | MAC | | | |
| `UMULHXrr` | umulh | 1 | 3 | 3 | MAC | | | |
| `NEGX` | neg | 1 | 2 | 1 | ALU | | | |
| `ANDXrr` | and | 1 | 3 | 1 | ALU | | | |
| `ORRXrr` | orr | 1 | 3 | 1 | ALU | | | |
| `EORXrr` | eor | 1 | 3 | 1 | ALU | | | |
| `LSLXrr` | lsl | 1 | 3 | 1 | ALU | | | |
| `LSRXrr` | lsr | 1 | 3 | 1 | ALU | | | |
| `ASRXrr` | asr | 1 | 3 | 1 | ALU | | | |
| `LSRXri` | lsr | 1 | 3 | 1 | ALU | | | |
| `ADDXrr` | add | 1 | 3 | 1 | ALU | | | |
| `ADDXri` | add | 1 | 3 | 1 | ALU | | | |
| `ADDXrs` | add | 1 | 5 | 1 | ALU | | | |
| `SUBXrr` | sub | 1 | 3 | 1 | ALU | | | |
| `SUBXri` | sub | 1 | 3 | 1 | ALU | | | |
| `LSLXri` | lsl | 1 | 3 | 1 | ALU | | | |
| `ASRXri` | asr | 1 | 3 | 1 | ALU | | | |
| `COPYXtoW` | mov | 1 | 2 | 1 | ALU | | | |
| `MOVXrr` | mov | 1 | 2 | 1 | ALU | | | |
| `SUBSPri` | sub | 1 | 3 | 1 | ALU | | | **T** |
| `ADDSPri` | add | 1 | 3 | 1 | ALU | | | **T** |
| `SXTW` | sxtw | 1 | 2 | 1 | ALU | | | |
| `UXTW` | uxtw | 1 | 2 | 1 | ALU | | | |

#### 比较与测试（仅 SetF）

| Opcode | Mnemonic | Defs | Ops | SetF | Lat | Res |
|--------|----------|------|-----|------|-----|-----|
| `CMPWrr` | cmp | 0 | 2 | **T** | 1 | ALU |
| `CMPWri` | cmp | 0 | 2 | **T** | 1 | ALU |
| `CMPXrr` | cmp | 0 | 2 | **T** | 1 | ALU |
| `CMPXri` | cmp | 0 | 2 | **T** | 1 | ALU |
| `TSTWrr` | tst | 0 | 2 | **T** | 1 | ALU |
| `TSTWri` | tst | 0 | 2 | **T** | 1 | ALU |

#### 标量浮点

| Opcode | Mnemonic | Defs | Ops | Lat | Res | SetF |
|--------|----------|------|-----|-----|-----|------|
| `FADDS` | fadd | 1 | 3 | 4 | FPALU | |
| `FSUBS` | fsub | 1 | 3 | 4 | FPALU | |
| `FMULS` | fmul | 1 | 3 | 4 | FPMulDiv | |
| `FDIVS` | fdiv | 1 | 3 | 18 | FPMulDiv | |
| `FNEGS` | fneg | 1 | 2 | 2 | FPALU | |
| `FCMPSrr` | fcmp | 0 | 2 | 3 | FPALU | **T** |
| `FCMPZS` | fcmp | 0 | 1 | 3 | FPALU | **T** |
| `SCVTFWS` | scvtf | 1 | 2 | 4 | FPALU | |
| `FCVTZSW` | fcvtzs | 1 | 2 | 4 | FPALU | |
| `FMOVWS` | fmov | 1 | 2 | 2 | FPALU | |
| `FMOVSW` | fmov | 1 | 2 | 2 | FPALU | |

#### 加载（mayLoad）

| Opcode | Mnemonic | Defs | Ops | Lat | Res |
|--------|----------|------|-----|-----|-----|
| `LDRWui` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRWlo` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRWro` | ldr | 1 | 5 | 4 | LoadStore |
| `LDRWpost` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRSui` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRSlo` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRSro` | ldr | 1 | 5 | 4 | LoadStore |
| `LDRSpost` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRDui` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRQui` | ldr | 1 | 3 | 5 | LoadStore |
| `LDRQlo` | ldr | 1 | 3 | 5 | LoadStore |
| `LDRQro` | ldr | 1 | 5 | 5 | LoadStore |
| `LDRQpost` | ldr | 1 | 3 | 5 | LoadStore |
| `LDRXui` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRXlo` | ldr | 1 | 3 | 4 | LoadStore |
| `LDRXro` | ldr | 1 | 5 | 4 | LoadStore |
| `LDRXpost` | ldr | 1 | 3 | 4 | LoadStore |
| `LDPWi` | ldp | 2 | 4 | 5 | LoadStore |
| `LDPSi` | ldp | 2 | 4 | 5 | LoadStore |
| `LDPXi` | ldp | 2 | 4 | 5 | LoadStore |
| `LDPDi` | ldp | 2 | 4 | 5 | LoadStore |
| `LDPQi` | ldp | 2 | 4 | 5 | LoadStore |
| `LDPXpost` | ldp | **3** | 4 | 5 | LoadStore |

`LDPXpost` 额外 **hasSideEffects=T**（后变址更新基址）。

#### 存储（mayStore，Lat 默认 1）

| Opcode | Mnemonic | Defs | Ops | Side |
|--------|----------|------|-----|------|
| `STRWui` | str | 0 | 3 | |
| `STRWlo` | str | 0 | 3 | |
| `STRWro` | str | 0 | 5 | |
| `STRWpost` | str | 0 | 3 | |
| `STRSui` | str | 0 | 3 | |
| `STRSlo` | str | 0 | 3 | |
| `STRSro` | str | 0 | 5 | |
| `STRSpost` | str | 0 | 3 | |
| `STRDui` | str | 0 | 3 | |
| `STRQui` | str | 0 | 3 | |
| `STRQlo` | str | 0 | 3 | |
| `STRQro` | str | 0 | 5 | |
| `STRQpost` | str | 0 | 3 | |
| `STRXui` | str | 0 | 3 | |
| `STRXlo` | str | 0 | 3 | |
| `STRXro` | str | 0 | 5 | |
| `STRXpost` | str | 0 | 3 | |
| `STPWi` | stp | 0 | 4 | |
| `STPSi` | stp | 0 | 4 | |
| `STPXi` | stp | 0 | 4 | |
| `STPDi` | stp | 0 | 4 | |
| `STPQi` | stp | 0 | 4 | |
| `STPXpre` | stp | 0 | 4 | **T** |

所有存储 Res = LoadStore。

#### NEON / 向量

| Opcode | Mnemonic | Defs | Ops | Lat | Res |
|--------|----------|------|-----|-----|-----|
| `DUPv4i32` | dup | 1 | 2 | 6 | FPALU |
| `DUPv4f32` | dup | 1 | 2 | 6 | FPALU |
| `DUPv4sLane` | dup | 1 | 3 | 6 | FPALU |
| `INSv4i32` | ins | 1 | 4 | 6 | FPALU |
| `INSv4f32` | ins | 1 | 4 | 6 | FPALU |
| `EXTRACTv4i32` | umov | 1 | 3 | 6 | FPALU |
| `EXTRACTv4f32` | umov | 1 | 3 | 6 | FPALU |
| `ZIP1v4s` | zip1 | 1 | 3 | 6 | FPALU |
| `ZIP2v4s` | zip2 | 1 | 3 | 6 | FPALU |
| `UZP1v4s` | uzp1 | 1 | 3 | 6 | FPALU |
| `UZP2v4s` | uzp2 | 1 | 3 | 6 | FPALU |
| `TRN1v4s` | trn1 | 1 | 3 | 6 | FPALU |
| `TRN2v4s` | trn2 | 1 | 3 | 6 | FPALU |
| `EXTv16b` | ext | 1 | 4 | 6 | FPALU |
| `REV64v4s` | rev64 | 1 | 2 | 6 | FPALU |
| `ADDv4i32` | add | 1 | 3 | 6 | FPALU |
| `SUBv4i32` | sub | 1 | 3 | 6 | FPALU |
| `MULv4i32` | mul | 1 | 3 | 6 | FPMulDiv |
| `SMINv4i32` | smin | 1 | 3 | 6 | FPALU |
| `SMAXv4i32` | smax | 1 | 3 | 6 | FPALU |
| `NEGv4i32` | neg | 1 | 2 | 6 | FPALU |
| `SSHLv4i32` | sshl | 1 | 3 | 6 | FPALU |
| `USHLv4i32` | ushl | 1 | 3 | 6 | FPALU |
| `SHLiv4i32` | shl | 1 | 3 | 6 | FPALU |
| `SSHRiv4i32` | sshr | 1 | 3 | 6 | FPALU |
| `USHRiv4i32` | ushr | 1 | 3 | 6 | FPALU |
| `MLAv4i32` | mla | 1 | 4 | 6 | FPMulDiv |
| `MLSv4i32` | mls | 1 | 4 | 6 | FPMulDiv |
| `ADDv4f32` | add | 1 | 3 | 6 | FPALU |
| `SUBv4f32` | sub | 1 | 3 | 6 | FPALU |
| `MULv4f32` | fmul | 1 | 3 | 6 | FPMulDiv |
| `DIVv4f32` | fdiv | 1 | 3 | **18** | FPMulDiv |
| `NEGv4f32` | fneg | 1 | 2 | 6 | FPALU |
| `FMLAv4f32` | fmla | 1 | 4 | 6 | FPMulDiv |
| `FMLSv4f32` | fmls | 1 | 4 | 6 | FPMulDiv |
| `ANDv16i8` | and | 1 | 3 | 6 | FPALU |
| `ORRv16i8` | orr | 1 | 3 | 6 | FPALU |
| `EORv16i8` | eor | 1 | 3 | 6 | FPALU |
| `SHUFFLEv16i8` | tbl | 1 | 4 | 6 | FPALU |
| `ADDVv4i32` | addv | 1 | 2 | 6 | FPALU |

向量指令 Term/Branch/Call/Ret/Load/Store/Side/Pseudo/SetF/UseF 均为 F（除上表已列）。

## 1 Selection DAG

### 1.0 数据结构

#### `SDOpcode`

`SDOpcode` 是 SelectionDAG 节点的操作码，类型为 `std::uint16_t`。每个枚举值对应一种 DAG 语义：

| 枚举值 | 含义 |
|--------|------|
| `Invalid` | 无效/已消除节点。`DAGCombiner` 将子节点标记为此值以逻辑删除，不物理移除。 |
| `EntryToken` | 基本块入口 token，作为内存副作用链（chain）的起点。 |
| `Argument` | 函数形参。 |
| `Constant` | 整数常量，或向量常量（lane 数据存于 `shuffleMask`）。 |
| `FPConstant` | 浮点常量（位模式存于 `floatingBits`）。 |
| `GlobalAddress` | 全局变量地址，符号名存于 `symbol`。 |
| `FrameIndex` | 栈帧对象（`alloca`），索引存于 `index`。 |
| `Add` / `Sub` / `Mul` / `SDiv` / `SRem` / `UDiv` / `URem` | 整数二元运算。 |
| `FAdd` / `FSub` / `FMul` / `FDiv` / `FNeg` | 浮点运算。 |
| `Shl` / `LShr` / `AShr` / `And` / `Or` / `Xor` | 位运算。 |
| `ICmp` / `FCmp` | 整数/浮点比较，谓词存于 `predicate`。 |
| `Select` | 条件选择：`cond ? trueVal : falseVal`。 |
| `GEP` | 地址计算（getelementptr）。 |
| `Load` / `Store` | 内存读写，带 chain 依赖。 |
| `ZExt` / `SExt` / `Trunc` | 整数扩展/截断。 |
| `FPToSI` / `SIToFP` | 浮点↔整数转换。 |
| `Bitcast` | 位级重解释。 |
| `Clz` | 前导零计数。 |
| `Splat` | 标量广播为 4-lane 向量（由 combiner 生成）。 |
| `InsertElement` / `ExtractElement` / `ShuffleVector` | 向量 lane 操作。 |
| `Phi` | SSA φ 节点。 |
| `Call` / `TailCall` | 普通调用 / 尾调用。 |
| `Branch` / `BranchCond` | 无条件/条件分支。 |
| `Return` | 返回。 |
| `MAdd` / `MSub` | 整数乘加/乘减融合（`a*b+c` / `a*b-c` 的变体，由 combiner 生成）。 |
| `FMAdd` / `FMSub` | 浮点向量乘加/乘减融合（由 combiner 在 FP 收缩开启时生成）。 |
| `VectorReduceAdd` | 4 个 `ExtractElement` 之和归约为标量（由 combiner 生成）。 |
| `SMin` / `SMax` | 有符号最小/最大（来自 intrinsic 调用）。 |
| `MulMod` | `(a*b)%m` 取模（来自 intrinsic 调用）。 |

**注意**：`opcodeName()` 打印函数未覆盖 `FMAdd`、`FMSub`、`Splat`，这些 opcode 在 `printSelectionDAG` 中会显示为 `"Invalid"`。

#### `SelectionDAG` 

- `nextNodeId_`：自增节点 ID 计数器。
- `nodes_`：`std::vector<std::unique_ptr<SDNode>>`，拥有所有节点。
- `entryToken_`：入口 token 的 `SDValue`。

**构造函数**：创建 `EntryToken` 节点（无结果类型、无操作数），`entryToken_` 指向它。

**`createNode(opcode, resultTypes, operands)`**：
1. 分配新 `SDNode`（ID = `nextNodeId_++`）。
2. 追加到 `nodes_`。
3. 返回引用。

#### `FunctionDAG` 

```cpp
struct FunctionDAG {
    Function *function;                                          // 源函数
    std::vector<BasicBlock *> blockOrder;                          // 基本块遍历顺序
    std::unordered_map<BasicBlock *, std::unique_ptr<SelectionDAG>> blocks;  // 每 BB 一个 DAG
    std::unordered_map<Value *, SDValue> values;                   // IR Value → SDValue 映射
};
```

- 每个基本块有独立 `SelectionDAG`，跨块值通过 `functionDAG.values` 全局共享。
- `blockOrder` 决定 PHI 解析与 combiner 遍历顺序。

### 1.1 SelectionDAG 构建流程

#### Phase 0：检查非声明函数

```cpp
if (!function || function->is_declaration())
    throw std::invalid_argument("cannot build a DAG for a declaration");
```

#### Phase 1：基本块排序

1. 从 `function->basic_blocks_.front()` DFS 后继，记录 `postOrder`
2. 反转得近似 RPO
3. 将 DFS 未访问的块追加到末尾（处理不可达块）

**为何**：RPO 保证大多数 SSA 定义在使用前已建立；不可达块仍保留。

#### Phase 2：为每个基本块创建空 `SelectionDAG`

```cpp
for (BasicBlock *block : result->blockOrder)
    result->blocks.emplace(block, std::make_unique<SelectionDAG>());
```

每个 DAG 构造时自动创建 `EntryToken`。

#### Phase 3: 预注册函数参数

```cpp
SelectionDAG &entryDAG = *result->blocks.at(result->blockOrder.front());
for (Argument *argument : function->arguments_)
    getValue(*result, entryDAG, argument);
```

在入口块 DAG 中创建所有 `Argument` 节点。

#### Phase 4：预扫描 PHI 与 Alloca

遍历 `blockOrder` 中每个块的 `instr_list_`：

##### PHI 分支

```cpp
if (auto *phi = dynamic_cast<PhiInst *>(instruction)) {
    SDNode &node = dag.createNode(SDOpcode::Phi, {valueType(phi->type_)});
    node.origin = phi;
    result->values.emplace(phi, SDValue{&node, 0});
}
```

- 此时**不**填充操作数
- 立即注册到 `values`

**为何**：循环回边 PHI 可能引用尚未线性遍历到的值。

##### Alloca 分支

```cpp
else if (auto *alloca = dynamic_cast<AllocaInst *>(instruction)) {
    SDNode &node = dag.createNode(SDOpcode::FrameIndex, {ValueType::Ptr});
    node.index = nextFrameIndex++;           // 函数级递增
    node.memorySize = typeSize(alloca->alloca_ty_);
    node.alignment = naturalAlignment(alloca->alloca_ty_);
    node.origin = alloca;
    result->values.emplace(alloca, SDValue{&node, 0});
}
```

- `FrameIndex` 在函数内全局编号
- 类型始终 `Ptr`

**为何**：非入口块的 alloca 可能被其他块引用。

#### Phase 5：延迟解析 PHI 的常量/全局操作数

```cpp
std::unordered_map<PhiInst *, std::vector<SDValue>> deferredPhiOperands;
```

对每个块**头部连续 PHI**（遇非 PHI 则 `break`）：

对每个 PHI 的 `(value, bb)` 对：
- 若 `value` 为 `Constant` 或 `GlobalVariable`：
  - 在 `entryDAG` 中 `getValue`（可立即物化）
  - 存入 `deferredPhiOperands[phi][i/2]`
- 其他值留空，阶段 7 再解析

**为何**：常量/全局不依赖遍历顺序；SSA 值须等定义完成。

#### Phase 6：主指令遍历

对每个 `blockOrder` 中的块：

```cpp
SDValue chain = dag.entryToken();
Instruction *skipInstruction = nullptr;
```

按 `instr_list_` 顺序处理：

##### 跳过逻辑

1. `instruction == skipInstruction`：清除标志，`continue`（尾调用已消费 terminator）
2. `PhiInst`：`continue`（已预建）
3. `AllocaInst`：`continue`（已预建）

##### 操作数 lambda

```cpp
auto operand = [&](unsigned index) {
    return getValue(*result, dag, instruction->get_operand(index));
};
```

##### 路径 A：二元运算（`binaryOpcode != Invalid`）

```cpp
created = &dag.createNode(binary, {valueType(instruction->type_)},
                          {operand(0), operand(1)});
```

覆盖所有 `binaryOpcode` 映射的 IR 指令。

##### 路径 B：switch(`instruction->op_id_`)

###### `FNeg`

```cpp
created = &dag.createNode(SDOpcode::FNeg, {valueType(instruction->type_)}, {operand(0)});
```

###### `ICmp`

```cpp
created = &dag.createNode(SDOpcode::ICmp, {valueType(instruction->type_)},
                          {operand(0), operand(1)});
created->predicate = static_cast<int>(static_cast<ICmpInst *>(instruction)->icmp_op_);
```

`ICmpOp` 枚举值（`ICMP_EQ=32` 等）原样存入 `predicate`。

###### `FCmp`

同 `ICmp`，`predicate` 来自 `FCmpInst::fcmp_op_`（`FCMP_FALSE=10` 等）。

###### `Select`

```cpp
operands: {cond, trueVal, falseVal}  // operand(0), (1), (2)
```

###### `GetElementPtr`

```cpp
created = &dag.createNode(SDOpcode::GEP, {ValueType::Ptr}, {operand(0)});
Type *current = static_cast<PointerType *>(operand(0)->type_)->contained_;
for (unsigned i = 1; i < instruction->num_ops_; ++i) {
    created->operands().push_back(operand(i));
    if (i > 1 && current && current->tid_ == Type::ArrayTyID)
        current = static_cast<ArrayType *>(current)->contained_;
    created->gepScales.push_back(typeSize(current));
}
```

- 操作数 0：基址指针
- 操作数 1..N：各层索引
- `gepScales[i-1]`：第 i 个索引的字节缩放（对应该层元素类型大小）
- `current` 更新规则：`i > 1` 且当前为数组时，下钻一层 `contained_`（第一个索引通常只「进入」聚合，不消耗数组维度）

###### `Load`

```cpp
created = &dag.createNode(SDOpcode::Load,
    {valueType(instruction->type_), ValueType::Invalid},  // 值 + chain
    {chain, operand(0)});
created->memorySize = typeSize(instruction->type_);
created->alignment = naturalAlignment(instruction->type_);
chain = SDValue{created, 1};  // 更新 chain 为 Load 的 chain 结果
```

**为何双结果**：内存操作串联副作用链，保证 Load/Store/Call 顺序。

###### `Store`

```cpp
Type *storedType = instruction->get_operand(0)->type_;
created = &dag.createNode(SDOpcode::Store, {ValueType::Invalid},
                          {chain, operand(0), operand(1)});  // chain, value, ptr
created->memorySize = typeSize(storedType);
created->alignment = naturalAlignment(storedType);
chain = SDValue{created, 0};
```

###### `ZExt` / `SExt` / `Trunc`

单操作数，结果类型 `valueType(instruction->type_)`。

###### `FPtoSI` / `SItoFP` / `BitCast` / `Clz`

同上，单操作数一元节点。

###### `InsertElement`

```cpp
SDValue vector = operand(0);
ValueType resultType = valueType(instruction->type_);
if (valueType(instruction->get_operand(0)->type_) != resultType) {
    // 向量操作数类型与结果不一致 → 创建 undef 向量常量
    SDNode &undef = dag.createNode(SDOpcode::Constant, {resultType});
    undef.shuffleMask.assign(4, 0);
    vector = SDValue{&undef, 0};
}
created = &dag.createNode(SDOpcode::InsertElement, {resultType},
                          {vector, operand(1), operand(2)});
```

**为何**：IR 可能从标量/不同类型向量起步插入；用全零 `Constant`+`shuffleMask` 模拟 undef 起始向量。

###### `ExtractElement`

```cpp
operands: {vector, index}
```

###### `ShuffleVector`

```cpp
operands: {v1, v2}
created->shuffleMask = static_cast<ShuffleVectorInst *>(instruction)->mask();
```

###### `Call` — 子分支 1：SignedMinMax intrinsic

```cpp
if (isSignedMinMaxIntrinsic(callee, &minMaxKind)) {
    if (instruction->num_ops_ != 3)
        throw std::logic_error("signed min/max intrinsic must have two operands");
    created = &dag.createNode(
        minMaxKind == SignedMinMaxIntrinsic::SMin ? SDOpcode::SMin : SDOpcode::SMax,
        {valueType(instruction->type_)}, {operand(0), operand(1)});
    break;
}
```

识别 `Function::IntrinsicID::SignedMin/SignedMax`（`llvm.smin.*` / `llvm.smax.*`）。

###### `Call` — 子分支 2：MulMod intrinsic

```cpp
if (isMulModIntrinsic(callee)) {
    if (instruction->num_ops_ != 4)
        throw std::logic_error("mulmod intrinsic must have three operands");
    created = &dag.createNode(SDOpcode::MulMod, {valueType(instruction->type_)},
                              {operand(0), operand(1), operand(2)});
    break;
}
```

识别 `Function::IntrinsicID::MulMod`（`llvm.mulmod.i32`）。

###### `Call` — 子分支 3：尾调用判定

```cpp
bool emitTail =
    callInst->is_tail() &&
    callArgsFitInRegisters(callInst) &&
    term && term->is_ret() &&
    ((callInst->is_void() && term->num_ops_ == 0) ||
     (term->num_ops_ > 0 && term->get_operand(0) == callInst));
```

全部满足时发 `TailCall`：

```cpp
std::vector<SDValue> operands = {chain};
for (unsigned i = 0; i + 1 < instruction->num_ops_; ++i)
    operands.push_back(operand(i));
created = &dag.createNode(SDOpcode::TailCall, {ValueType::Invalid}, std::move(operands));
created->symbol = callee ? callee->name_ : "";
chain = SDValue{created, 0};
skipInstruction = term;  // 跳过紧随的 Ret
break;
```

**尾调用条件解释**：
1. `CallInst::is_tail_` 为真
2. 实参全部 fit 寄存器
3. 块 terminator 为 `Ret`
4. void 调用 + 空 ret，**或** ret 操作数就是该 call 的返回值

**为何 skip Ret**：尾调用复用调用方帧/LR，terminator 由 `TailCall` 承担。

###### `Call` — 子分支 4：普通调用

```cpp
std::vector<SDValue> operands = {chain};
for (unsigned i = 0; i + 1 < instruction->num_ops_; ++i)
    operands.push_back(operand(i));
std::vector<ValueType> results;
if (!instruction->is_void())
    results.push_back(valueType(instruction->type_));
results.push_back(ValueType::Invalid);  // chain
created = &dag.createNode(SDOpcode::Call, std::move(results), std::move(operands));
created->symbol = callee ? callee->name_ : "";
chain = SDValue{created, results.size() - 1};
```

###### `Br` — 无条件（`num_ops_ == 1`）

```cpp
created = &dag.createNode(SDOpcode::Branch, {ValueType::Invalid}, {chain});
created->incomingBlocks.push_back(
    dynamic_cast<BasicBlock *>(instruction->get_operand(0)));
chain = SDValue{created, 0};
```

###### `Br` — 条件（`num_ops_ == 3`）

```cpp
created = &dag.createNode(SDOpcode::BranchCond, {ValueType::Invalid},
                          {chain, operand(0)});  // chain + cond
created->incomingBlocks.push_back(operand(1) 的 BB);  // true 目标
created->incomingBlocks.push_back(operand(2) 的 BB);  // false 目标
chain = SDValue{created, 0};
```

###### `Ret`

```cpp
std::vector<SDValue> operands = {chain};
if (instruction->num_ops_)
    operands.push_back(operand(0));
created = &dag.createNode(SDOpcode::Return, {ValueType::Invalid}, std::move(operands));
chain = SDValue{created, 0};
```

###### `default`

```cpp
throw std::logic_error("SelectionDAGBuilder does not cover an IR opcode");
```

##### 创建后通用处理

```cpp
if (created) {
    created->origin = instruction;
    if (created->opcode() != SDOpcode::TailCall &&
        !instruction->is_void() &&
        instruction->op_id_ != Instruction::Store &&
        instruction->op_id_ != Instruction::Br &&
        instruction->op_id_ != Instruction::Ret)
        result->values[instruction] = SDValue{created, 0};
}
```

**不注册到 `values` 的指令**：`TailCall`、`Store`、`Br`、`Ret`、void 指令。

#### Phase 7：解析 PHI 操作数

```cpp
for (BasicBlock *block : result->blockOrder) {
    for (Instruction *instruction : block->instr_list_) {
        auto *phi = dynamic_cast<PhiInst *>(instruction);
        if (!phi) break;  // 仅处理块头连续 PHI
        SDNode *node = result->values.at(phi).node;
        for (unsigned i = 0; i < phi->num_ops_; i += 2) {
            SDValue incoming = deferredPhiOperands[phi][i / 2];
            if (!incoming)
                incoming = getValue(*result, dag, phi->get_operand(i));
            node->operands().push_back(incoming);
            node->incomingBlocks.push_back(
                dynamic_cast<BasicBlock *>(phi->get_operand(i + 1)));
        }
    }
}
```

每个 PHI 操作数后跟对应 `incomingBlocks` 条目，顺序与 IR 一致。

### 1.2 SelectionDAG 检查流程

`DAGLegalizer::run` 遍历 `functionDAG.blockOrder` 中每个 DAG 的每个节点，执行**校验**（不做图重写）。

#### 规则 1：结果类型合法性

```cpp
for (ValueType type : node->resultTypes()) {
    if (type != ValueType::Invalid && !isSupportedValueType(type))
        throw std::logic_error("DAG contains a type outside the SysY backend scope");
}
```

#### 规则 2：向量操作类型

```cpp
if ((node->opcode() == InsertElement || ExtractElement || ShuffleVector) &&
    node->resultTypes().front() != V4I32 &&
    node->resultTypes().front() != V4F32 &&
    node->opcode() != ExtractElement)  // ExtractElement 例外
    throw std::logic_error("unsupported vector legalization");
```

**注意**：`ExtractElement` 结果为标量（`I32`/`F32`），故排除在校验外；`InsertElement`/`ShuffleVector` 结果须为 `V4I32`/`V4F32`。

#### 规则 3：SMin/SMax 类型

```cpp
if ((node->opcode() == SMin || SMax) &&
    resultTypes().front() != I32 && != V4I32)
    throw std::logic_error("unsupported signed min/max type");
```

### 1.3 SelectionDAG 合并流程

#### 预备：使用计数

```cpp
std::unordered_map<SDNode *, unsigned> useCount;
// 遍历所有节点的所有 operand.node，++useCount[operand.node]
```

用于判断子节点是否可安全消除（`useCount == 1`）。

---

#### 合并模式 1：整数 MAdd（`Add` + 单 use `Mul`）

**匹配条件**：
- `node.opcode() == Add`
- 2 个操作数
- 结果类型 `I32` 或 `V4I32`
- 某个操作数为 `Mul` 且 `useCount[mul] == 1`

**变换**（`multiplyIndex` 为 0 或 1）：

```cpp
SDValue addend = node.operands()[1 - multiplyIndex];
node.setOpcode(SDOpcode::MAdd);
node.operands() = {mul->operands()[0], mul->operands()[1], addend};
multiply->setOpcode(SDOpcode::Invalid);
```

语义：`Add(Mul(a,b), c)` → `MAdd(a, b, c)`；若 `Mul` 在操作数 1，则 `Add(c, Mul(a,b))` 同样处理。

**为何**：AArch64 `madd`/`mla` 单指令完成乘加，且 `useCount==1` 保证 `Mul` 无其他引用。

---

#### 合并模式 2：整数 MSub（`Sub` + 单 use `Mul`）

**匹配条件**：
- `node.opcode() == Sub`，结果 `I32`/`V4I32`
- **操作数 1**（减数）为 `Mul`，`useCount == 1`

**变换**：

```cpp
SDValue minuend = node.operands()[0];
node.setOpcode(SDOpcode::MSub);
node.operands() = {mul->operands()[0], mul->operands()[1], minuend};
multiply->setOpcode(SDOpcode::Invalid);
```

语义：`Sub(minuend, Mul(a,b))` → `MSub(a, b, minuend)`（即 `a*b - minuend` 的 AArch64 `msub` 形式）。

**注意**：仅匹配 `Mul` 在**右侧**（操作数 1），不匹配 `Sub(Mul(a,b), x)`。

---

#### 合并模式 3：浮点向量 FMAdd（需 FP 收缩）

**启用条件**：

```cpp
static bool fpContractionEnabled() {
    return std::getenv("SYSY_ENABLE_FP_CONTRACT") != nullptr;
}
```

**匹配条件**：
- `node.opcode() == FAdd`，结果 `V4F32`
- `fpContractionEnabled()`
- 某操作数为单 use `FMul`

**变换**：同 MAdd，opcode 改为 `FMAdd`，操作数 `{a, b, addend}`。

**为何需环境变量**：FMA 将两次舍入合并为一次，可能改变 IEEE-754 结果（若干 ulp）；默认关闭以保持与未收缩代码位精确一致。

---

##### 合并模式 4：浮点向量 FMSub（需 FP 收缩）

**匹配条件**：
- `node.opcode() == FSub`，结果 `V4F32`
- `fpContractionEnabled()`
- 操作数 1 为单 use `FMul`

**变换**：同 MSub，opcode 改为 `FMSub`。

---

#### 合并模式 5：Full Splat（`InsertElement` 链 → `Splat`）

遍历所有节点，对匹配 `matchFullSplat` 的 `root`：

```cpp
root.setOpcode(SDOpcode::Splat);
root.operands() = {match.scalar};
// 消除辅助节点（均设 Invalid）：
for (insert in match.inserts, insert != &root)
    insert->setOpcode(Invalid);
for (index in match.indices, useCount[index] == 1)
    index->setOpcode(Invalid);
for (redundant in match.redundantScalars, useCount[redundant] == 1)
    redundant->setOpcode(Invalid);
if (match.base && match.base->opcode() == Constant && useCount[base] == 1)
    match.base->setOpcode(Invalid);
```

**为何在 DAG 层做**：所有产生 4-lane 插入链的中端 pass 都能受益，后端可用 `DUP` 指令。

---

#### 合并模式 6：VectorReduceAdd（四 ExtractElement 之和）

**匹配条件**（对 `root` 为 `Add`、结果 `I32`）：

递归 `collect(node, isRoot)`：

| 节点类型 | 条件 | 动作 |
|----------|------|------|
| `Add` | 非 root 时 `useCount==1` | 加入 `consumed`，递归两操作数 |
| `ExtractElement` | 2 操作数，`useCount==1` | 索引为 `Constant` 且 `∈[0,3]`；源向量类型 `V4I32`；所有 Extract 同源 `vector`；记录 lane 到 `lanes` |
| 其他 | — | 失败 |

成功条件：
- `lanes.size() == 4`（4 个不同 lane）
- `consumed.size() == 7`（3 层 Add 树 + 4 个 Extract = 7 节点）

**变换**：

```cpp
root.setOpcode(SDOpcode::VectorReduceAdd);
root.operands() = {SDValue{vector, 0}};
for (node in consumed, node != &root)
    node->setOpcode(Invalid);
```

语义：`extract(v,0)+extract(v,1)+extract(v,2)+extract(v,3)` → `ADDV`（水平求和）。

**结构要求**：必须是深度 3 的完全 Add 二叉树（7 节点），任意排列的 4 个 Extract 均可被递归收集。

### 1.4 应用示例

进入后端的 IR（节选：局部数组初始化、带 PHI 的循环、`mul+add`、`select`、随后并行归约入口）：

```text
  %local = alloca [4 x i32]
  %2 = call i32 @getint()
  %3 = call i32 @getint()
  %arrayidx = getelementptr [4 x i32], [4 x i32]* %local, i32 0, i32 0
  store i32 %3, i32* %arrayidx
  %arrayidx1 = getelementptr [4 x i32], [4 x i32]* %local, i32 0, i32 1
  %add = add i32 %3, 1
  store i32 %add, i32* %arrayidx1
  %arrayidx2 = getelementptr [4 x i32], [4 x i32]* %local, i32 0, i32 2
  %add1 = add i32 %3, 2
  store i32 %add1, i32* %arrayidx2
  %arrayidx3 = getelementptr [4 x i32], [4 x i32]* %local, i32 0, i32 3
  %add2 = add i32 %3, 3
  store i32 %add2, i32* %arrayidx3
  br label %while.cond
while.cond:                                                ; preds = %entry, %while.body
  %phi = phi i32 [ %1, %entry ], [ %7, %while.body ]
  %phi1 = phi i32 [ 0, %entry ], [ %add4, %while.body ]
  %phi2 = phi i32 [ 0, %entry ], [ %7, %while.body ]
  %cmp = icmp slt i32 %phi1, %2
  br i1 %cmp, label %while.body, label %while.cond1.loopexit
while.body:                                                ; preds = %while.cond
  %4 = ashr i32 %phi1, 2
  %5 = shl i32 %4, 2
  %sub = sub i32 %phi1, %5
  %arrayidx4 = getelementptr [4 x i32], [4 x i32]* %local, i32 0, i32 %sub
  %6 = load i32, i32* %arrayidx4
  %mul = mul i32 %6, %0
  %add3 = add i32 %mul, %phi2
  %cmp1 = icmp slt i32 %add3, 0
  %sub1 = sub i32 0, %add3
  %7 = select i1 %cmp1, i32 %sub1, i32 %add3
  call void @putint(i32 %7)
  %add4 = add nuw nsw i32 %phi1, 1
  br label %while.cond
while.cond1.loopexit:                                                ; preds = %while.cond
  store i32 %0, i32* @__sysy_par_ctx_0_0
  store i32 0, i32* @__sysy_par_scalar_start_0
  store i32 %2, i32* @__sysy_par_scalar_bound_0
  store i32 0, i32* @__sysy_par_scalar_partial_0_0
  store i32 0, i32* @__sysy_par_scalar_partial_0_1
  call void @__sysy_parallel_for(i32 0, i32 0, i32 %2)
  %8 = load i32, i32* @__sysy_par_scalar_partial_0_0
  %9 = load i32, i32* @__sysy_par_scalar_partial_0_1
  %add5 = add i32 %8, %9
  %add7 = add i32 %phi2, %add5
  store i32 %0, i32* @scale
  store i32 %phi, i32* @slot
  ret i32 %add7
}
declare void @__sysy_parallel_for(i32, i32, i32)
```

**构建**后循环体 DAG 中，`Mul` 已被合并标记，`Add(Mul,acc)` 成为 `MAdd`；`Select` 保留；Load/Call 挂在 chain 上：

```text
selection-dag main {
  block entry:
    t0 = EntryToken
    t1 = FrameIndex: ptr
    t2 = Constant: i32
    t3 = Constant: i32
    t4 = GlobalAddress: ptr
    t5 = Load: i32 invalid t0.0 t4.0
    t6 = GlobalAddress: ptr
    t7 = Load: i32 invalid t5.1 t6.0
    t8 = Call: i32 invalid t7.1
    t9 = Call: i32 invalid t8.1
    t10 = GEP: ptr t1.0 t11.0 t12.0
    t11 = Constant: i32
    t12 = Constant: i32
    t13 = Store: invalid t9.1 t9.0 t10.0
    t14 = GEP: ptr t1.0 t15.0 t16.0
    t15 = Constant: i32
    t16 = Constant: i32
    t17 = Constant: i32
    t18 = Add: i32 t9.0 t17.0
    t19 = Store: invalid t13.0 t18.0 t14.0
    t20 = GEP: ptr t1.0 t21.0 t22.0
    t21 = Constant: i32
    t22 = Constant: i32
    t23 = Constant: i32
    t24 = Add: i32 t9.0 t23.0
    t25 = Store: invalid t19.0 t24.0 t20.0
    t26 = GEP: ptr t1.0 t27.0 t28.0
    t27 = Constant: i32
    t28 = Constant: i32
    t29 = Constant: i32
    t30 = Add: i32 t9.0 t29.0
    t31 = Store: invalid t25.0 t30.0 t26.0
    t32 = Branch: invalid t31.0
  block while.cond:
    t0 = EntryToken
    t1 = Phi: i32 t7.0 t15.0
    t2 = Phi: i32 t2.0 t18.0
    t3 = Phi: i32 t3.0 t15.0
```

**检查**：上表节点结果类型均为 `I32`/`Ptr`/`I1`/`Invalid(chain)`，无非法向量/`SMin` 形态，Legalizer 通过且不改图。

**合并**：`t9 = Invalid`（原 `Mul`）+ `t10 = MAdd` 即模式「`Add` + 单 use `Mul`」；chain `Load → … → Call putint → Branch` 未改。并行体 DAG 中同样出现 `MAdd` 与多组 `Phi`：

```text
  block while.body1:
    t0 = EntryToken
    t1 = Invalid: i32 t1.0 t5.0
    t2 = MAdd: i32 t1.0 t5.0 t2.0
    t3 = Constant: i32
    t4 = Add: i32 t1.0 t3.0
    t5 = Branch: invalid t0.0
```

## 2 Instruction Selection (ISel)

### 整体流水线

| 阶段 | 内容 |
|------|------|
| 1 | 创建 `MachineFunction`，设置 `IsSSA`、`HasPHIs`、`Legalized`、`Selected` |
| 2 | IR `BasicBlock` → `MachineBasicBlock` 映射，复制 CFG 后继 |
| 3 | 分析 `directGlobalMemory`（全局地址是否仅用于直接 load/store） |
| 4 | **预分配 VReg**（所有有结果类型的 SDNode） |
| 5 | 定义核心 lambda（`resultReg`、`use`、`define`、`append`、`emitSignedConstantDivision`、`emitVectorConstant` 等） |
| 6 | 创建 `FrameIndex` 对应栈对象 |
| 7 | 分析函数入口参数布局（GPR/SIMD 双银行 + 栈偏移） |
| 8 | 按块、按节点顺序 `switch (node.opcode())` 发射 MachineInstr |

`selection_dag.hpp` 中全部 56 个 `SDOpcode` 均在 `switch` 中有对应分支（部分合并处理）。以下按源码 `switch` 顺序穷尽描述。

### 2.1 `SDOpcode::Invalid`

**发射**：无  
**原因**：占位/无效节点。

### 2.2 `SDOpcode::EntryToken`

**发射**：无  
**原因**：控制依赖 token，无值结果。

### 2.3 `SDOpcode::Argument`

**操作数**：`node.index` 为参数编号。

#### 寄存器传参（bank index < 8）

```
COPY vreg, physReg
  parallelCopyGroup = entryArgumentCopyGroup
  physReg = Xi 或 Vi（由 argumentIsFloat 决定）
```

**原因**：从 ABI 物理寄存器拷贝到 VReg；group 标记并行 COPY。

#### 栈传参（bank index ≥ 8）

1. `fixed = frameInfo().createFixedObject(size, stackOffset, align)`
   - NEON128：16 字节；其余：8 字节
2. `SPILL_LOAD vreg, frameIndex(fixed)` + `MachineMemOperand`

**原因**：保留 fixed incoming frame 引用贯穿寄存器分配；帧降低可选 x29 相对寻址，避免每个栈参数先物化地址。

### 2.4 `SDOpcode::Constant`

#### 向量类型（V4I32 / V4F32）

从 `node.shuffleMask` 填 4 个 lane → `emitVectorConstant(block, define(node), lanes, &node)`

#### 标量类型

| 类型 | Opcode |
|------|--------|
| Ptr, I64 | MOVi64 |
| 其他 | MOVi32 |

操作数：`define(node), immediate(node.integer)`  
附加：`registerInfo.get(...).rematerializable = true`

**原因**：标量常量可重物化，减轻寄存器压力。

### 2.5 `SDOpcode::FPConstant`

1. `MOVi32 bits, node.floatingBits`（临时 VReg）
2. `FMOVSW vreg, bits`

**原因**：AArch64 无通用 float 立即数 load；通过 GPR 位模式 + `FMOV` 从整数转 float。

### 2.6 `SDOpcode::GlobalAddress`

#### Direct global memory（`directGlobalMemory[&node] == true`）

```
ADRP vreg, symbol
```

**原因**：仅作 load/store 基址时，`*lo` 指令自带符号低 12 位，页基址足够。

#### 需完整指针

```
ADRP page, symbol          ; 临时 VReg
ADDlow vreg, page, symbol
rematerializable = true
```

**原因**：GEP、算术等需要完整 64 位地址；ADDlow 补全页内偏移。

### 2.7 `SDOpcode::FrameIndex`

```
LEA_FRAME vreg, frameIndex(node.index)
```

**原因**：栈槽抽象地址；后续帧降低与 SP 合成或选 x29 相对模式。

### 2.8 二元运算组（合并 case）

**包含 SDOpcode**：`Add`, `Sub`, `Mul`, `SDiv`, `UDiv`, `And`, `Or`, `Xor`, `Shl`, `LShr`, `AShr`, `FAdd`, `FSub`, `FMul`, `

#### 2.8.1 基础 Opcode 表

**V4I32（integerVector）**：

| SDOpcode | Machine Opcode | 备注 |
|----------|----------------|------|
| Add | ADDv4i32 | |
| Sub | SUBv4i32 | |
| Mul | MULv4i32 | |
| And | ANDv16i8 | 128 位按 16×i8 |
| Or | ORRv16i8 | |
| Xor | EORv16i8 | |
| Shl | SSHLv4i32 | 寄存器移位量变体 |
| LShr | USHLv4i32 | |
| AShr | SSHLv4i32 | 与 Shl 同 opcode，右移见 5.8.6 |
| SDiv/UDiv | — | 无映射，抛 unsupported |

**V4F32（floatingVector）**：

| SDOpcode | Opcode |
|----------|--------|
| FAdd | ADDv4f32 |
| FSub | SUBv4f32 |
| FMul | MULv4f32 |
| FDiv | DIVv4f32 |

**标量 I64（integer64）**：

| SDOpcode | Opcode |
|----------|--------|
| Add | ADDXrr |
| Sub | SUBXrr |
| Mul | MULXrr |
| SDiv | SDIVXrr |
| UDiv | UDIVXrr |
| And | ANDXrr |
| Or | ORRXrr |
| Xor | EORXrr |
| Shl | LSLXrr |
| LShr | LSRXrr |
| AShr | ASRXrr |

**标量 I32 / F32**：

| SDOpcode | Opcode |
|----------|--------|
| Add | ADDWrr |
| Sub | SUBWrr |
| Mul | MULWrr |
| SDiv | SDIVWrr |
| UDiv | UDIVWrr |
| And | ANDWrr |
| Or | ORRWrr |
| Xor | EORWrr |
| Shl | LSLWrr |
| LShr | LSRWrr |
| AShr | ASRWrr |
| FAdd | FADDS |
| FSub | FSUBS |
| FMul | FMULS |
| FDiv | FDIVS |

若 `opcode == Invalid` 抛 `"unsupported typed binary oion"`。

#### 2.8.2 32 位乘法强度削减（Mul，非向量、非 I64）

检测 lhs 或 rhs 为 `Constant`：

| 因子 factor | 约减 Opcode | 附加 |
|-------------|-------------|------|
| 0 | MOVi32 dst, 0 | |
| +1 | COPY | |
| -1 | NEGW | |
| 2^k (k>0) | LSLWri dst, var, #k | |
| 2^k+1 | ADDWlsl dst, var, var, #k | `var + var<<k` |
| 负因子（magnitude≠1） | 上述 + NEGW | 先写临时 VReg 再取负 |
**：移位和 `ADDWlsl` 比乘法快；编译期可确定因子时避免 `MULWrr`。

#### 2.8.3 32 位有符号常量除法（SDiv）

条件：非向量、非 I64、RHS 为 `Constant`  
调用 `emitSignedConstantDivision(block, define(node), use(lhs), dr, &node)`；成功则 `break`。

#### 2.8.4 向量立即数移位（Shl/LShr/AShr + Splat(Constant)）

RHS 为 `Splat`，其操作数为 `Constant`：

| SDOpcode | Opcode | shift 范围 |
|----------|--------|------------|
| Shl | SHLiv4i32 | [0, 31) |
| LShr | USHRiv4i32 | (0, 31) |
| AShr | SSHRiv4i32 | (0, 31) |
**：立即数移位单条完成，优于寄存器 SSHL/USHL。

#### 2.8.5 向量乘 2 的幂（Mul + Splat(Constant)）

因子为正、≤ INT32_MAX、为 2^k：

```
SHLiv4i32 dst, value, #log2(k)
```

#### 2.8.6 标量立即数形式候选

对 Add/Sub/And/Shl/LShr/AShr（非向量、非浮点向量）设置 `immediateOpcode`：

| SDOpcode | W | X |
|----------|---|---|
| Add | ADDWri | ADDXri |
| Sub | SUBWri | SUBXri |
| And | ANDWri | （见下） |
| Shl | LSLWri | LSLXri |
| LShr | LSRWri | LSRXri |
| AShr | ASRWri | ASRXri |

**注意**：`ANDWri` 虽被设为候选，但 `InstrInfo::acceptsImmediate` **不包含** `ANDWri`（仅 TSTWri 用 logical immediate），故 And  `ANDWrr`/`ANDXrr`。

#### 2.8.7 向量右移寄存器形式（LShr/AShr，非立即数路径）

```
NEGv4i32 negated, rhs
SSHLv4i32 / USHLv4i32 dst, lhs, negated
```
**：NEON SSHL/USHL 用有符号 lane 移位量；右移 = 左移负量。

#### 2.8.8 默认二元发射

构造 `instruction(opcode)`：`define(node), use(op0)`  
若 RHS 为 Constant 且 `acceptsImmediate(immediateOpcode, rhs->integer)`：  
→ `setOpcode(immediateOpcode)` + `immediate(rhs->integer)`  
否则：`use(op1)`  
`append(block, instruction, &node)`

### 2.9 `SDOpcode::SMin` / `SDOpcode::SMax`

#### V4I32

```
SMINv4i32 / SMAXv4i32 dst, op0, op1
```

#### I32

1. `CMPWrr op0, op1`（定义 NZCV）
2. `CSELW dst, op0, op1, LT/GT`

SMin 用 `CondCode::LT`；SMax 用 `CondCode::GT`。

**原因**：标量无 MIN/MAX 指令，比较 + 条件选择。

其他类型抛 `"unsupported signed min/max type"`。

### 2.10 `SDOpcode::MulMod`

**语义**：`(a * b) mod m`，`m` 为 `operands[2]` 的非零常量。

**操作数**：`operands[0]=a`, `operands[1]=b`, `operands[2]=modulus`

#### 第一步：64 位积

```
SMULLXrr productReg, a, b
```

#### 辅助 `emitXTemp(opcode, ops)`

创建 64 位临时 VReg，发射指令，返回 `MachineOperand::vreg`。

#### 回退路径（!info.reducible || modulus < 0）

```
MOVi64 modulusReg, modulus
SDIVXrr quotient, product, modulusReg
MSUBXrrr remainder, quotient, modulusReg, product
COPYXtoW dst, remainder
```

**原因**：不支持 magic/Barrett 或负模数时用 64 位除法。

#### 优化路径（可约且 modulus > 0）

**步骤 1：取绝对值**

```
CMPXri product, #0
NEGX negatedProduct, product
CSELX absReg, negatedProduct, product, LT
```

**步骤 2a：m 为 2^k**

```
COPYXtoW absW, absolute
ANDWri remW, absW, #(magnitude-1)
UXTW remainderX, remW
```

**步骤 2b：一般 m（Barrett）**

```
MOVi64 muReg, barrett.mu
MOVi64 modulusReg, magnitude
UMULHXrr quotient, absolute, muReg
MSUBXrrr provisional, quotient, modulusReg, absolute
SUBXrr adjusted, provisional, modulusReg
CMPXrr provisional, modulusReg
CSELX remReg, adjusted, provisional, HS
```

Barrett：`μ = floor(2^64 / m)`；`r = n - umulh(n,μ)*m`，一次条件减足够。

**步骤 3：恢复符号**

```
NEGX negatedRem, remainderX
CMPXri product, #0
CSELX signedRem, negatedRem, remainderX, LT
COPYXtoW dst, signedRem
```

**原因**：向零取余 = `|ab| mod |m|` 再按 `ab` 符号恢复。

### 2.11 `SDOpcode::SRem` / `SDOpcode::URem`

**通用公式**：`remainder = numerator - quotient * divisor`（MSUB）

#### SRem + 32 位常量除数优化

**magnitude == 1**：`MOVi32 dst, 0`

**m 为 2^k**：

1. `CMPWri numerator, #0`（定义 NZCV）
2. 若 shift != 1：`CNEGW absolute, numerator, MI` 取绝对值
3. `ANDWri masked, magnitude, #(m-1)`
4. `CNEGW dst, masked, MI` 恢复符号

**其他可约常量**：`emitSignedConstantDivision` 算 quotient 到临时 VReg

#### 通用路径

1. 算 quotient：
   - 上述 magic 路径，或
   - `SDIVWrr/SDIVXrr`（SRem）/ `UDIVWrr/UDIVXrr`（URem）
2. `MSUBWrrr/MSUBXrrr dst, quotient, divisor, numerator`

**原因**：MSUB 单条完成 `a - q*d`，避免显式乘法。

### 2.12 `SDOpcode::MAdd` / `SDOpcode::MSub`

**语义**：`a * b + c` / `a * b - c`（操作数顺序见下）

#### V4I32

```
MLAv4i32 / MLSv4i32 dst, acc, a, b
```

- `operands[0].isEarlyClobber = true`（dst）
- `operands[1].tiedTo = 0`（acc 与 dst 同寄存器）
- 操作数顺序：`dst, acc(op2), a(op0), b(op1)`

#### 标量 I32

```
MADDWrrr / MSUBWrrr dst, a, b, c
```

操作数：`dst, op0, op1, op2`

### 2.13 `SDOpcode::FMAdd` / `SDOpcode::FMSub`

```
FMLAv4f32 / FMLSv4f32 dst, a, b, acc
```

- `operands[3].tiedTo = 0`（acc 即 dst，原地累加）
- 操作数：`define(node), op0, op1, op2`（acc 为 op2）

**原因**：匹配 AArch64 FMLA/FMLS 语义；regalloc 保证 vd 与累加同寄存器。

### 2.14 `SDOpcode::FNeg`

| 结果类型 | Opcode |
|----------|--------|
| V4F32 | NEGv4f32 |
| F32 | FNEGS |

### 2.15 `SDOpcode::ICmp` / `SDOpcode::FCmp`

**结果**：i1 布尔值（0/1）。

#### FCMP_FALSE / FCMP_TRUE

```
MOVi32 dst, 0/1
```

不发射比较指令。

#### 比较指令选择

| 条件 | Opcode |
|------|--------|
| 整数 + RHS 常量 + acceptsImmediate | CMPWri / CMPXri |
| 浮点 + RHS 为零 FPConstant | FCMPZS |
| 浮点一般 | FCMPSrr |
| 整数一般 | CMPWrr / CMPXrr |

均附加 NZCV 定义操作数：`physReg(NZCV, CCR, def=true, live=true)`

#### FCMP_ONE / FCMP_UEQ（需组合）

**FCMP_ONE**（有序不等）：

1. `CSETW first, NE`
2. `CSETW second, VC`
3. `ANDWrr dst, first, second`

**FCMP_UEQ**（无序相等）：

1. `CSETW first, EQ`
2. `CSETW second, VS`
3. `ORRWrr dst, first, second`

#### 默认

```
CSETW dst, condition
```

condition 来自 `integerCondition` 或 `floatingCondition`。

### 2.16 `SDOpcode::Select`

**语义**：`cond ? true_val : false_val`（cond 为 i1）

1. `CMPWri cond, #0`（定义 NZCV）
2. 按 true_val 的 RegClass 选 Opcode：
   - FPR32 → `FCSELS`
   - GPR64 → `CSELX`
   - 其他 → `CSELW`
3. `select dst, true_val, false_val, NE`

**原因**：非零为真（NE），选第一个值。

### 2.17 `SDOpcode::GEP`

从 `operands[0]` 基址开始，逐索引累加；`gepScales[i-1]` 为第 i 个索引的缩放。

#### 常量索引

- `offset = index * scale`
- `0 ≤ offset ≤ 4095`：`ADDXri dst, current, #offset`
- 否则：`MOVi64 offsetReg` + `ADDXrr dst, current, offsetReg`

#### 非常量索引 + scale 为 2^k 且 ≤ 8

```
ADDXrs dst, current, index, #log2(scale), #1
```

#### 非常量索引 + scale 为 2^k 且 > 8

```
SXTW extended, index
ADDXrs dst, current, extended, #log2(scale), #2
```

shift type #2 表示扩展后的 64 位索引。

#### 一般 scale

```
MOVi32 scaleReg, scale
SMADDLXrrr dst, index, scaleReg, current
```

**原因**：`base + sext(index)*scale` 单条 SMADDL 精确表达；在 isel 阶段选定，避免 RA/打印器事后发明 scratch。

#### 仅基址（operands.size()==1）

```
COPY dst, base
```

中间步骤用临时 VReg；最后一步 `setDefinition` 到结果节点。

### 2.18 `SDOpcode::Load`

按结果 RegClass 与 direct global 选 opcode：

| RegClass | direct global | 一般 |
|----------|---------------|------|
| FPR32 | LDRSlo | LDRSui |
| NEON128 | LDRQlo | LDRQui |
| GPR64 | LDRXlo | LDRXui |
| GPR32 | LDRWlo | LDRWui |

操作数：

```
load dst, address
; direct global: 第三操作数 global(symbol)
; 否则: immediate(0)
```

附加 `MachineMemOperand{Load, memorySize, alignment, origin}`。

### 2.19 `SDOpcode::Store`

与 Load 对称：

| RegClass | direct global | 一般 |
|----------|---------------|------|
| FPR32 | STRSlo | STRSui |
| NEON128 | STRQlo | STRQui |
| GPR64 | STRXlo | STRXui |
| GPR32 | STRWlo | STRWui |

操作数：`value, address, global/immediate(0)` + Store mem operand。  
**注意**：Store 不调用 `append(..., &node)`，因 store 无定义 VReg。

### 2.20 `SDOpcode::ZExt` / `SExt` / `Trunc` / `Bitcast`

| 转换 | 发射 |
|------|------|
| I32/I1 → I64，SExt | SXTW |
| I32/I1 → I64，ZExt | UXTW |
| I64 → I32/I1 | COPYXtoW |
| 其他同宽/无关 | COPY |

### 2.21 `SDOpcode::FPToSI` / `SIToFP`

| SDOpcode | Opcode | 语义 |
|----------|--------|------|
| FPToSI | FCVTZSW | float→int，向零 |
| SIToFP | SCVTFWS | int→float |

### 2.22 `SDOpcode::Clz`

```
CLZW dst, src
```

### 2.23 `SDOpcode::Splat`

```
DUPv4f32 / DUPv4i32 dst, scalar
```

**原因**：标量广播，非向量立即数物化；GPR 标量可被整数用户复用；pre-RA 窥孔可将孤立 mov+dup 提升为 NEON 形式。

### 2.24 `SDOpcode::InsertElement`

要求 lane 为常量且 ∈ [0,3]：

```
INSv4f32 / INSv4i32 dst, vector, element, lane
operands[1].tiedTo = 0
```

vector 操作数 tied 到 dst（原地插入）。

### 2.25 `SDOpcode::ExtractElement`

```
EXTRACTv4f32 / EXTRACTv4i32 dst, vector, lane
```

lane 必须为常量 ∈ [0,3]。

### 2.26 `SDOpcode::ShuffleVector`

**掩码约定**：源向量 0 的 lane 0–3 → 掩码 0–3；源向量 1 → 4–7。

#### 内部辅助 lambda

| 名称 | 发射 |
|------|------|
| emitBinaryShuffle(op) | 双操作数 shuffle |
| emitUnaryShuffle(op, src) | 单操作数 |
| emitCopy(src) | COPY |
| emitDupLane(src, lane) | DUPv4sLane |
| emitExt(low, high, byteOff) | EXTv16b |

#### 固定模式表（按源码顺序）

| 掩码 | 发射 | 语义 |
|------|------|------|
| {0,1,2,3} | COPY op0 | 取向量 0 |
| {4,5,6,7} | COPY op1 | 取向量 1 |
| 四 lane 相同 s∈[0,7] | DUPv4sLane op(s<4?0:1), s&3 | 广播单 lane |
| {0,4,1,5} | ZIP1v4s | 低半交错 |
| {2,6,3,7} | ZIP2v4s | 高半交错 |
| {0,2,4,6} | UZP1v4s | 偶位解压 |
| {1,3,5,7} | UZP2v4s | 奇位解压 |
| {0,4,2,6} | TRN1v4s | 低半转置 |
| {1,5,3,7} | TRN2v4s | 高半转置 |
| {1,0,3,2} | REV64v4s op0 | 64 位内交换 |
| {5,4,7,6} | REV64v4s op1 | 同上 |
| {3,2,1,0} | REV64v4s op0 → EXTv16b #8 | 四 .s 全反序 |
| {7,6,5,4} | REV64v4s op1 → EXTv16b #8 | 同上 |
| {s,s+1,s+2,s+3}, s∈{1,2,3} | EXTv16b op0, op1, s*4 | 跨向量滑动窗口 |

#### 通用回退

1. 将 lane 掩码扩展为字节索引 `packedMask`（每 lane 4 字节）
2. `emitVectorConstant` 物化 mask 向量
3. `SHUFFLEv16i8 dst, op0, op1, maskReg`

**原因**：TBL 通用但慢；常见模式用单条 NEON 指令。

### 2.27 `SDOpcode::VectorReduceAdd`

1. `ADDVv4i32 reduced, vector`（结果在 FPR32 VReg）
2. `FMOVWS dst, reduced`

**原因**：ADDV 归约到标量 float 寄存器槽。

### 2.28 `SDOpcode::Phi`

```
PHI dst, val0, bb0, val1, bb1, ...
```

每对：incoming 值 + `MachineOperand::block(incomingBlocks[i])`。

### 2.29 `SDOpcode::Call`

#### 参数分拣（operands[1..]）

- GPR 银行：X0–X7；SIMD 银行：V0–V7（FPR32 / NEON128）
- index ≥ 8 → 栈参数，8/16 字节对齐
- `outgoingStackSize` 向上取 16 字节；更新 `maxCallFrameSize`

#### 发射顺序

1. **栈空间**：`ADJCALLSTACKDOWN #outgoingStackSize`（若 >0）
2. **栈传参**：`STR*ui value, SP, offset`
   - offset 不可编码（非 width 倍数或 >4095×width）：`MOVi64` + `ADDXrr` 算地址，memoryOffset=0
3. **寄存器传参**：`COPY physReg(Xi/Vi), value`，`parallelCopyGroup = callCopyGroup`
4. **调用**：`CALL symbol, registerMask(callPreservedMask())`；`hasCalls = true`
5. **恢复栈**：`ADJCALLSTACKUP #outgoingStackSize`
6. **返回值**：若有结果，`COPY vreg, X0` 或 `V0`

### 2.30 `SDOpcode::TailCall`

- **仅寄存器参数**；index ≥ 8 抛 `"TailCall selected with stack-passed arguments"`
- `COPY` 到 Xi/Vi（`callCopyGroup`）
- `TAILCALL symbol, registerMask`
- **不**设 `hasCalls`、**无** ADJCALLSTACK

**原因**：尾调用复用调用者栈；全尾调用函数可 frameless。

### 2.31 `SDOpcode::Branch`

```
B target_bb
```

`target_bb = incomingBlocks[0]`。

### 2.32 `SDOpcode::BranchCond`

```
CBNZ cond, true_bb    ; operands[1] 为条件，incomingBlocks[0] 为真分支
B false_bb            ; incomingBlocks[1] 为假分支
```

**原因**：非零跳真；假分支需显式 B。

### 2.33 `SDOpcode::Return`

- 若有返回值（operands.size()>1）：`COPY X0/V0, value`
- `RET`

### 2.34 除法算法

**算法**（Granlund-Montgomery 风格）：

1. `precision = 31`
2. 初始化 quotient1/remainder1（基于 cutoff）与 quotient2/remainder2（基于 absoluteDivisor）
3. **do-while 循环**增 precision：
   - 倍增 quotient1/remainder1、quotient2/remainder2
   - 条件进位
   - 计算 delta = absoluteDivisor - remainder2
   - 继续直到 `quotient1 >= delta` 且 `(quotient1 > delta || remainder1 != 0)`
4. `multiplier = quotient2 + 1`；divisor<0 则取负
5. `shift = precision - 32`
6. 策略选择：
   - `divisor>0 && multiplier<0` → MultiplyAddShift
   - `divisor<0 && multiplier>0` → MultiplySubShift
   - 否则 → MultiplyShift

### 应用示例

ISel 后循环头仍含 `PHI`，条件为 `cmp`+`cset`+`cbnz`；循环体为变址 `ldr`、`madd`、`csel`、`bl putint`：

```text
  bb.1 while.cond: successors %bb.3 %bb.2
    PHI def %28:gpr32, %7:gpr32, %bb.0, %63:gpr32, %bb.3
    PHI def %29:gpr32, %2:gpr32, %bb.0, %65:gpr32, %bb.3
    PHI def %30:gpr32, %3:gpr32, %bb.0, %63:gpr32, %bb.3
    cmp %29:gpr32, %8:gpr32, def implicit $nzcv
    cset def %31:gpr32, cc(11), implicit $nzcv
    cbnz %31:gpr32, %bb.3
    b %bb.2
  bb.3 while.body: successors %bb.1
    MOVi32 def %50:gpr32, 2
    asr def %51:gpr32, %29:gpr32, 2
    MOVi32 def %52:gpr32, 2
    lsl def %53:gpr32, %51:gpr32, 2
    sub def %54:gpr32, %29:gpr32, %53:gpr32
    add def %70:gpr64, %1:gpr64, 0
    add def %55:gpr64, %70:gpr64, %54:gpr32, 2, 1
    MOVi32 def %56:gpr32, 0
    ldr def %57:gpr32, %55:gpr64, 0
    madd def %58:gpr32, %57:gpr32, %5:gpr32, %30:gpr32
    MOVi32 def %59:gpr32, 0
    cmp %58:gpr32, 0, def implicit $nzcv
    cset def %60:gpr32, cc(11), implicit $nzcv
    MOVi32 def %61:gpr32, 0
    sub def %62:gpr32, %61:gpr32, %58:gpr32
    cmp %60:gpr32, 0, def implicit $nzcv
    csel def %63:gpr32, %62:gpr32, %58:gpr32, cc(1), implicit $nzcv
    COPY def $w0, %63:gpr32
    bl &putint, <regmask>
    MOVi32 def %64:gpr32, 1
    add def %65:gpr32, %29:gpr32, 1
    b %bb.1
}
```

全局 `@scale` 经 `ADRP`+`LDRWlo` 加载；`alloca` 对应 `LEA_FRAME`。

## 3 Machine Pass

### Pipeline

```
ISel
  ↓ [O≥1, !disablePeephole]
  PreRAMachinePeephole → PreRAAddressingFolder
  ↓ [O≥1]
  AArch64ConditionOptimizer → DeadMachineInstructionElimination (≤4轮)
  ↓ [所有优化级别]
  PhiElimination
  ↓ [O≥1, !disablePeephole]
  PreRAMachinePeephole → MachineLICM → PreRAMachinePeephole → PreRACFGOptimizer
  ↓ [O≥1, !disablePreSchedule]
  A53MachineScheduler (pre-RA)
  ↓
  GraphColoringRegisterAllocator
  ↓
  PostRAParallelCopyResolver → PostRAInstructionExpansion::run()
  ↓ [O≥1, !disablePeephole]
  PostRACopyPropagation (第1次)
  ↓
  AArch64FrameLowering
  ↓ [O≥1]
  PostRACopyPropagation (第2次) → PostRAAddressingOptimizer → MachineBlockPlacement
  ↓ [All]
  PostRAInstructionExpansion::expandConstantMaterializations()
  ↓ [O≥1, !disableSchedule]
  A53MachineScheduler (post-RA, final)
  ↓
  AArch64AssemblyPrinter
```

| Pass | 阶段 | 前提条件 |
|------|------|----------|
| PreRAMachinePeephole | Pre-RA | 虚拟寄存器，可有 PHI（部分模式要求无 PHI） |
| PreRAAddressingFolder | Pre-RA | `IsSSA` + `Selected`，无物理寄存器 |
| AArch64ConditionOptimizer | Pre-RA | `IsSSA` |
| DeadMachineInstructionElimination | Pre-RA | `IsSSA` |
| MachineLICM | Pre-RA | 无硬性属性检查（PHI 消除后运行） |
| PreRACFGOptimizer | Pre-RA | 非 `NoVRegs` |
| PhiElimination | Pre-RA→Post-SSA | 可有 `HasPHIs` |
| PostRAParallelCopyResolver | Post-RA | `NoVRegs` |
| PostRAInstructionExpansion | Post-RA | `NoVRegs` |
| PostRACopyPropagation | Post-RA | `NoVRegs` |
| PostRAAddressingOptimizer | Post-RA | `NoVRegs` |
| MachineBlockPlacement | Post-RA | 无硬性属性检查 |

### 各 Pass 详解

#### 1. PreRAMachinePeephole

**运行时机**：PHI 消除前 1 次；PHI 消除后 2 次（LICM 前后各 1 次）。  
**前提**：虚拟寄存器；代码下沉要求 `!HasPHIs`。  
**目的**：性能优化（减少指令、改善寻址、CSE、寄存器类选择）。

##### 模式 1：常量 CSE（块内局部）

**匹配**：
```
MOVi32/MOVi64/MOVIv4Zero/MOVIv4s/MOVIv4sMsl/MVNIv4s/MOVIv16b/FMOVv4s  → %v
```
同一块内、call 之后清空表；以 `(opcode[:imm...])` 为 key。

**重写**：
- 首次出现：记入 `available` 表
- 重复：将所有 `%duplicate` 使用替换为 `%canonical`，删除重复指令，擦除虚拟寄存器

**原因**：RA 前共享常量，让图着色决定寄存器/溢出；call 后清空避免跨调用活跃范围。

##### 模式 2：GPR → NEON 立即数广播

**匹配 A（整数）**：
```
%scalar = MOVi32 #imm
%vec    = DUPv4i32 %scalar
```
条件：`%scalar` 仅被 `DUPv4i32` 使用；`classifyNeonSplatImmediate(imm)` 成功。

**匹配 B（浮点）**：
```
%bits  = MOVi32 #imm
%scalar = FMOVSW %bits
%vec    = DUPv4f32 %scalar
```
条件：链路仅用于广播；`classifyNeonSplatImmediate(bits)` 成功。

**重写**：`DUP` → `makeNeonSplatImmediate(...)`，可能为：
- `MOVIv4Zero`
- `MOVIv16b #byte`
- `MOVIv4s #imm8, shift`
- `MOVIv4sMsl #imm8, msl`
- `MVNIv4s #imm8, shift`
- `FMOVv4s #bits`

**原因**：标量 MOVi 无其他用户时，NEON 立即数更高效；死标量留给 DCE。

##### 模式 3：零立即数恒等变换

**匹配**：
```
ADDWri/SUBWri/ADDXri/SUBXri %dst, %src, #0
```

**重写**：
```
COPY %dst, %src
```

##### 模式 4：自拷贝消除（物理寄存器）

**匹配**：
```
COPY %phys, %phys   (同一物理寄存器)
```

**重写**：删除指令。

##### 模式 5：LDR/STR → LDP/STP 配对（Pre-RA，虚拟地址）

**支持的配对种类**：

| load | store | pair load | pair store | stride |
|------|-------|-----------|------------|--------|
| LDRWui | STRWui | LDPWi | STPWi | 4 |
| LDRSui | STRSui | LDPSi | STPSi | 4 |
| LDRXui | STRXui | LDPXi | STPXi | 8 |
| LDRQui | STRQui | LDPQi | STPQi | 16 |

**地址追踪**：沿 `COPY`（GPR64）和 `ADDXri #imm` 链解析 `(root, offset)`。

**配对条件**：
- 同 `PairKind`、同 `root`、偏移差 = stride
- `lowerOffset % stride == 0`，`lowerOffset/stride ∈ [-64, 63]`
- 非 volatile
- 两指令间无 call、无 store（load 时）、无额外 load（store 时）、不重新定义 `root`（store 时还检查 early data）
- Load：`Rt != Rt2`

**重写**：
```
LDP/STP %rt_low, %rt_high, [%root, #lowerOffset]
```
- Load：在较早指令处替换，删除较晚的
- Store：在较晚指令处替换，删除较早的

**原因**：减少内存指令数；load 在早处合并使两值同时可用，store 在晚处保证数据已定义。

##### 模式 6：代码下沉（仅 `!HasPHIs`）

**可下沉 opcode**：
`MOVi32, MOVi64, MOVIv4Zero, FMOVWS, FMOVSW, COPYXtoW, SXTW, UXTW`

**匹配**：
- 单 def 虚拟寄存器、单 use
- use 在唯一后继块、该后继仅有此一个前驱
- 无副作用、非 terminator/call/load/store

**重写**：将指令从源块 splice 到后继块 use 之前。

**原因**：缩短活跃区间；限制单前驱边保证可用性。

#### 2. PreRAAddressingFolder

**运行时机**：第一次 PreRAMachinePeephole 之后（PHI 消除前）。  
**前提**：`IsSSA` + `Selected`，非 `NoVRegs`。  
**目的**：性能——将地址算术折叠进 LDR/STR。

##### 地址解析（递归 `resolve`）

| 定义形式 | 解析结果 |
|----------|----------|
| 基址 vreg | `Immediate{base=vreg, offset=0}` |
| `COPY %a, %b` | 跟随 `%b`；Index 形式要求 COPY 单消费者 |
| `ADDXri %addr, %base, #imm` | 合并 offset（溢出检查） |
| `ADDXrs %addr, %base, %idx, #shift, #ext` | `Index{base, index, shift, ext}`；offset=0；单消费者；ext∈{0,1}→GPR32，ext=2→GPR64 |

##### 模式 A：scaled-immediate 折叠

**匹配**：
```
%addr = ... (Immediate 形式, offset≠0 或 base≠address)
LDR/STR*ui %val, [%addr, #memOffset]
```

**条件**：`form.offset + memOffset` 不溢出且 `scaledImmediateEncodable(folded, width)`

**重写**：
```
LDR/STR*ui %val, [%base, #folded]
```

##### 模式 B：寄存器偏移寻址

**匹配**：
```
%addr = ADDXrs ... (Index 形式)
LDR/STR*ui %val, [%addr, #0]
```

**条件**：`memOffset==0`；shift 为 0 或 `registerOffsetShift(width)`

**重写**：
```
LDR/STR*ro %val, [%base, %index, #shift, #extension]
```

**原因**：消除独立地址计算虚拟寄存器；COPY 路径 Index 折叠要求单消费者避免延长两条 live range。

#### 3. AArch64ConditionOptimizer

**运行时机**：PreRAAddressingFolder 之后。  
**前提**：`IsSSA`。  
**目的**：性能——标志复用，消除冗余比较/布尔化。

##### 模式 1：CSET + CMP(0) + CSEL 折叠（标志复用）

**匹配序列**：
```
%p = CSETW cc, NZCV          ; %p 仅 1 次使用
... (不 clobber NZCV 的指令) ...
NZCV = CMPWri %p, #0
%r = CSELW/CSELX/FCSELS %t, %f, {EQ|NE}, NZCV
```

**重写**：
- 删除 `CSETW` 和 `CMPWri`
- `CSEL` 条件码改为：
  - CSEL 用 NE → 原 `cc`
  - CSEL 用 EQ → `inverse(cc)`
- 擦除 `%p` 虚拟寄存器

**原因**：`%p` 仅驱动 CSEL 时，直接复用原始比较标志，省去布尔化和二次比较。

##### 模式 2：CSET + CBZ/CBNZ → Bcc

**匹配序列**：
```
%p = CSETW cc, NZCV          ; %p 仅 1 次使用
... (不 clobber NZCV) ...
CBZ/CBNZ %p, target
```

**重写**：
```
Bcc adjusted_cc, target, NZCV
```
- CBZ → `inverse(cc)`；CBNZ → `cc`
- 删除 `CSETW`

**原因**：CBZ/CBNZ 测试的是整数零，CSET 已编码比较结果；转为 Bcc 直接用 NZCV。

#### 4. DeadMachineInstructionElimination

**运行时机**：ConditionOptimizer 之后，最多 4 轮迭代。  
**前提**：`IsSSA`。  
**目的**：正确性清理 + 性能（消除死代码）。

**算法**：use-def 反向工作表
1. 统计所有 vreg 使用次数和定义指令
2. use 数为 0 的 def 入队
3. 若指令 `removableInstruction` 且所有 def 均已死 → 标记死指令，递减 use 的 vreg 计数，继续传播

**删除**：擦除死指令及其 vreg 定义；清除 `TracksLiveness`。

**`removableInstruction` 排除**：terminator、call、load、store、副作用、含物理寄存器。

#### 5. PhiElimination（`regalloc.cpp`）

**运行时机**：所有优化级别，Pre-RA 调度前。  
**前提**：可有 `HasPHIs`、Machine SSA。  
**目的**：正确性——将 PHI 转为 COPY，为 RA 准备非 SSA IR。

##### 阶段 1：关键边分裂

**条件**：后继块首部有 PHI，且某前驱有 >1 个后继（关键边）。

**操作**：
1. 创建 `phi.edge.N` 块
2. 前驱 → split → 后继
3. 更新前驱分支目标
4. split 末尾插入 `B successor`
5. PHI incoming 边从前驱改为 split

**原因**：并行 COPY 必须插入到特定前驱边上。

##### 阶段 2：PHI → 并行 COPY

**匹配**：块首部连续 `PHI %dst, %src1, bb1, %src2, bb2, ...`

**操作**：
- 对每个 incoming `(src, pred_block)` 记录 `Copy{dst, src, regClass}`
- 删除 PHI 指令

##### 阶段 3：并行 COPY 解析（含环打破）

在每个前驱块 terminator 前插入 COPY，贪心调度：

1. 找 `dst == src` 或 `dst` 不在剩余 sources 中的 ready copy → 发射 `COPY dst, src`
2. 若无 ready（环）：
   - 取 `pending.front()` 为 `cycle`
   - 创建临时 vreg `temporary`
   - 插入 `COPY temporary, cycle.source`
   - 将所有 `source == cycle.source` 的 pending copy 的 source 改为 `temporary`
   - 继续循环

**属性变更**：清除 `IsSSA`、`HasPHIs`、`TracksLiveness`。

#### 6. MachineLICM

**运行时机**：PHI 消除后第一次 Peephole 之后。  
**前提**：需有规范 preheader（PHI 关键边分裂提供）。  
**目的**：性能——循环不变常量外提。

##### 算法

1. **支配者计算**：迭代数据流
2. **自然循环识别**：back-edge `tail → header`（header 支配 tail）
3. **Preheader**：header 有唯一外部前驱且该前驱仅指向 header
4. 按循环块数升序处理

##### 可外提 opcode

仅 `MOVi32`、`MOVi64`（**不**外提 NEON 立即数）。

##### 外提条件

- 指令所有 use 的 def 不在循环内（或无非 def 操作数）
- 不含物理寄存器
- 若当前块是嵌套循环 preheader 且非当前 header → 跳过（避免反复外提）

**操作**：splice 到 preheader terminator 前。

#### 7. PreRACFGOptimizer

**运行时机**：PHI 消除后 LICM 和 Peephole 之后。  
**前提**：非 `NoVRegs`。  
**目的**：性能——分支/测试指令优化、循环批处理。

##### 模式 1：AND + CMP(0) + Bcc → TBZ/TBNZ/TST

**匹配**：
```
%t = ANDWrr/ANDWri %lhs, %mask
CMPWri %t, #0
Bcc {EQ|NE}, target
```

**条件**：
- `%t` 单 use
- 仅 EQ/NE
- `!flagsUsedAfter(block, after_branch)`
- source 为虚拟寄存器

**子模式 A — 单位掩码**（`mask` 为 2 的幂）：
```
TBZ/TBNZ source, #bit, target    ; EQ→TBZ, NE→TBNZ
```
删除 AND 和 CMP。

**子模式 B — 可编码立即数掩码**：
```
TSTWri source, #mask, NZCV
```
删除 AND；CMP 替换为 TST。

**子模式 C — 寄存器掩码**：
```
TSTWrr source, mask, NZCV
```

**额外**：若 mask 来自 `MOVi32` 且单 use → 删除 mask 物化指令。

##### 模式 2：CMP(0) + Bcc → CBZ/CBNZ

**匹配**：
```
CMPWri %v, #0
Bcc {EQ|NE}, target
```

**条件**：EQ/NE；`!flagsUsedAfter`；`%v` 为虚拟寄存器

**重写**：
```
CBZ/CBNZ %v, target    ; EQ→CBZ, NE→CBNZ
```
删除 CMP。

##### 模式 3：精确减半循环批处理（CTZ 加速）

**匹配 CFG 结构**：

**parity 块**（2 条指令）：
```
TBZ %state, #0, even
B odd_path
```

**even 块**（3 条）：
```
ASRWri %shifted, %state, #1
COPY %state', %shifted
B latch
```

**latch 块**（4 条）：
```
ADDWri %count', %count, #1
CMPWri %state', #odd_sentinel    ; sentinel 为奇数
Bcc EQ, exit
B continuation
```

**continuation 块**（3 条）：
```
COPY ... (复制 count 和 state)
B parity
```

**可选 odd 路径**：fallthrough 到计算 `3*state+1` 的块，经 update 块回到 latch。

**重写**（even 块）：
1. 插入 `RBITW %rev, %state`
2. 插入 `CLZW %amt, %rev`
3. `ASRWri #1` → `ASRWrr %shifted, %state, %amt`
4. 删除 `B latch`
5. 追加 `ADDWrr %count', %count, %amt`、原 compare/exit/continue 指令
6. even 后继改为 exit 和 continuation（移除 latch）

**odd 路径**（若存在）：类似插入 RBITW/CLZW，COPY 改 ASRWrr，追加 ADDWrr+ADDWri，重定向边。

**原因**：连续偶数迭代可用 CTZ 一次完成多次右移和计数增量。

#### 8. PostRAParallelCopyResolver（`regalloc.cpp`）

**运行时机**：RA 之后立即。  
**前提**：`NoVRegs`（RA 完成）。  
**目的**：正确性——将带 `parallelCopyGroup` 标记的并行 COPY 序列化为安全顺序。

##### 来源

ISel 在函数入口参数和 call 返回值处生成带 `parallelCopyGroup` 的虚拟 COPY；RA 着色后变为物理 COPY。

##### 算法

1. 收集同 `parallelCopyGroup` 的连续 `COPY phys_dst, phys_src`
2. 删除原指令组
3. 贪心调度（同 PhiElimination 逻辑）：
   - ready：`dst == src` 或 `dst ∉ sources` → 发射 COPY
   - **无环打破**：若无法找到 ready → `throw logic_error`（RA 的 forbiddenColors 预着色应保证无环）

**与 PhiElimination 的区别**：物理并行 COPY **不**插入临时寄存器打破环；依赖 RA 阶段 ABI 约束保证可调度。

#### 9. PostRAInstructionExpansion

**运行时机**：RA 后 `run()`；frame lowering 后 `expandConstantMaterializations()`。  
**前提**：`NoVRegs`。

##### `run()`：向量 insert/accumulate 绑定

**匹配**：
```
INSv4i32/INSv4f32 %vd, %vn, ...
MLAv4i32/MLSv4i32/FMLAv4f32/FMLSv4f32 %vd, %va, %vb, ...
```
且 `%vd`（def）≠ `%vn`/`%va`（source）

**重写**：
1. 若 def ≠ source：前插 `COPY %vd, %source`
2. 将 source 操作数改为 tied：`physReg(vd); tiedTo=0`

**原因**：AArch64 向量 accumulate/insert 要求 dest=source（in-place）。

##### `expandConstantMaterializations()`：MOVi → MOVZ/MOVK

**匹配**：`MOVi32`（GPR32）或 `MOVi64`（GPR64）

**算法**（`expandIntegerImmediate`）：
1. 找第一个非零 16-bit slice `first`
2. 全零 → `MOVZ reg, #0, lsl #0`
3. 否则：
   - `MOVZ reg, #piece[first], lsl #(first*16)`
   - 对每个其他非零 slice `i`：`MOVK reg, #piece[i], lsl #(i*16)`（tiedTo=0, isKill=true）

**原因**：拆分后 post-RA 调度器可交错独立常量物化。

#### 10. PostRACopyPropagation

**运行时机**：RA 后 1 次；frame lowering 后 1 次（O≥1）。  
**前提**：`NoVRegs`。  
**目的**：性能——消除冗余 COPY、转发物理寄存器。

##### 阶段 1：算术+COPY 融合

**匹配**：
```
ADDWri/SUBWri/LSLWri/LSRWri/ASRWri/
ADDXri/SUBXri/LSLXri/ASRXri  %tmp, %in, ...
COPY %in_alias, %tmp          (距离 ≤5，无 call/terminator 间隔)
```
且 `%tmp` 与 `%in` 无别名。

**重写**：算术 def 直接写 `%in`；删除 COPY。

##### 阶段 2：自 COPY 消除

同 Peephole 模式 4（物理寄存器）。

##### 阶段 3：物理寄存器活跃性分析

- 收集每块 use/def（含 call 隐式 use X0–X7/V0–V7、RET 用 X0/V0/X30）
- call  clobber 所有 caller-saved
- 反向迭代计算 `physicalLiveIn` / `physicalLiveOut`

##### 阶段 4：COPY 前向传播（值被杀死）

**匹配**：
```
COPY %dst, %src
... 后续指令使用 %dst 但不 live-out ...
```

**条件**：
- 遇 call：dst 为参数寄存器 → blocked；caller-saved dst → killed
- 遇 terminator：terminator 使用 dst → blocked；否则若 dst 不 live-out → killed
- 扫描中：def dst → killed；def src → sourceAvailable=false
- tied 操作数 / 寄存器类不匹配 → blocked

**重写**：将 `%dst` 使用替换为 `%src`；删除 COPY。

##### 阶段 5：COPY 前向传播（至重新定义）

**匹配**：
```
COPY %dst, %src
... 使用 %dst ...
%dst = ... (重新定义)
```

**条件**：无 call/terminator/副作用；src 在 dst 重定义前不被重新定义

**重写**：中间 `%dst` 使用 → `%src`；删除 COPY。

#### 11. PostRAAddressingOptimizer

**运行时机**：frame lowering 之后（O≥1）。  
**前提**：`NoVRegs`。  
**目的**：性能——后变址寻址。

##### 模式：LDR/STR ui [#0] + ADDXri → LDR/STR post

**匹配**：
```
LDR/STR{W,S,X,Q}ui %val, [%base, #0]
ADDXri %base, %base, #inc     ; inc ∈ [-256, 255]
```
中间无 call/terminator；`%base` 非 SP/X29；ADD 前某指令首次提及 `%base`。

**支持的 post 形式**：

| ui | post |
|----|------|
| LDRWui | LDRWpost |
| STRWui | STRWpost |
| LDRSui | LDRSpost |
| STRSui | STRSpost |
| LDRXui | LDRXpost |
| STRXui | STRXpost |
| LDRQui | LDRQpost |
| STRQui | STRQpost |

**重写**：
```
LDR/STR*post %val, [%base, #inc]!    ; base 为 early-clobber def
```
删除 ADDXri。

**原因**：合并 load/store 与指针更新为单条后变址指令。

#### 12. MachineBlockPlacement

**运行时机**：PostRAAddressingOptimizer 之后（O≥1）。  
**目的**：性能——基本块布局、fallthrough 优化、循环旋转。

##### 阶段 1：转发块消除（threading）

**匹配**：仅含 `B target` 的单指令块（非自环）。

**操作**：
- 所有前驱边重定向到 `target`
- 删除转发块

##### 阶段 2：链式布局（`extendChain`）

从 entry 及未放置块开始，贪心延伸：

**后继评分**（未放置的后继）：
- 唯一前驱：+100
- 循环深度不同：最深循环后继 +300
- 深度相同：
  - `scoreForwardTrace`：向前追踪最深 loopDepth 和加权长度
  - `likelySuccessor`（条件分支 EQ/CBZ 的 unlikely 边启发）：+10000
  - `preferredFallthrough`（Bcc+CBZ/CBNZ+B 结构的 fallthrough）：+1
  - 块号更大：+1

**ForwardTraceScore**：DFS 带 memo，限 64 块；`weightedLength = 1 + 4*min(loopDepth,4) + child`

##### 阶段 3：循环 latch 旋转

**条件**：
- latch 有 2 个后继，loopDepth > 0
- 一个后继 `header` 满足 `header.loopDepth > 0 && header.number < latch.number`
- 所有被位移边的块有显式 terminator

**操作**：将 latch 移到 header 之前（在 order 中）。

**原因**：使热回边 fallthrough，匹配 LLVM MachineBlockPlacement 循环旋转。

##### 阶段 4：分支清理

对每个块及其物理后继 `fallthrough`：

**A. 冗余无条件跳转**：
```
B fallthrough
```
→ 删除

**B. 条件+无条件跳转反转**：
```
Bcc/CBZ/CBNZ cond_target
B fallback
```
若 `cond_target == fallthrough`：
- Bcc：反转条件码，目标改 fallback
- CBZ↔CBNZ 互换，目标改 fallback
- 删除 B fallback

**原因**：让热路径 fallthrough，减少一条跳转指令。

### 应用示例

条件优化后，循环头由 `cset`+`cbnz` 收成 `b.cond`（`PHI` 仍在）：

```text
  bb.1 while.cond: successors %bb.3 %bb.2
    PHI def %28:gpr32, %7:gpr32, %bb.0, %63:gpr32, %bb.3
    PHI def %29:gpr32, %2:gpr32, %bb.0, %65:gpr32, %bb.3
    PHI def %30:gpr32, %2:gpr32, %bb.0, %63:gpr32, %bb.3
    cmp %29:gpr32, %8:gpr32, def implicit $nzcv
    b.cond cc(11), %bb.3, implicit $nzcv
    b %bb.2
```

PHI 消除后，入口与回边改为 COPY：

```text
    COPY def %28:gpr32, %7:gpr32
    COPY def %29:gpr32, %2:gpr32
    COPY def %30:gpr32, %2:gpr32
    MOVi32 def %56:gpr32, 0
    b %bb.1
  bb.3 while.body: successors %bb.1
    asr def %51:gpr32, %29:gpr32, 2
    lsl def %53:gpr32, %51:gpr32, 2
    sub def %54:gpr32, %29:gpr32, %53:gpr32
    ldr def %57:gpr32, %1:gpr64, %54:gpr32, 2, 1
    madd def %58:gpr32, %57:gpr32, %5:gpr32, %30:gpr32
    cmp %58:gpr32, 0, def implicit $nzcv
    sub def %62:gpr32, %56:gpr32, %58:gpr32
    csel def %63:gpr32, %62:gpr32, %58:gpr32, cc(11), implicit $nzcv
    COPY def $w0, %63:gpr32
    bl &putint, <regmask>
    add def %65:gpr32, %29:gpr32, 1
    COPY def %28:gpr32, %63:gpr32
    COPY def %29:gpr32, %65:gpr32
    COPY def %30:gpr32, %63:gpr32
    b %bb.1
}
```

## 4 寄存器分配

### 4.1 数据结构

**`LiveInterval`**：虚拟寄存器的活跃区间
| 字段 | 含义 |
|------|------|
| `reg` | 虚拟寄存器编号 |
| `regClass` | 寄存器类 (GPR32/64, FPR32, NEON128) |
| `start`, `end` | 半开区间 `[start, end)`，以 slot 为单位 |
| `weight` | 溢出代价权重 |
| `crossesCall` | 是否跨越函数调用仍存活 |

**`LivenessResult`**：`intervals` + 每基本块 `blockLiveOut` 集合。

### 4.2 `MachineLiveness` — 活跃性分析（精确算法）

#### 4.2.1 循环深度 (`computeMachineLoopDepths`)

1. 从入口 BFS 标记可达块。
2. 迭代数据流求支配集：入口 `{self}`；不可达块 `{self}`；其余初始为全体可达块；对每个块取所有前驱支配集交集，再加入自身。
3. 自然循环检测：对边 `tail → header`，若 `header` 支配 `tail`，则以 `header` 为循环头，反向沿前驱收集循环体（不含 header 本身）。
4. 循环体内所有块 `loopDepth++`（嵌套循环会累加）。

#### 4.2.2 Slot 编号

- 全局 `slot` 从 **2** 开始。
- 每块：`blockStart[block] = slot`。
- 每条指令：`instruction.slotIndex = slot`，然后 `slot += 2`。
- 块结束：`blockEnd[block] = max(blockStart+1, slot)`，再 `slot += 2`。
- 步长为 2 是为 PHI 入边预留间隔。

#### 4.2.3 块级 use/def 与 liveIn/liveOut

对每个块：
- **use**：在 def 之前出现的 vreg。
- **def**：作为 def 操作数的 vreg。

反向迭代所有块，标准 live 方程：
```
liveOut[B] = ⋃ liveIn[S]  (S ∈ succ(B))
liveIn[B]  = use[B] ∪ (liveOut[B] − def[B])
```
直到不动点。

#### 4.2.4 `crossesCall` 检测

每块从 `liveOut` 出发反向扫描：
- 遇到 `isCall()` 指令时，将当前 `live` 集合中所有 vreg 记入 `liveAcrossCalls`。
- 标准 kill/gen：`live -= defs`，`live += uses`。

#### 4.2.5 区间构建与权重

对每个 vreg 维护 `MutableInterval {start, end, weight}`：
- 每条指令的操作数：更新 `start = min`，`end = max(slot+1)`。
- **use**（非 def）操作数：`weight += blockWeight`，其中 `blockWeight = 10^min(loopDepth, 4)`。
- `liveIn` 中的 vreg：扩展 `start` 至 `blockStart`。
- `liveOut` 中的 vreg：扩展 `end` 至 `blockEnd`。
- 最终 `weight = max(1.0, weight)`。

设置 `MachineProperty::TracksLiveness`。

### 4.3 `PhiElimination` — PHI 消除

#### 阶段一：关键边分裂

对每个含 PHI 前导指令的 successor 块：
- 若某 predecessor 有 **>1** 个后继，则分裂边 `pred → succ`：
  - 创建 `phi.edge.N` 块。
  - 重定向 `pred` 的分支目标。
  - 新块末尾插入 `B succ`。
  - 更新 PHI 操作数中的 BasicBlock 引用。

#### 阶段二：PHI → COPY

- 扫描每块头部连续 PHI，解析 `(def, [src₁,bb₁], [src₂,bb₂], ...)`。
- 删除 PHI，在对应 predecessor 块 terminator 前插入 `COPY def ← src`。
- 设置 `changed = true`。

#### 阶段三：并行 COPY 环消解

对每个 predecessor 的 pending COPY 列表：
1. 找 **ready** 拷贝：`dst == src` 或 `dst` 不在任何 pending 的 `source` 集合中。
2. 若无 ready（成环）：创建临时 vreg `t`，`COPY t ← cycle.source`，将所有以该 source 为源的 pending 改为 `t`。
3. 插入实际 `COPY` 指令。

完成后清除 `IsSSA`、`HasPHIs`、`TracksLiveness` 属性（若 changed）。

### 4.4 `GraphColoringRegisterAllocator` — Chaitin-Briggs 图着色分配器

#### 4.4.1 总体流程

```
最多 32 轮:
  liveness = MachineLiveness::run()
  if colorOnce() 成功:
    rewriteVirtualRegisters() → 设置 NoVRegs，返回
  else:
    insertSpills(spills) → 下一轮
超出 32 轮 → logic_error
```

#### 4.4.2 干扰图构建

**图存储**：无向邻接 **位矩阵**（`uint64_t` 数组），`graphIndex[vreg]` 映射到矩阵行/列。仅同寄存器 bank 的 vreg 可连边（GPR↔GPR，Vector↔Vector）。

**边来源（指令级扫描）**：

| 类型 | 规则 |
|------|------|
| 同指令多 def | 两两连边 |
| 同指令多 use | 两两连边 |
| tied 操作数 | tied use 与 tied def 视为同色；tied def 与同指令其他 use 连边 |
| early-clobber def | 与同指令非 tied use 连边 |
| COPY 亲和 | vreg-vreg COPY 累加双向 affinity 权重（**不**直接连干扰边） |
| 物理 hint | vreg=COPY phys 或 phys=COPY vreg 记录 `physicalHints` |

**活跃性干扰（反向块遍历）**：

维护 `live` 集合（从 `blockLiveOut` 初始化），对每条指令：
1. 对每个 def `d`、每个 `liveReg ∈ live`：连边 `(d, liveReg)`，**除非**该指令是 COPY 且 `d==copyDef && liveReg==copyUse`（COPY 两端不互相干扰）。
2. 对每个 physical def：向所有 live vreg 加入 `forbiddenColors[liveReg]`（COPY 时跳过 copyUse）。
3. 对每个 physical use：向 live vreg 和所有 use vreg 加入 forbidden（COPY 时跳过 copyDef）。
4. kill/gen 更新 live。

**`parallelCopyGroup` ABI 预着色**：

将同组 COPY 视为并行赋值，通过 `forbiddenColors` 保证顺序安全：
- 正向扫描：对 `phys ← vreg`，禁止 vreg 使用组内更早的 physical def；记录 `earlierPhysicalDefs`。
- 反向扫描：维护 `remainingPhysicalUses`，对每个 vreg def 禁止尚未消费的 physical source。

#### 4.4.3 着色 / 溢出 / 简化

**分 bank 独立着色**：先 GPR bank（`FPR32/NEON128` 为 false），再 Vector bank。

对每个 bank 的节点：

**初始化**：
- `degree` = 干扰图度数。
- `availableColorCount`：遍历 `RegisterInfo::allocationOrder(regClass)`，排除：
  - `isReserved`
  - `forbiddenColors`
  - `crossesCall && NEON128` → 跳过所有颜色
  - `crossesCall && isCallerSaved` → 跳过 caller-saved
- `degree < availableColorCount` 的节点入 `lowDegree` 集合。

**简化（Chaitin）**：
```
while remaining 非空:
  if lowDegree 非空: 取最小 vreg 编号
  else: 选溢出候选 — cost = weight/(degree+1)，最小者；
        spillTemporary 的 vreg cost = ∞（永不被选为溢出）
  从 remaining/lowDegree 移除，压入 simplifyStack
  更新邻居 degree；若 neighbor.degree < availableColorCount → 入 lowDegree
```

**选择（Briggs）**：弹出栈顶，按优先级选色：
1. **tied partner** 的颜色（fmla/fmls 等）。
2. **physicalHints**。
3. **affinity 权重最高**且已着色的邻居颜色。
4. `allocationOrder` 中第一个 allowed 颜色。

`allowed(phys)` 条件：
- 非 reserved、非 forbidden
- 非 `(crossesCall && NEON128)`
- 非 `(crossesCall && callerSaved)`

无法选色 → 加入 `spills`，返回 `false`。

#### 4.4.4 乐观重着色

着色成功后，对 affinity 边（按权重降序）最多 **4 轮**迭代：
- 若两端已同色、有干扰边 → 跳过。
- 尝试让一端 adopt 另一端颜色（需 `canRecolor`：新色不冲突邻居、满足 crossesCall 约束）。
- 仅当 `edge.weight >= strongestSatisfiedAffinity(该端)` 时才合并。

**注意**：简化阶段不做 Briggs 保守合并；合并仅在着色后乐观进行。

#### 4.4.5 Rematerialization

在 `insertSpills` 中，对溢出 vreg 检查其唯一 def：
- 必须是 `MOVi32` / `MOVi64` / `MOVIv4Zero`。
- def 的操作数 `[0]` 是该 vreg 且无其他 vreg use。
- → 标记为 `rematerializations`，**不分配栈槽**。

每个 use 点：在指令前插入该 MOV 的副本（目标为 spill temporary），并 `setDefinition`。

#### 4.4.6 溢出槽与 spill/reload 插入

**栈槽创建**：
| regClass | size | alignment |
|----------|------|-----------|
| NEON128 | 16 | 16 |
| GPR64 | 8 | 8 |
| 其他 | 4 | 4 |

`spill=true` 标记为 RA 溢出槽。

**每条使用溢出 vreg 的指令**：
1. 为每个涉及的溢出 vreg 创建 `spillTemporary`
2. **load 侧**（use）：remat → 插入 MOV；否则 `SPILL_LOAD temp, [frameIndex]` + memory operand。
3. 替换指令操作数为 temporary；def 时更新 `setDefinition`。
4. **store 侧**（def，非 remat）：指令后插 `SPILL_STORE temp, [frameIndex]`。

**清理**：删除已被 remat 替代的原始 def 指令。

#### 4.4.7 `rewriteVirtualRegisters`

将所有 vreg 操作数替换为已分配的 `PhysReg`（保留 isKill/isDead/tiedTo 等标志）。设置 `NoVRegs`，清除 `IsSSA/HasPHIs/TracksLiveness`。

#### 4.4.8 Caller/Callee Saved 交互

| 机制 | 行为 |
|------|------|
| `crossesCall` | 跨调用的 GPR 只能用 **callee-saved** (X19–X28)；跨调用的 NEON128 **无法着色**（必溢出） |
| `forbiddenColors` | 物理寄存器在特定程序点占用，禁止 vreg 使用该色 |
| `allocationOrder` | GPR 优先 X9–X17, X19–X28，最后 X0–X8；Vector 优先 V16–V31，其次 V8–V15，最后 V0–V7 |
| `isReserved` | SP, XZR, X18, X29, X30, NZCV 不可分配 |
| Callee-saved 保存 | 由 `frame_lowering` 在 RA 后根据实际使用的物理 callee-saved 寄存器决定 |

**Caller-saved 定义** (`target.cpp`)：
- X0–X17（除 X18）
- V0–V7, V16–V31

**Callee-saved**：
- X19–X28
- V8–V15

### 应用示例

`regalloc round 0, vregs=37` 一次着色成功。循环中 `%29`(i)、`%30`(acc)、`%5`(scale)、`%8`(n) 跨越 `bl putint` / `bl __sysy_parallel_for`，`crossesCall` 迫使其落入 callee-saved。最终 MIR 中可见 `w19`–`w25` 与序言保存：

```text
  bb.0 entry: successors %bb.1
    stp $x29, $x30, def $sp, -16
    mov def $x29, $sp
    sub def $sp, $sp, 80
    stp $x19, $x20, $sp, 0
    stp $x21, $x22, $sp, 16
    stp $x23, $x24, $sp, 32
    str $x25, $sp, 48
    add def $x23, $sp, 64
    movz def $w19, 0, 0
    adrp def $x9, @scale
    ldr def $w22, $x9, @scale
    adrp def $x9, @slot
    ldr def $w25, $x9, @slot
    bl &getint, <regmask>
    COPY def $w21, $w0
    bl &getint, <regmask>
    add def $w9, $w0, 1
    stp $w0, $w9, $x23, 0
    add def $w10, $w0, 2
    add def $w9, $w0, 3
    stp $w10, $w9, $x23, 8
    COPY def $w24, $w19
    movz def $w20, 0, 0
```

`LEA_FRAME` 槽在着色后仍以栈对象存在，待帧降低消解；无向量跨越调用，未触发「跨 call 的 NEON 强制溢出」路径。

## 5 指令调度

对函数每个基本块，在 **barrier 之间**的指令区域做局部调度。

### 5.1 调度屏障

以下指令不可被移动，且作为区域边界：
- terminator / call / pseudo / `hasSideEffects()`
- `parallelCopyGroup != 0`
- 含 volatile memory operand
- 大小 **3 ≤ N ≤ 512**，否则跳过。
- 区域内指令保持相对顺序的依赖约束。

### 5.2 依赖图

每个指令一个节点：
| 字段 | 来源 |
|------|------|
| `defs` / `uses` | 寄存器操作数集合（`registerKey`：phys 高位 bit63=1） |
| `latency` | `max(1, InstrDesc.latency)` |
| `resource` | `InstrDesc.resource` |
| `load` / `store` | `mayLoad()` / `mayStore()` |

**边规则**（仅 `i < j` 的原始顺序对）：
- **寄存器依赖**：`defs[i] ∩ uses[j]` 或 `uses[i] ∩ defs[j]` 或 `defs[i] ∩ defs[j]`（WAW）。
- **内存依赖**：双方任一为 load/store，且至少一方为 store（保守 alias）。

边方向 `i → j`（i 必须在 j 前），`j.predecessors++`。

### 5.3 资源模型

```cpp
enum class SchedResource { None, ALU, MAC, Divide, LoadStore, Branch, FPALU, FPMulDiv };
```

来自 `InstrDesc`（如 MUL→MAC latency=3，SDIV→Divide latency=12，LDR→LoadStore 等）。调度器用 `resource` 做 **软偏好**（换资源类型 +2 分），非硬端口限制。

### 5.4 高度与排序

反向计算 `height[i] = latency[i] + max(height[successors])`。

**列表调度**：
- 就绪集：predecessors==0 的节点。
- 评分（越高越优先）：
  ```
  score = height * 16
        + (load ? 8 : 0)
        + (resource != previousResource ? 2 : 0)
        - index / 64
  ```
- 同分取 **较小原始 index**（稳定 tie-break）。
- 选中后更新后继，记录 `previousResource`。

若结果与原始顺序相同 → 不修改。若拓扑排序失败（有环）→ 放弃。

### 5.5 Pre-RA vs Post-RA 差异

| 方面 | Pre-RA | Post-RA |
|------|--------|---------|
| 触发时机 | Phi 消除后、regalloc 前 (`disablePreSchedule`) | frame lowering + 常量展开后 (`disableSchedule`) |
| 寄存器 | vreg + 少量 phys hint | 纯 phys reg |
| 依赖精度 | vreg 级细粒度 | phys 级（WAW/WAR/RAW 更保守） |
| 副作用 | 改变指令顺序 → **清除 TracksLiveness** | 同上 |
| 指令形态 | 含伪指令、SPILL 伪指令 | 已展开 MOVZ/MOVK、真实 LDR/STR |

两次调度使用 **同一 `scheduleRegion` 实现**；差异来自 MIR 形态与寄存器命名空间。

### 应用示例

循环体在 `cmp`/`madd`/`csel`/`bl putint`/`add`/`b` 之间形成寄存器与调用屏障。调度器以 `bl`/`b.cond`/`ret` 等为 region 边界，只在屏障之间的 `asr/lsl/sub/ldr/madd/cmp/sub/csel` 一段做列表调度：`madd` 的 `SchedResource::MAC` 与相邻 `ALU` 换资源时获得软加分；`bl putint` 两侧指令不会被调过调用。

## 6. 帧降低

### 6.1 前提

`NoVRegs` 必须已设置；否则 `logic_error`。

扫描所有指令的 physical register 操作数，收集 `RegisterInfo::isCalleeSaved` 的寄存器，排序存入 `frame.savedRegisters`。

### 6.2 栈布局

```
cursor = maxCallFrameSize    // 底部预留传出参数区，SP 在 call 间不动
```

**Callee-saved 区**（每寄存器 8 字节，AAPCS64 仅保存 V8–V15 低 64 位）：
```
for reg in savedRegisters:
  align cursor to 8
  savedRegisterOffsets[reg] = cursor
  cursor += 8
```

**Fixed 对象**（传入栈参数）：`object.offset += 16`（相对 X29，FP/LR push 之后 caller 栈参起始为 X29+16）。

**普通对象布局**（两轮）：
1. `spill=true` 对象（RA 溢出槽）— 优先放在低地址、可直接 SP 编码的区域。
2. `spill=false` 对象（大数组等，通过 LEA_FRAME 寻址）。

```
stackSize = alignTo(cursor, 16)
usesFramePointer = stackSize≠0 || hasCalls || savedRegisters非空 || 存在 fixed 对象
```

### 6.3 `eliminateFrameIndices`

#### ADJCALLSTACKDOWN / ADJCALLSTACKUP
- 操作数为 immediate，**直接删除**（栈帧已在 prologue 预留 maxCallFrameSize）。

#### LEA_FRAME → ADDXri / MOVXrr 链
```
dst, frameIndex →
  base = fixed ? X29 : SP
  offset = object.offset
  首条: offset≤4095 → ADDXri dst, base, #offset 或 MOVXrr（offset=0）
  后续: 每次最多 +4095 的 ADDXri dst, dst, #amount
```

#### SPILL_LOAD / SPILL_STORE → 真实内存指令

| regClass | Load | Store |
|----------|------|-------|
| FPR32 | LDRSui | STRSui |
| NEON128 | LDRQui | STRQui |
| GPR64 | LDRXui | STRXui |
| GPR32 | LDRWui | STRWui |

操作数变为 `(value, base, #offset)`。

**大偏移处理**（`offset` 不满足 `0 ≤ off ≤ 4095*width` 无符号缩放编码）：
1. 计算 `adjustment = (absoluteOffset / 16) * 16`（16 字节对齐）。
2. 访问前：插入 `ADDSPri SP, SP, #amount`（每次 ≤4080）。
3. 执行 load/store（`base=SP`, `memoryOffset = absoluteOffset - adjustment`）。
4. 访问后：插入 `SUBSPri SP, SP, #amount` 恢复。
5. **不引入隐藏 scratch 寄存器**。

### 6.4 `insertPrologueEpilogues`

仅当 `usesFramePointer` 时插入。

#### Prologue（入口块头部，逆序插入故实际执行顺序如下）

```
1. STPXpre  X29, X30, [SP, #-16]!     // FP/LR 入栈
2. MOVXrr   X29, SP                   // 建立帧指针
3. SUB SP, SP, #amount                // 分配栈帧（每次 ≤4095，循环）
4. 保存 callee-saved:
   - 相邻同类型（GPR 或 Vector）、offset 连续且可 pair 编码 → STPXi/STPDi
   - 否则 → STRXui/STRDui（Vector 仅保存 8 字节）
```

#### Epilogue（每个 RET / TAILCALL 前，逆序恢复）

```
1. 恢复 callee-saved（LDPXi/LDPDi 或 LDRXui/LDRDui，顺序与保存相反）
2. ADD SP, SP, #amount                // 释放栈帧
3. LDPXpost X29, X30, [SP], #16       // 弹出 FP/LR
```

每条内存指令附带 `MachineMemOperand`（size/alignment/offset）。

### 应用示例

帧降低后：`usesFramePointer`；`sub sp` 为局部 `[4 x i32]`（对齐后槽）与 callee-saved 区让路；`LEA_FRAME %stack.0` 换成 `add x23, sp, #…`；`STPXpre`/`LDPXpost` 保存恢复 `x29,x30` 与 `x19`–`x25`。

```text
    stp $x29, $x30, def $sp, -16
    mov def $x29, $sp
    sub def $sp, $sp, 80
    stp $x19, $x20, $sp, 0
    stp $x21, $x22, $sp, 16
    stp $x23, $x24, $sp, 32
    str $x25, $sp, 48
    add def $x23, $sp, 64
    movz def $w19, 0, 0
    adrp def $x9, @scale
    ldr def $w22, $x9, @scale
    adrp def $x9, @slot
    ldr def $w25, $x9, @slot
    bl &getint, <regmask>
    COPY def $w21, $w0
    bl &getint, <regmask>
    add def $w9, $w0, 1
    stp $w0, $w9, $x23, 0
    add def $w10, $w0, 2
    add def $w9, $w0, 3
```

`ADJCALLSTACK*` 在 isel 阶段若出现，此处删除（传出区已计入 `maxCallFrameSize`）。

## 7. 汇编打印

### 7.1 函数级输出

```asm
.p2align 2
.global <name>
.type <name>, %function
<name>:
  [入口块无标签；其余块 .L<func>_bb<N>:]
  ...
.size <name>, .-<name>
```

**基本块标签**：`.L{functionName}_bb{blockNumber}`

### 7.2 操作数格式化

| 工具 | 规则 |
|------|------|
| `registerName` | 按 `RegClass` 视图输出 x/w/v/s 名 |
| `registerNameAs` | 强制指定视图（如 SMULL 用 W 操作数） |
| NEON128 内存/向量 | 前缀 `q` + 寄存器号 |
| D-register 内存 | 前缀 `d` |
| `vectorView` | `vN.4s` |
| 条件码 | `eq/ne/hs/lo/...`（15 种） |

### 7.3 内存寻址辅助

- **`emitMemory`**：`ldr/str Rt, [Xn, #offset]`，offset 须满足无符号缩放编码。
- **`emitGlobalMemory`**：`Rt, [Xn, :lo12:symbol]`。
- **`emitDMemory`**：D 寄存器 8 字节缩放。
- **`emitPostIndexedMemory`**：`Rt, [Xn], #imm`。
- **`emitRegisterOffsetMemory`**：`Rt, [Xn, Xm, uxtw/sxtw/lsl #shift]`，shift 仅 0 或 log2(width)。

### 7.4 每条 Opcode 打印规则

#### 数据移动
| Opcode | 汇编 |
|--------|------|
| COPY | NEON: `mov Vd.16b, Vn.16b`；FPR32: `fmov`；其他: `mov` |
| MOVZ/MOVK | `movz/movk Rd, #imm [, lsl #shift]` |
| MOVIv4Zero | `movi Vd.4s, #0` |
| MOVIv4s/Msl/Mvni | `movi/mvni Vd.4s, #imm [, lsl/msl #...]` |
| MOVIv16b | `movi Vd.16b, #imm` |
| FMOVv4s | `fmov Vd.4s, #float` |
| ADRP | `adrp Xd, symbol` |
| ADDlow | `add Xd, Xn, :lo12:symbol` |
| MOVXrr | `mov Xd, Xn` |
| COPYXtoW | 别名不同时 `mov Wd, Wn` |
| SXTW/UXTW | `sxtw/uxtw` |

#### 整数 ALU（W/X 变体）
| Opcode 族 | 汇编 |
|-----------|------|
| ADD/SUB rr/ri | `add/sub` |
| ADDWrs/ADDWrsX | `add ..., lsr #imm` |
| ADDWlsl | `add ..., lsl #imm` |
| NEGW/NEGX | `neg` |
| MUL/MADD/MSUB | `mul/madd/msub` |
| SDIV/UDIV | `sdiv/udiv` |
| SMULL/SMADDL/UMULH | 带 W/X 视图转换 |
| AND/ORR/EOR | `and/orr/eor` |
| LSL/LSR/ASR rr/ri | `lsl/lsr/asr` |
| ADDXrs | `add Xd, Xn, Xm, uxtw/sxtw/lsl #shift` |
| SUBSPri/ADDSPri | `sub/add SP, SP, #imm` |

#### 比较与条件
| Opcode | 汇编 |
|--------|------|
| CMPWrr/ri, CMPXrr/ri | `cmp` |
| TSTWrr/ri | `tst` |
| CSETW | `cset Wd, cond` |
| CNEGW | `cneg Wd, Wn, cond` |
| CSELW/X, FCSELS | `csel/fcsel` |
| CLZW/RBITW | `clz/rbit` |

#### 浮点标量
| Opcode | 汇编 |
|--------|------|
| FADDS/FSUBS/FMULS/FDIVS | `fadd/fsub/fmul/fdiv` |
| FNEGS | `fneg` |
| FCMPSrr/FCMPZS | `fcmp` |
| SCVTFWS/FCVTZSW | `scvtf/fcvtzs` |
| FMOVWS/FMOVSW | `fmov` |

#### 内存
| Opcode 族 | 汇编 |
|-----------|------|
| LDR/STR *ui | 无符号立即偏移 |
| LDR/STR *ro | 寄存器偏移 |
| LDR/STR *lo | 全局 `:lo12:` |
| LDRDui/STRDui | D 寄存器 |
| LDR/STR *post | 后索引 |
| STPXpre/LDPXpost | 前/后索引 pair |
| LDP/STP *i (Wi/Si/Xi/Di/Qi) | pair，offset 须 pair 可编码 |

#### 控制流
| Opcode | 汇编 |
|--------|------|
| B | `b .L<func>_bb<N>` |
| Bcc | `b.<cond> .L...` |
| CBZ/CBNZ | `cbz/cbnz Wn, .L...` |
| TBZ/TBNZ | `tbz/tbnz Wn, #bit, .L...` |
| CALL | `bl symbol` |
| TAILCALL | `b symbol` |
| RET | `ret` |

#### NEON 向量
| Opcode 族 | 汇编 |
|-----------|------|
| DUPv4i32/f32 | `dup Vd.4s, ...` |
| DUPv4sLane | `dup Vd.4s, Vn.s[lane]` |
| INSv4i32/f32 | `mov Vd.s[lane], ...` |
| EXTRACTv4i32/f32 | `umov/mov` |
| ADD/SUB/MUL/DIV/MIN/MAX v4 | `add/sub/mul/fmul/fdiv/smin/smax Vd.4s, ...` |
| NEGv4 | `neg/fneg` |
| SSHL/USHLv4 | `sshl/ushl` |
| SHLi/SSHRi/USHRi | `shl/sshr/ushr ..., #imm` |
| MLA/MLS/FMLA/FMLS | `mla/mls/fmla/fmls Vd.4s, Vn, Vm`（accumulator 为 op0） |
| SHUFFLEv16i8 | `tbl Vd.16b, {Vn.16b, Vm.16b}, Vk.16b` |
| ZIP/UZP/TRN v4s | `zip1/zip2/uzp1/uzp2/trn1/trn2` |
| EXTv16b | `ext Vd.16b, Vn.16b, Vm.16b, #imm` |
| REV64v4s | `rev64` |
| ADDVv4i32 | `addv` |
| AND/ORR/EOR v16i8 | `and/orr/eor Vd.16b, ...` |

#### 不可达（抛出 logic_error）
`PHI`, `MOVi32/64`, `LEA_FRAME`, `SPILL_*`, `FRAME_SETUP/DESTROY`, `ADJCALLSTACK*`, `IMPLICIT_DEF`, `Invalid`

### 7.5 CFI

**本模块不生成 `.cfi_*` 指令**。CFI 仅存在于 `parallelRuntime.hpp` 预编译 runtime 中。

### 7.6 向量常量池

若 `function.vectorConstantPool()` 非空：
```asm
.section .rodata
.p2align 4
<label>:
  .word 0x<lane0>
  .word 0x<lane1>
  ...
.text
```

### 应用示例

打印结果中与上述 MIR 对应的片段（乘加、条件选、调用、并行入口、全局）：

```asm
	asr w9, w24, #2
	lsl w9, w9, #2
	sub w9, w24, w9
	ldr w9, [x23, w9, sxtw #2]
	madd w10, w9, w22, w19
	cmp w10, #0
	sub w9, w20, w10
	csel w19, w9, w10, lt
	mov w0, w19
	bl putint
	add w24, w24, #1
	mov w25, w19
	b .Lmain_bb1
.Lmain_bb2:
	adrp x9, __sysy_par_ctx_0_0
	str w22, [x9, :lo12:__sysy_par_ctx_0_0]
	movz w1, #0
	adrp x9, __sysy_par_scalar_start_0
	str w1, [x9, :lo12:__sysy_par_scalar_start_0]
	adrp x9, __sysy_par_scalar_bound_0
	str w21, [x9, :lo12:__sysy_par_scalar_bound_0]
	adrp x9, __sysy_par_scalar_partial_0_0
	str w1, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	adrp x9, __sysy_par_scalar_partial_0_1
	str w1, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	mov w0, w1
	mov w2, w21
	bl __sysy_parallel_for
	adrp x9, __sysy_par_scalar_partial_0_0
	ldr w10, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	adrp x9, __sysy_par_scalar_partial_0_1
	ldr w9, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	add w9, w10, w9
	add w0, w19, w9
	adrp x9, scale
```

```asm
	bl __sysy_parallel_for
	adrp x9, __sysy_par_scalar_partial_0_0
	ldr w10, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	adrp x9, __sysy_par_scalar_partial_0_1
	ldr w9, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	add w9, w10, w9
	add w0, w19, w9
	adrp x9, scale
	str w22, [x9, :lo12:scale]
	adrp x9, slot
	str w25, [x9, :lo12:slot]
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #80
	ldp x29, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
```

模块含 `__sysy_parallel_for` 时，文本段末尾继续输出手写 `__sysy_par_dispatch`（按 body id 分支）以及并行 runtime 目标代码。

## 8. 并行 Runtime

### 8.1 `parallelRuntime.hpp`

内联常量 `kSysyParallelRuntimeAsm`：由 `.proto/par_runtime_only.c` 经 `gcc -O2 -fno-stack-protector -S` 生成的完整 AArch64 汇编，包含：

| 符号 | 功能 |
|------|------|
| `__sysy_bind_cpu.part.0` | 遍历 CPU 掩码，对匹配核心调用 `sched_setaffinity` |
| `__sysy_worker` | worker 线程：绑定 CPU 3，循环等待 job 序号，调用 `__sysy_par_dispatch` |
| `__sysy_parallel_for` | 主入口：范围过小则直接 dispatch；否则 `sched_getaffinity`、启动 worker（`clone`）、拆分任务 |
| `.bss` 全局状态 | `__sysy_orig_mask`(128B)、job 序号/边界、worker 状态、1MB 栈 `__sysy_wstack` |

含完整 `.cfi_*` 调试帧信息（仅 runtime，非用户函数）。

### 8.2 `main.cpp` 接入流程

```cpp
backend::aarch64::AArch64Backend codegen(m.get(), *out, backendOptions);
codegen.generate();   // 输出所有用户函数汇编

bool hasParallel = hasParallelForCall(m.get());  // 模块中是否调用了 __sysy_parallel_for
std::vector<int> bodyIds = parallelBodyIds(m.get());  // 收集 __sysy_par_body_<N> 函数名中的 N

if (hasParallel) {
    // 1. 手写 dispatch 跳板
    *out << "\n\t.text\n\t.align 2\n"
         << "\t.global __sysy_par_dispatch\n"
         << "__sysy_par_dispatch:\n";
    for (int id : bodyIds)
        *out << "\tcmp w0, #" << id << "\n"
             << "\tb.eq .Lsysy_disp_" << id << "\n";
    *out << "\tret\n";
    for (int id : bodyIds)
        *out << ".Lsysy_disp_" << id << ":\n"
             << "\tmov w0, w1\n"    // 参数重映射
             << "\tmov w1, w2\n"
             << "\tb __sysy_par_body_" << id << "\n";

    // 2. 追加预编译 runtime
    *out << kSysyParallelRuntimeAsm;
}
```

**`hasParallelForCall`**：遍历所有函数基本块指令，检查 call 目标是否为 `__sysy_parallel_for`。

**`parallelBodyIds`**：收集所有以 `__sysy_par_body_` 为前缀且后缀为纯数字的函数名，排序去重。

最终 `.s` 文件结构：用户代码 → `__sysy_par_dispatch` 跳板 → 并行 runtime 实现。

### 应用示例

第二段归约循环被拆成 `__sysy_parallel_for` + `__sysy_par_body_0`。后端先按普通函数降低 body，再在汇编末尾追加 dispatch 与 runtime。Dispatch 形态：

```asm
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
```

Runtime 提供 `__sysy_parallel_for` / `__sysy_worker` / 绑核与 `clone`；用户侧只保留对 `__sysy_parallel_for` 的 `bl` 与 body 符号。
