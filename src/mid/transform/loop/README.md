# Loop Transform 说明

## 总体作用

`loop/` 目录实现中间端的循环变换 Pass。与只观察单条指令的 InstCombine 不同，循环 Pass 会分析完整的循环结构、归纳变量、迭代范围、内存访问和跨迭代依赖，并可能重写多个基本块组成的 CFG。

这些 Pass 的总体目标包括：

- 规范化循环 CFG，为后续分析提供统一的 preheader、latch 和 dedicated exit。
- 建立 LCSSA，明确循环内定义如何传递到循环外。
- 化简归纳变量、循环边界和循环内重复地址计算。
- 删除无用迭代、无副作用循环和可用闭式表达式替代的递推循环。
- 移动、融合、交换、倾斜或剥离循环，改善局部性并暴露更多优化机会。
- 通过展开、SIMD 向量化和 DOALL/波前并行化提高循环吞吐量。
- 在每次 CFG 改写后维护 PHI 入边、前驱/后继关系、def-use 链和循环活跃输出。

目录中的优化只依据可证明的通用 IR 结构、别名关系和依赖关系应用，不依赖函数名、测试用例名或特定输入内容。

## 循环形态约定

许多循环优化不能直接处理任意 CFG，需要先建立以下规范形态：

### Simplified Loop Form

由 `LoopSimplify` 建立，主要保证：

- 循环具有专用 preheader。
- 多条回边被合并为统一的 latch/backedge 路径。
- 循环出口是 dedicated exit，即出口块的前驱均来自该循环。

### LCSSA

由 `LCSSA` 在 Simplified Loop Form 基础上建立。循环内定义若在循环外使用，必须先经过出口块中的 PHI：

```text
loop 内定义 %v
        |
        v
exit: %v.lcssa = phi [%v, loop_pred]
        |
        v
循环外用户
```

这样，循环变换只需修复出口 PHI 的入边，就能集中维护循环活跃输出。Pass 可通过 `requiredLoopForm()` 声明前置形态，PassManager 会按需自动运行 `LoopSimplify` 和 `LCSSA`。

## 常用分析

循环 Pass 通常组合使用以下分析结果：

- `LoopInfo`：识别自然循环、父子关系、header、latch、preheader、exiting 和 exit 块。
- `DominatorTreeAnalysis`：验证定义是否支配新使用点，以及 CFG 移动是否合法。
- `ScalarEvolution`：描述归纳变量、仿射表达式和可推导的迭代次数。
- `BasicAliasAnalysis` / `ArgumentAliasAnalysis`：判断不同内存根或指针访问是否可能别名。
- `DependenceAnalysis` / `AffineAnalysis`：判断交换、融合、倾斜、向量化和并行化是否保持跨迭代依赖顺序。
- `RangeAnalysis`：证明循环边界、溢出条件或分支在整个迭代域中恒定。
- `FunctionTerminationAnalysis`：在删除或跳过迭代前确认不会改变可观察的终止行为。

## 各文件作用

下面的代码片段用于展示各 Pass 的核心变换方向。它们省略了实际实现中的支配关系、别名、溢出、退出边和收益检查；只有源码中的完整合法性判断全部通过时，Pass 才会提交改写。

各文件按 `loop/` 目录中的文件顺序排列，每个文件给出：文件名、作用、示例片段。

#### `indVarSimplify.cpp`

> **归纳变量化简**：识别基本和派生归纳变量，规范化循环控制流，消除能够由控制 IV 直接推导的重复递推，并在迭代次数可证明时用 preheader 计算的退出值替换循环外使用。

```cpp
// 变换前：i 和 j 是等价的线性递推。
int j = 4;
for (int i = 0; i < n; ++i, j += 2)
    use(j);

// 变换后：删除 j 的循环携带状态。
for (int i = 0; i < n; ++i)
    use(4 + 2 * i);
```

#### `indVarStrengthReduce.cpp`

> **归纳变量强度削减**：把每轮重新执行的乘法或线性 GEP 改写为指针或整数递推。较昂贵的初值计算放在 preheader，latch 中只保留固定步长更新。

```cpp
// 变换前
for (int i = 0; i < n; ++i)
    use(base[i * 4]);

// 变换后示意
int *p = base;
for (int i = 0; i < n; ++i, p += 4)
    use(*p);
```

#### `inductiveRangeCheckElimination.cpp`

> **归纳范围检查消除**：利用归纳变量的单调范围、入口保护条件和仿射边界，证明循环内范围检查始终通过；证明成立后删除每轮分支，或收紧循环迭代域。

```cpp
// 已有入口保护：0 <= begin && end <= length
if (0 <= begin && end <= length)
    for (int i = begin; i < end; ++i) {
        if (0 <= i && i < length)  // 可由入口保护和 IV 范围证明
            sum += a[i];
    }

// 循环内检查可删除，只保留 sum += a[i]。
```

#### `lastIterationElimination.cpp`

> **最后一次迭代消除**：当每轮写入都会完整覆盖前一轮结果，而且循环外只观察最终覆盖值时，跳过不可观察的早期迭代，仅执行最后一次动态迭代。

```cpp
// 前 n-1 次对 result 的写入都会被下一轮覆盖。
for (int i = 0; i < n; ++i)
    result = build(i);

// 保留零次迭代语义后，可简化为：
if (n > 0)
    result = build(n - 1);
```

#### `lcssa.cpp`

> **循环闭包 SSA（LCSSA）**：构造 Loop-Closed SSA。循环内定义若在循环外使用，会先在循环出口建立 PHI，外部用户再读取该 PHI，从而把 live-out 修复集中到退出边上。

```llvm
; 变换前
loop:  %next = add i32 %iv, 1
exit:  %result = add i32 %next, 10

; 变换后
exit:  %next.lcssa = phi i32 [ %next, %loop ]
       %result = add i32 %next.lcssa, 10
```

#### `loopCloneUtils.cpp`

> **循环克隆工具**：提供循环区域克隆的共享工具，本身不是独立 Pass。它复制基本块和指令、建立旧值到新值的映射，并用映射后的值修复克隆区域的操作数和出口 PHI。

```cpp
// 简化示意：克隆时先记录对应关系，再统一重映射引用。
valueMap[oldBlock] = clonedBlock;
valueMap[oldInst] = clonedInst;
clonedInst->set_operand(index, valueMap[oldOperand]);
exitPhi->add_phi_pair_operand(valueMap[oldValue], clonedPredecessor);
```

#### `loopDeletion.cpp`

> **循环删除**：删除没有可观察副作用且 live-out 可以直接替代的循环。若循环已被证明至多执行一次，也可以断开回边并选择初始值或一次迭代后的值。

```cpp
// 结果从未使用，循环体也没有副作用。
int x = seed;
for (int i = 0; i < n; ++i)
    x = x * 3 + 1;

// 整个循环可以删除。
```

#### `loopFusion.cpp`

> **循环融合**：融合边界相同的相邻同级循环。Pass 会先验证标量传递、调用副作用、别名和跨迭代内存依赖，确保合并后不会改变执行顺序语义。

```cpp
// 变换前
for (int i = 0; i < n; ++i) a[i] = b[i] + 1;
for (int i = 0; i < n; ++i) c[i] = a[i] * 2;

// 变换后
for (int i = 0; i < n; ++i) {
    a[i] = b[i] + 1;
    c[i] = a[i] * 2;
}
```

#### `loopInterchange.cpp`

> **循环交换**：依据仿射访问和依赖方向交换嵌套循环次序，使连续内存维度处于内层，或为向量化、并行化暴露更合适的迭代维度。

```cpp
// 变换前：按列访问行主序矩阵。
for (int j = 0; j < m; ++j)
    for (int i = 0; i < n; ++i)
        use(a[i][j]);

// 交换后：内层 j 连续访问。
for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j)
        use(a[i][j]);
```

#### `loopInvariantCodeMotion.cpp`

> **循环不变量外提（LICM）**：实现 LICM。操作数不随循环变化、无副作用且可以安全提前执行的指令会被提升到 preheader；可能除零、越界或仅在条件路径执行的指令需要额外安全证明。

```cpp
// 变换前
for (int i = 0; i < n; ++i)
    out[i] = a * b + i;

// 变换后
int invariant = a * b;
for (int i = 0; i < n; ++i)
    out[i] = invariant + i;
```

#### `loopInvariantReduction.cpp`

> **循环不变量归约**：识别被外层循环重复执行、实际却与外层迭代无关的私有初始化和内层归约，将等价计算移出外层或复用一次计算结果。

```cpp
// 变换前：innerSum 与外层 i 无关，却被重复计算。
for (int i = 0; i < m; ++i) {
    int innerSum = 0;
    for (int j = 0; j < n; ++j) innerSum += table[j];
    result[i] = innerSum + i;
}

// 变换后：innerSum 在外层循环前只计算一次。
```

#### `loopMemoryScalarPromotion.cpp`

> **循环内存标量化**：把循环中对同一循环不变地址的反复 load/store 提升为局部标量状态。循环入口读取一次，循环内更新标量，退出时再写回内存。

```cpp
// 变换前
for (int i = 0; i < n; ++i)
    *sum = *sum + a[i];

// 变换后
int local = *sum;
for (int i = 0; i < n; ++i)
    local += a[i];
*sum = local;
```

#### `loopPeel.cpp`

> **循环剥离**：把第一次迭代克隆到循环外单独执行，再让剩余迭代进入原循环。这样可以消除只在首轮成立的判断，或为后续展开、向量化建立稳定状态。

```cpp
// 变换前
for (int i = 0; i < n; ++i)
    body(i);

// 剥离首轮后
if (n > 0)
    body(0);
for (int i = 1; i < n; ++i)
    body(i);
```

#### `loopRotate.cpp`

> **循环旋转**：旋转循环 CFG，把入口处的条件检查与回边条件整理成更适合分析的形态。旋转后的循环通常具有清晰的零次迭代入口检查和底部回边判断。

```cpp
// 变换前：条件在循环头。
while (i < n) {
    body(i);
    ++i;
}

// 变换后示意：入口先判断，循环底部形成回边条件。
if (i < n)
    do {
        body(i);
        ++i;
    } while (i < n);
```

#### `loopSimplify.cpp`

> **循环 CFG 规范化**：`LoopSimplify` 的 Pass 入口。它建立专用 preheader、统一回边并拆分非专用出口，使后续 Pass 可以通过固定位置取得循环入口、latch 和 exit。

```text
变换前：entry ─┬─> header <── latch.a
               └─> header <── latch.b

变换后：entry -> preheader -> header <- latch
                              |
                              +-> dedicated.exit
```

#### `loopSkewing.cpp`

> **循环倾斜**：根据仿射依赖距离对循环坐标做倾斜或加权波前变换，使存在跨维依赖的迭代仍按合法顺序执行，并为同一波前内的并行执行创造条件。

```cpp
// 原坐标：(i, j)，依赖可能沿 i 和 j 传播。
for (int i = 1; i < n; ++i)
    for (int j = 1; j < m; ++j)
        a[i][j] = a[i - 1][j] + a[i][j - 1];

// 倾斜示意：使用 wave = i + j，按波前推进。
for (int wave = 2; wave < n + m; ++wave)
    for (int i = lower(wave); i < upper(wave); ++i)
        compute(i, wave - i);
```

#### `loopVectorize.cpp`

> **循环向量化**：识别元素级循环和归约循环，证明内存访问可以并行后构造 NEON 向量主循环；不能静态排除别名时可建立运行时检查，并保留标量尾循环。

```cpp
// 标量循环
for (int i = 0; i < n; ++i)
    c[i] = a[i] + b[i];

// 四元素向量主循环示意
for (; i + 3 < n; i += 4)
    store4(c + i, load4(a + i) + load4(b + i));
for (; i < n; ++i)
    c[i] = a[i] + b[i];
```

#### `parallelizeLoops.cpp`

> **循环并行化**：验证循环是否属于 DOALL，或是否满足已证明的波前执行条件；随后处理 live-in、live-out、私有 scratch 和归约，把循环体 outline 为工作函数并调用并行运行时。

```cpp
// 变换前：各次迭代互不依赖。
for (int i = 0; i < n; ++i)
    out[i] = work(in[i]);

// 变换后示意
parallel_for(0, n, [&](int begin, int end) {
    for (int i = begin; i < end; ++i)
        out[i] = work(in[i]);
});
```

#### `scalarExpansion.cpp`

> **标量扩展**：把"跨迭代串行依赖"的标量归约，通过按迭代索引的临时数组（scratch）拆成多个独立状态，解除伪依赖，为并行化/向量化铺路。

```cpp
// 变换前：单条 loop-carried 依赖链。
for (int i = 0; i < n; ++i)
    sum += a[i];

// 四路标量扩展示意。
int partial[4] = {0, 0, 0, 0};
for (int i = 0; i < n; ++i)
    partial[i & 3] += a[i];
sum += partial[0] + partial[1] + partial[2] + partial[3];
```

#### `simpleLoopUnswitch.cpp`

> **简单循环反切换**：对循环不变量条件进行版本分裂。条件被移到循环外，真、假两个循环副本中的对应分支随后变成确定路径。

```cpp
// 变换前：flag 在循环中不变。
for (int i = 0; i < n; ++i)
    if (flag) a[i] = x(i); else a[i] = y(i);

// 变换后
if (flag)
    for (int i = 0; i < n; ++i) a[i] = x(i);
else
    for (int i = 0; i < n; ++i) a[i] = y(i);
```

## Pass 之间的典型关系

循环 Pass 并不是各自孤立运行，常见协作关系为：

```text
LoopSimplify
    -> LCSSA
    -> IndVarSimplify / LICM / RangeCheckElimination
    -> 删除、闭式折叠、融合、交换、倾斜等高层变换
    -> 再次 LoopSimplify + LCSSA
    -> LoopVectorize / IndVarStrengthReduce
    -> LoopPeel
    -> InstCombine / DCE / CFGSimplify 清理新暴露的冗余 IR
```

实际 `-O1` 流水线会多次运行规范化和标量清理 Pass，因为前一个循环变换产生的新 PHI、分支或表达式可能为后一个 Pass 创造机会。具体执行顺序以 `src/main.cpp` 中的 PassManager 配置为准。

一些重要的先后关系：

- `LoopSimplify` 必须先于 `LCSSA`，因为 LCSSA 依赖 dedicated exit。
- 需要改写循环活跃输出的 Pass 通常要求 LCSSA，避免在任意循环外位置逐个修复 use。
- `IndVarSimplify` 和 LICM 能把循环变成更规范的分析形式，通常位于向量化和展开之前。
- `LoopVectorize` 后运行 `IndVarStrengthReduce`，可继续把向量循环的仿射地址计算变成指针递推。
- `LoopPeel` 或循环版本化后通常需要 InstCombine、DCE 和 CFGSimplify 清理克隆产生的常量分支、退化 PHI 和死块。
