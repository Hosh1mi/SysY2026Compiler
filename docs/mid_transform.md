# Mid-Level Transform 实现文档

`src/mid/transform` 放置改写 IR 形状的中端变换。当前分为局部指令合并、循环规范化和
循环级变换三部分。通用 CFG、过程间和清理 pass 位于 [`src/mid/opt`](../src/mid/opt)，分析实现
位于 [`src/mid/analysis`](../src/mid/analysis)。

本目录下的 pass 以 IR 结构和分析事实为匹配条件；未证明 legality 或 profitability 时
不改写 IR。

## 1. 目录边界

| 目录 | 职责 |
| --- | --- |
| [`instCombine`](../src/mid/transform/instCombine) | 工作表驱动的局部算术、比较、位运算和转换合并 |
| [`loop`](../src/mid/transform/loop) | 循环 SSA/CFG 变换、递推闭式、循环调度、并行和向量化 |
| [`loopCanonicalization.cpp`](../src/mid/transform/loopCanonicalization.cpp) | LoopSimplify 共享的 preheader/backedge/dedicated-exit 建立器 |

公共 pass 声明主要位于 [`src/include/mid/opt`](../src/include/mid/opt)。
`loopCanonicalization` 的独立接口位于
[`src/include/mid/transform`](../src/include/mid/transform)。头文件路径中的 `opt` 不表示实现文件也位于
`src/mid/opt`。

## 2. 在 O1 流水线中的位置

`main.cpp::buildArm64Pipeline` 中与本目录直接相关的顺序可概括为：

```text
LoopDistribution
  └── scalar/interprocedural/CFG cleanup
        └── repeat up to 8 rounds
              LoopSimplify → LCSSA → IndVarSimplify
              → SimpleLoopUnswitch → LoopRotate
              → InductiveRangeCheckElimination → LICM
              → cleanup → LCSSA → LoopDeletion
        └── LoopFixedPointEliminate
        └── recurrence / fusion / schedule transforms
        └── ParallelizeLoops → LoopVectorize → IVSR
        └── LoopRepFold → LoopModuloDelay → LoopUnroll
        └── LoopSimplify → LCSSA → LoopVectorize
        └── SLP / GEP / late cleanup
```

`InstCombine` 是 canonical cleanup 的一部分，因此会在整条流水线中多次运行。
`LoopMemoryScalarPromotion` 的实现虽在本目录，但调度在 late target-independent 阶段。

Pass 修改 IR 后通常返回 `PreservedAnalyses::none()`，让 `AnalysisManager` 清除 LoopInfo、SCEV、
LVI 和 alias 缓存。纯维持性 pass 可以不参与 repeat-group 的收敛判定，但这不改变
其分析失效语义。

## 3. InstCombine

### 3.1 工作表与终止性

[`instCombine.cpp`](../src/mid/transform/instCombine/instCombine.cpp) 先把函数中所有指令放入 worklist。一条指令被替换后，
其 users、operands 和新产生的 replacement 会重新入队，使局部等式可以连锁收敛。

实现同时限制处理次数和成功改写次数，防止可逆规则反复触发或超大函数产生过长的
重新入队链。超过 1024 条指令的函数不构建 RangeAnalysis，只使用不依赖该分析的局部规则。

如果某个操作数在改写后只剩跨块的唯一 user，且 user 所在块只有原块一个前驱，
`trySinkInstruction` 会把纯指令移到 user 前，减少分支两侧的无效活跃区间。

### 3.2 规则分组

[`instCombineInternal.hpp`](../src/mid/transform/instCombine/instCombineInternal.hpp) 定义各规则文件共享的匹配和分析辅助接口。

| 文件 | 主要规则 |
| --- | --- |
| [`instCombineAddSub.cpp`](../src/mid/transform/instCombine/instCombineAddSub.cpp) | 常量折叠、加减恒等式、重关联、负号消去、A53 友好乘法形式 |
| [`instCombineMulDivRem.cpp`](../src/mid/transform/instCombine/instCombineMulDivRem.cpp) | 乘除模化简、2 的幂、有界模递推、连续常量除法 |
| [`instCombineBitwise.cpp`](../src/mid/transform/instCombine/instCombineBitwise.cpp) | and/or/xor 恒等式、常量合并、布尔形式收缩 |
| [`instCombineShifts.cpp`](../src/mid/transform/instCombine/instCombineShifts.cpp) | 移位恒等式、嵌套移位、与乘除 2 的幂之间的安全转换 |
| [`instCombineCast.cpp`](../src/mid/transform/instCombine/instCombineCast.cpp) | 常量转换和明显可消去的 cast/PHI |
| [`instCombineCmpSelect.cpp`](../src/mid/transform/instCombine/instCombineCmpSelect.cpp) | 比较常量化、谓词归一化、select 折叠与 min/max 识别 |
| [`instCombineCompares.cpp`](../src/mid/transform/instCombine/instCombineCompares.cpp) | 比较谓词反转、边界和位模式辅助规则 |

所有规则都必须保留项目的 i32 wrap、有符号除法/取模和浮点语义。`NSW/NUW`、
`Exact` 和 `Disjoint` 只在 RangeAnalysis 或位事实证明时加上，不从源语言整数表达式
默认推导无溢出。

## 4. 循环规范形

### 4.1 LoopSimplify 与 canonicalization

[`loopSimplify.cpp`](../src/mid/transform/loop/loopSimplify.cpp) 是 LoopSimplify pass 的入口，调用
[`loopCanonicalization.cpp`](../src/mid/transform/loop/loopCanonicalization.cpp) 由内向外反复重建 LoopInfo，每次只做一类
CFG 改写，直到没有进展。它建立三个基本保证：

1. 每个自然循环有唯一 dedicated preheader；
2. 多条回边先汇入单一 backedge block；
3. 同时有循环内外前驱的 exit 被拆成 dedicated loop exit。

拆边时会同步改 terminator、CFG 缓存和 PHI。多条旧入边的值不同时，在新中介块中建
PHI，然后只给原 PHI 留一条新 incoming。

### 4.2 LCSSA、IndVarSimplify 与 LoopRotate

- [`lcssa.cpp`](../src/mid/transform/loop/lcssa.cpp) 把循环内定义的外部 use 改经 exit 顶部 PHI。它依赖 dedicated
  exits，并按 innermost-first 处理嵌套。
- [`indVarSimplify.cpp`](../src/mid/transform/loop/indVarSimplify.cpp) 利用 LoopInfo/SCEV 规一化循环控制 IV，重写
  退出值和可证明的 live-out，并保留动态边界的溢出安全。
- [`loopRotate.cpp`](../src/mid/transform/loop/loopRotate.cpp) 把规范 while 形状转为 latch 检测的 do-while 形状，
  必要时拆 exit edge 并克隆 guard。它只旋转可以正确处理 zero-trip 和 live-out 的循环。
- [`simpleLoopUnswitch.cpp`](../src/mid/transform/loop/simpleLoopUnswitch.cpp) 对循环不变条件克隆真/假两个版本，
  在 preheader 分派，并依赖 LCSSA 限定出口修复范围。

## 5. 标量循环化简

| Pass | 匹配与改写 |
| --- | --- |
| [`InductiveRangeCheckElimination`](../src/mid/transform/loop/inductiveRangeCheckElimination.cpp) | 把单调 IV guard 吸收进 `min`/`max` trip bound，删除每轮的 skip/continue 检查 |
| [`LICM`](../src/mid/transform/loop/loopInvariantCodeMotion.cpp) | 把可投机执行的循环不变纯指令外提到 preheader；load 还需 BasicAA 证明不被循环修改 |
| [`LoopDeletion`](../src/mid/transform/loop/loopDeletion.cpp) | 删除可证终止、无副作用、无 live-out 的计数循环，也可断开单次回边 |
| [`LastIterationElimination`](../src/mid/transform/loop/lastIterationElimination.cpp) | 对只有最后一轮覆盖结果可观测的循环，把控制值 clamp 到最后执行值，保留 zero-trip 路径 |
| [`LoopFixedPointEliminate`](../src/mid/transform/loop/loopFixedPointEliminate.cpp) | 比较每个活跃非控制 header PHI 与回边值，状态全不变时提前退出 |
| [`LoopResetPointElimination`](../src/mid/transform/loop/loopResetPointElimination.cpp) | 识别“旧状态×本轮因子”的内存递推，从最后一个可证明完全覆盖状态的零因子处开始执行 |
| [`LoopMemoryScalarPromotion`](../src/mid/transform/loop/loopMemoryScalarPromotion.cpp) | 把同一循环不变地址的精确 load/store 改用局部 mirror alloca，由后续 Mem2Reg 建立 SSA |

`LoopFixedPointEliminate` 的安全条件包括：有限 unit-stride 计数递推只用于 trip guard、
无 call、store 不与任意 loop load 别名，且所有可观测非控制状态都参与相等比较。
近期对嵌套情况加了两层保护：header-exiting 的嵌套形状会被拒绝；一旦外层 while
插入早退出边，同一次函数遍历不再变换其内部循环，避免使用已过期的循环嵌套信息。

## 6. 递推与闭式折叠

### 6.1 LinearRecurrenceFold

[`linearRecurrenceFold.cpp`](../src/mid/transform/loop/linearRecurrenceFold.cpp) 识别 `x <- A*x` 形式的耦合 i32 线性状态，
语义是模 `2^32`。若出口线性型 `c^T` 是左特征向量，发射标量幂；否则发射矩阵快速幂。
循环计数、状态更新、出口 use 和无副作用都必须完整匹配后才删除原环。

### 6.2 LoopRepFold 与 LoopModuloDelay

[`loopRepFold.cpp`](../src/mid/transform/loop/loopRepFold.cpp) 包含三类路径：

- 迭代无关的重复贡献变为一次计算乘 trip count；
- `a*i+b` 形式的常量仿射和由 SCEV 直接求闭式；
- 正模数的加性递推。简单形式在有运行时非负/不溢出 guard 时用 i32 快路径，更一般的
  可求和表达式调用 `__compiler.summable_mod_sum`，失败时回退原循环。

[`loopModuloDelay.cpp`](../src/mid/transform/loop/loopModuloDelay.cpp) 位于 LoopRepFold 之后、LoopUnroll 之前。它把可证明安全的
循环携带 `srem` 拆成 i64 独立贡献累加，只在出口做一次约减。它不处理 LoopRepFold
已经可以完全闭式求值的情况，也不在展开破坏规范递推形状后再猜测匹配。

### 6.3 三角递推变换

- [`LoopInvariantReduction`](../src/mid/transform/loop/loopInvariantReduction.cpp) 外提私有的循环不变 store 区域，并从外层取模归约中抽出纯内层 reduction。
- [`TriangularRemapSourceCompose`](../src/mid/transform/loop/triangularRemapSourceCompose.cpp) 对已证明的有序三角 copy 链进行按需源追踪，保留原 consumer
  和 bounds 失败时的原路径。
- [`TriangularPanelize`](../src/mid/transform/loop/triangularPanelize.cpp) 把三角标量递推的公共前缀改为多向量 panel，波内依赖仍按原顺序执行。

## 7. 循环重排与局部性

### 7.1 ScalarExpansion 与 LoopDistribution

[`scalarExpansion.cpp`](../src/mid/transform/loop/scalarExpansion.cpp) 是可独立调用的准备 pass：它为可交换的标量
reduction 分配函数局部 scratch buffer，但不自己改 CFG。当前 `main.cpp` 没有单独调度
ScalarExpansion；[`loopDistribution.cpp`](../src/mid/transform/loop/loopDistribution.cpp) 会复用已有 scratch，也会在缺失时
自行创建，然后把计算拆为：

```text
clear scratch → compute region → store-back
```

reduction PHI 在 compute 区改写为按父循环 IV 索引的 scratch load/store。变换依赖
DependenceAnalysis 证明交换不反转依赖，并要求 CostModel 计算的最内层字节 stride 严格下降。

### 7.2 LoopFusion 与 LoopInterchange

[`loopFusion.cpp`](../src/mid/transform/loop/loopFusion.cpp) 只融合同级、相邻且 trip count 完全一致的规范 while 循环。
两环之间的纯指令要么可外提，要么可下沉；跨环 SSA 状态、不确定 alias、call 或可能
破坏后续交换收益的形状都会拒绝融合。

[`loopInterchange.cpp`](../src/mid/transform/loop/loopInterchange.cpp) 把完全并行且更 cache-friendly 的循环维下沉到最内层。
对不完美嵌套，它先按直线代码段与子循环做 distribution，再递归重建循环。每一层的
依赖方向和 stride 收益都来自 analysis 模块，不由固定循环层数或数组名决定。

### 7.3 Skewing 与三角调度

- [`LoopSkewing`](../src/mid/transform/loop/loopSkewing.cpp) 从仿射边界推导 skew 系数与 offset，改写内层起止值以消除斜向依赖。
- [`TriangleInterchange`](../src/mid/transform/loop/triangleInterchange.cpp) 对 `0 <= distance < row <= extent` 一类已证明三角域，把 distance
  调度为外层 wave，把同一 wave 上的独立 cell 改为内层 lane。
- 两者都要求完整的 IV、仿射访存、alias 和出口形状证明。成功后的 wavefront 并行维会通过
  `WavefrontCoincident` 语义标记传给后续 pass。

## 8. 并行化、向量化和展开

### 8.1 ParallelizeLoops

[`parallelizeLoops.cpp`](../src/mid/transform/loop/parallelizeLoops.cpp) 把可证明 DOALL 的循环区域外提为：

```text
__sysy_par_body_<id>(lo, hi)
```

原调用点替换为 `__sysy_parallel_for(id, lo, hi)`，live-in 经 `__sysy_par_ctx_*` 全局槽传递。
它支持可合并的内存 reduction、标量加减/模 reduction 和可私有化 scratch；存在不可证明的
循环携带依赖、alias、call 副作用或 live-out 时拒绝外提。

本 pass 只生成 worker IR 和 dispatch call。最终的 `__sysy_par_dispatch` 跳转表与双核 clone/spin
汇编 runtime 在后端输出阶段追加，不属于 `src/mid/runtime`。

### 8.2 LoopVectorize

[`loopVectorize.cpp`](../src/mid/transform/loop/loopVectorize.cpp) 面向 Cortex-A53 的 128-bit NEON，主要使用 4 个
`i32`/`float` lane。它先从 `LoopVectorizationAnalysis::Plan` 取得 legality/profitability 结果，
然后生成 vector main loop 和 scalar remainder loop。

支持的 operand 形式包括连续访存、递增 IV、循环不变值和有限 gather。标量 reduction 可组装为
add/sub/smin/smax 向量归约，其中 expression-form reduction 会记录多个 load 与表达式项。
别名、trip count、对齐/步长、循环体成本或标量尾部成本不合适时保留原循环。

### 8.3 IndVarStrengthReduce 与 LoopUnroll

[`IndVarStrengthReduce`](../src/mid/transform/loop/indVarStrengthReduce.cpp) 把循环内重复的仿射乘加、GEP 地址计算改为递推值，同时避开已经
用 `TargetPointerRecurrenceLoop` 标记的目标化指针环。

[`loopUnroll.cpp`](../src/mid/transform/loop/loopUnroll.cpp) 支持规范 while、rotated do-while、结构化 CFG region 和带状态的
header-exiting region。展开因子由循环体积、访存、向量指令和寄存器/指针压力决定：
紧凑的纯寄存器环可到 8×，普通标量环为 4×，向量或高指针压力环通常为 2×。

对动态 bound，主循环使用运行时 guard 确保 `bound-adjustment` 不回绕；不安全区间走原标量环。
主环之后的余数守卫与原循环处理 `0..N-1` 个剩余迭代，exit PHI 汇合主环和余数路径的
live-out。

## 9. 调试与验证

建议先使用：

```bash
build/compiler -O1 --dump-ir --verify-ir -c input.sy -o /tmp/out.ir
LOOP_VERIFY_STRICT=1 build/compiler -O1 --verify-ir -c input.sy -o /tmp/out.ir
```

常用调试开关：

| 开关 | 观察内容 |
| --- | --- |
| `DEBUG_INDVAR_SIMPLIFY=1` / `DEBUG_IVSR=1` | IV 规一化与 strength reduction |
| `DEBUG_INDUCTIVE_RANGE=1` | 迭代域裁剪的匹配/拒绝原因 |
| `DEBUG_LOOP_FIXED_POINT=1` | 固定点状态与嵌套安全门槛 |
| `DEBUG_LOOP_FUSION=1` / `DEBUG_LOOP_INTERCHANGE=1` | 融合、交换的依赖与收益判定 |
| `DEBUG_LINEAR_RECURRENCE=1` / `DEBUG_LOOP_REPFOLD=1` | 递推摘要、闭式和回退路径 |
| `DEBUG_LOOP_MODULO_DELAY=1` | 宽位延迟取模的 legality |
| `DEBUG_LOOP_UNROLL=1` | 展开形状、因子和 guard |
| `DEBUG_LOOP_VECTORIZE=1` / `DEBUG_LOOP_VECTORIZE_REJECT=1` | 向量化 plan 和拒绝原因 |
| `DEBUG_PARALLEL=1` | DOALL 外提、privatization 和 reduction |
| `DEBUG_LOOP_SKEWING=1` / `DEBUG_TRIANGLE_INTERCHANGE=1` | skew/wavefront 调度 |
| `DEBUG_TRIANGULAR_PANELIZE=1` / `DEBUG_TRIANGULAR_REMAP_SOURCE=1` | 三角递推特化 |

修改循环 CFG 时应把 terminator、`pre_bbs_`/`succ_bbs_`、header/exit PHI 和分析失效视为一个
原子操作。`LoopVerify` 的 strict 模式会在 pass 宣称建立 LoopSimplify/LCSSA 保证时检查它们；
`Module::verify` 则负责更底层的 SSA、CFG 和 use-def 不变式。
