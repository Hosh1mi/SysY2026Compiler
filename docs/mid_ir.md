# Mid-Level IR 实现文档

`src/mid/ir` 定义了前端、中端优化和 AArch64 后端共享的 SSA IR。其文本语法接近
LLVM IR，但对象模型、类型集和指令语义以本项目实现为准。

本文档记录当前源码的对象关系、use-def/CFG 不变式、AST 降低方式和验证入口。

## 1. 目录与对象模型

- 实现位于 [`src/mid/ir`](../src/mid/ir)。
- 公共接口位于 [`src/include/mid/ir`](../src/include/mid/ir)。
- 聚合头文件 [`ir.hpp`](../src/include/mid/ir/ir.hpp) 引入主要 IR 类；
  [`irBuilder.hpp`](../src/include/mid/ir/irBuilder.hpp) 提供带插入点的构建接口。

顶层关系是：

```text
Module
├── GlobalVariable
└── Function
    ├── Argument
    └── BasicBlock
        └── Instruction
```

`GlobalVariable`、`Function`、`Argument`、`BasicBlock`、`Instruction` 和所有常量都继承
`Value`。`Value` 统一保存 `type_`、`name_`、`use_list_` 和持久化的语义标记，因此
pass 可以对不同种类的值使用相同的替换与查询接口。

| 组件 | 主要职责 |
| --- | --- |
| `Type` | 描述标量、数组、向量、指针和函数类型 |
| `Value` / `Use` | 表示 SSA 值并维护反向 use-list |
| `Instruction` | 保存 opcode、操作数、所属块和打印逻辑 |
| `BasicBlock` | 保存线性指令链表、CFG 边和分析附加信息 |
| `Function` | 保存参数和基本块；分析缓存由 AnalysisManager 管理 |
| `Module` | 保存全局量、函数和类型工厂 |
| `IRStmtBuilder` | 在当前基本块构造并插入指令 |
| `GenIR` | 以 Visitor 遍历 AST，生成未优化 IR |

实现文件与后文主题的对应关系如下：

| 实现文件 | 内容 |
| --- | --- |
| [`type.cpp`](../src/mid/ir/type.cpp)、[`constant.cpp`](../src/mid/ir/constant.cpp)、[`globalVariable.cpp`](../src/mid/ir/globalVariable.cpp) | 类型与常量、全局对象的文本表示 |
| [`value.cpp`](../src/mid/ir/value.cpp)、[`instruction.cpp`](../src/mid/ir/instruction.cpp) | use-def 维护与指令实现 |
| [`basicBlock.cpp`](../src/mid/ir/basicBlock.cpp)、[`function.cpp`](../src/mid/ir/function.cpp) | 指令链表、CFG 与命名 |
| [`module.cpp`](../src/mid/ir/module.cpp) | 模块输出与入口函数查询 |
| [`intrinsics.cpp`](../src/mid/ir/intrinsics.cpp) | signed min/max 与 `mulmod` 内建函数 |
| [`irGen.cpp`](../src/mid/ir/irGen.cpp) | AST 到基础 IR 的降低 |
| [`verify.cpp`](../src/mid/ir/verify.cpp) | IR 结构与 SSA 不变式验证 |

## 2. 类型、常量和语义标记

### 2.1 类型系统

[`type.hpp`](../src/include/mid/ir/type.hpp) 定义以下类型：

- `void` 和 `label`；
- `i1`、`i32` 和内部 `i64`；
- 32 位 `float`；
- 定长 `ArrayType` 与 `VectorType`；
- `PointerType` 和 `FunctionType`。

`Module` 直接持有常用基本类型，并按“元素类型 + 长度”缓存指针、数组和向量类型。
比较类型时因此通常可以使用指针恒等。

`i64` 是编译器内部类型，主要承载求和 runtime 中的宽位中间值。SysY 源语言
整数仍是 `i32`；前端不会因此把普通源级计算提升为 `i64`。对应的 `sext`/`trunc`
已加入指令集和后端 lowering。

### 2.2 常量

[`constant.hpp`](../src/include/mid/ir/constant.hpp) 包含：

- `ConstantInt`：用 `int64_t` 存放 `i1`/`i32`/`i64` 字面量；
- `ConstantFloat`：按 IR 所需的十六进制形式打印；
- `ConstantArray` 和 `ConstantVector`：递归持有元素；
- `ConstantZero`：表示标量或聚合类型的 `zeroinitializer`。

数组打印会检查嵌套常量是否全零，全零时收缩为 `zeroinitializer`，避免生成过长的文本
IR。

### 2.3 SemFlag

[`semFlags.hpp`](../src/include/mid/ir/semFlags.hpp) 把不适合临时重算的事实持久化在 `Value`
上。当前标记大致分为：

- 内存不变性：`ImmutableObject`、`ImmutableLoad`、`SrcConstArray`；
- 函数与参数效果：`FnPure`、`FnReadOnly`、`ArgReadOnly`、`ArgNoCapture`；
- 整数事实：`NoSignedWrap`、`NoUnsignedWrap`、`Exact`、`Disjoint`、
  `KnownNonNegative`；
- 变换协议：向量余数环、指针递推环、标量展开 scratch、wavefront 并行维等。

这些位会随 IR 文本打印。克隆指令或块时，变换需显式调用 `copySemFlagsFrom`
复制仍然有效的语义事实。

## 3. use-def 链与指令生命周期

### 3.1 双向关系

`Instruction::operands_[i]` 是 def-to-use 方向；每个操作数的 `Value::use_list_` 是
use-to-def 的反向索引。`Instruction::use_pos_[i]` 保存自己在对方 `use_list_` 中的迭代器，
使替换或删除一条 use 不需要扫描整条链。

必须通过以下接口修改关系：

- `set_operand(i, value)`：先删除旧 use，再注册新 use；
- `add_operand` / `PhiInst::addIncoming`：动态增加操作数；
- `remove_operands`：删除 PHI 的 `(value, predecessor)` 对并修正后续序号；
- `replace_all_use_with`：在所有 user 中替换当前值。

直接赋值 `operands_` 不会同步更新 use-list，会破坏 DCE、CSE 和 IR 验证所依赖的双向关系。

### 3.2 插入、移动和删除

`BasicBlock::instr_list_` 使用 `std::list`。每条已挂接指令的 `pos_in_bb` 保存在链表中的
位置，插入接口会同时设置 `parent_`。

- `delete_instr` 用于真正删除：解除操作数 use，移出链表，清空 `parent_`；
- `remove_instr` 只用于跨块移动：保留 use-def 关系，等待插入新块；
- no-insert 构造函数只创建对象，调用方必须使用 `add_instruction_*` 完成挂接。

`IRStmtBuilder::create_alloca` 是特例：无论当前插入点在哪里，都把 alloca 放到函数 entry
块的 alloca 段末尾。`Function::lastEntryAlloca_` 使连续分配为 O(1)，缓存失效时再
线性查找插入位置。

## 4. 指令集

[`instruction.hpp`](../src/include/mid/ir/instruction.hpp) 的 `OpID` 是中端和后端共享的指令分类。

| 类别 | 指令 |
| --- | --- |
| 整数算术 | `add/sub/mul/sdiv/srem/udiv/urem` |
| 浮点算术 | `fneg/fadd/fsub/fmul/fdiv` |
| 位运算 | `shl/lshr/ashr/and/or/xor/clz` |
| 内存 | `alloca/load/store/getelementptr` |
| 控制流 | `br/ret/phi/select/call` |
| 类型转换 | `zext/sext/trunc/fptosi/sitofp/bitcast` |
| 向量 | `insertelement/extractelement/shufflevector` 及向量化算术/比较 |

PHI 操作数始终交错保存为：

```text
[value0, predecessor0, value1, predecessor1, ...]
```

`GetElementPtrInst` 区分普通 GEP 和 split-suffix GEP。普通形式的第一个下标用于进入当前
聚合对象，suffix 形式的基址已经是 prefix GEP 结果，所以第一个下标就要继续消耗
数组层级。需要拆 GEP 时应使用 `create_split_suffix_gep`，否则返回指针类型可能错一层。

`Function::IntrinsicID` 目前标识 signed min/max 和宽位 `mulmod`。
[`intrinsics.cpp`](../src/mid/ir/intrinsics.cpp) 负责插入声明、识别 select 形式并在标量/向量类型间统一语义。

## 5. CFG、支配树和命名

### 5.1 CFG 两份表示

真实的控制流目标保存在基本块最后的 `BranchInst` 操作数中；`pre_bbs_` 和
`succ_bbs_` 是为分析和变换保留的反向/正向缓存。改分支时必须同时维护两者及目标块 PHI
的 incoming edge。

`Function::remove_bb` 会从函数列表和相邻块的 CFG 缓存中移除块。
批量 CFG pass 通常会从 terminator 重算边，不应在缓存未修复时查询 LoopInfo。

### 5.2 支配信息

支配信息不存放在 `Function` 或 `BasicBlock` 中。中端 pass 通过 `AnalysisManager` 请求
`DominatorTreeAnalysis`、`PostDominatorTreeAnalysis` 和 `DominanceFrontierAnalysis`，
并在修改 CFG 后用 `PreservedAnalyses` 触发统一失效。`BasicBlock` 只保留 CFG 边和
`live_in`/`live_out` 工作区。具体接口见 [`mid_analysis.md`](mid_analysis.md)。

### 5.3 可读 IR

`Function::set_instr_name` 在打印前为未命名的参数和有结果指令分配名称。已有语义名会保留，
冲突时加数字后缀；GEP、比较、算术和转换指令优先使用 `arrayidx`、`cmp`、
`add`、`zext` 等助记名，其余值按出现顺序使用数字名。

基本块名如 `entry`、`if.then`、`while.cond` 来自构建时的语义命名。同名块由 IRGen 自动加
后缀，这些名称仅用于输出和调试，不是稳定的结构标识。

## 6. AST 到基础 IR

[`irGen.cpp`](../src/mid/ir/irGen.cpp) 以 `GenIR` Visitor 遍历 SysY AST。`Scope` 使用 map 栈实现词法作用域，
每个声明映射到对应 `Value`。

基础 lowering 的主要约定是：

- 局部标量和数组先在 entry 中建 `alloca`，用 load/store 表达赋值，后续由 Mem2Reg 转 SSA；
- 全局常量初始化在编译期递归组装 `ConstantArray`，局部数组初始化生成 GEP/store；
- `if`/`while` 显式创建条件、body、汇合和退出块，并维护 CFG 边；
- `&&`/`||` 保留短路求值，不提前计算右侧表达式；
- 整数和浮点混合表达式在生成时插入 `sitofp`/`fptosi`；
- 非 void 函数初始使用 entry `%retval` 槽和统一返回块，随后立即折叠明显的
  store-load 转发和单前驱返回块。

`GenIR` 构造时预先加入 SysY IO 和计时库函数声明。`Module::print` 只输出被引用的外部
声明，未使用的内建函数不出现在最终 IR 中。

## 7. IR 验证与观察

调试命令：

```bash
build/compiler -O1 --dump-ir --verify-ir -c input.sy -o /tmp/out.ir
```

[`verify.cpp`](../src/mid/ir/verify.cpp) 不信任待验证的 CFG 缓存，而是从 terminator 独立推导边，并重新
计算支配关系。它在 `--verify-ir` 模式下于每个 pass 后检查：

1. 每个块非空、仅末尾存在 `br`/`ret`；
2. PHI 连续出现在块首，操作数成对且 incoming 块恰好等于前驱集；
3. 普通 use 的 def 支配 user，PHI use 的 def 支配对应入边末端；
4. terminator 推导的边与 `pre_bbs_`/`succ_bbs_` 对称一致；
5. 每个 operand 与 use-list 双向一致，已删除指令不再被使用；
6. 函数不留下不可达基本块。

验证失败会打印函数、基本块和 pass 上下文后 `abort`，因此新增 CFG/SSA 变换时应优先用
最小用例在该模式下定位第一个破坏不变式的 pass。
