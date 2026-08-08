# Mid-Level Analysis 实现文档

本文档描述当前编译器中 `mid/analysis` 模块的实现。这里的 analysis为优化 pass 提供可查询事实、合法性判断和
成本估计。分析结果通常以 `Top/Unknown/MayAlias/ANY` 等保守值表示，以便在变换失败时安全退回。

## 1. 模块边界与生命周期

### 1.1 目录结构

- 实现位于 [`src/mid/analysis`](../src/mid/analysis)。
- 公共接口位于 [`src/include/mid/analysis`](../src/include/mid/analysis)。
- `analysisManager.hpp`、`preservedAnalyses.hpp`、`constantEvaluator.hpp`、
  `loopUtils.hpp`、`valueFacts.hpp` 主要是接口或 header-only 工具，没有对应的
  `.cpp` 实现。

本模块当前包含以下分析和辅助组件：

| 组件 | 主要职责 |
| --- | --- |
| `AnalysisManager` | 创建、缓存和失效分析结果 |
| `DominatorTreeAnalysis` | 入口可达 CFG 的立即支配树和 SSA 支配查询 |
| `PostDominatorTreeAnalysis` | 多出口 CFG 的后支配关系和立即后支配点 |
| `DominanceFrontierAnalysis` | 独立计算支配边界，供 PHI 放置等变换使用 |
| `BasicAliasAnalysis` | 指针别名、Mod/Ref、副作用和捕获分析 |
| `ArgumentAliasAnalysis` | 根据所有调用点证明参数根对象不别名 |
| `LazyValueInfo` | 在 block 或 CFG edge 上求常量和谓词事实 |
| `LoopInfo` | 基于支配树识别循环、嵌套和归纳变量 |
| `ScalarEvolution` | SCEV 表达式、AddRec、trip count 和 GEP 线性化 |
| `RangeAnalysis` | 整数区间、分支事实、load 范围和归一化取模 |
| `AffineAnalysis` | 将值表示成归纳变量上的仿射表达式 |
| `DependenceAnalysis` | 内存依赖方向和循环携带依赖 |
| `LoopAccessAnalysis` | 收集循环访存并分类 GEP |
| `ReductionAnalysis` | 检测 scalar reduction 的可扩展内层循环 |
| `RecurrenceAnalysis` | 分析累加递推并计算闭式结果 |
| `ModuloRecurrenceAnalysis` | 识别可安全合并的正模数循环状态递推 |
| `SummableExpressionAnalysis` | 将可求和的模表达式归纳为线性/整除摘要 |
| `LoopInterchangeAnalysis` | 判断交换合法性和访存成本收益 |
| `LoopVectorizationAnalysis` | 构造向量化 legality/profitability plan |
| `CostModel` | 估算 GEP 的 cache stride |
| `VectorizationCostModel` | 估算固定宽度向量化成本 |
| `FunctionTerminationAnalysis` | 证明函数在定义行为下必然返回 |
| `LoopVerify` | 校验循环规范形和 LCSSA |
| `ConstantEvaluator`、`ValueFacts`、`loopUtils` | 常量、位事实和 SSA 使用位置辅助函数 |

### 1.2 AnalysisManager

[`analysisManager.hpp`](../src/include/mid/analysis/analysisManager.hpp) 和
[`analysisManager.cpp`](../src/mid/analysis/analysisManager.cpp) 实现统一分析入口。

`AnalysisManager` 的缓存分两级：

```text
Module
  └── BasicAliasAnalysis

Function
  ├── DominatorTreeAnalysis
  ├── PostDominatorTreeAnalysis
  ├── DominanceFrontierAnalysis → DominatorTreeAnalysis
  ├── LazyValueInfo
  ├── LoopInfo → DominatorTreeAnalysis
  ├── RangeAnalysis
  └── ScalarEvolution
```

- `BasicAliasAnalysis` 绑定整个 `Module`，首次调用 `getBasicAA(Module*)` 时分析模块。
- 其余分析按 `Function*` 缓存，首次查询时递归建立所需依赖。
- `RangeAnalysis` 需要 `LoopInfo` 和 `ScalarEvolution`，并持有 `AnalysisManager`
  以便在函数调用范围分析时查询其它函数。
- `LazyValueInfo` 建立时接收当前函数的 `LoopInfo` 和支配树。
- 分析对象中的 `Loop*`、`BasicBlock*` 和 `Value*` 都指向当前 IR；如果 transform
  改动了 CFG 或删除了指令，不能继续使用过期对象。

### 1.3 PreservedAnalyses 与失效

[`preservedAnalyses.hpp`](../src/include/mid/analysis/preservedAnalyses.hpp) 用 bit mask
记录以下分析是否仍然有效：

- `BasicAA`
- `DominatorTree`
- `PostDominatorTree`
- `DominanceFrontier`
- `LazyValueInfo`
- `LoopInfo`
- `SCEV`

`all()` 表示全部保留，`none()` 表示全部失效，`cfgAnalyses()` 表示 CFG 没有改变，
因此支配树、后支配树和支配边界都可复用。当前 `LoopInfo` 还保存归纳变量描述，指令改写
后不能只因 CFG 未变就保留。PassManager 在 pass 返回结果后调用
`AnalysisManager::invalidate` 或 `invalidateFunction`：

- 不保留 `DominatorTree` 时，同时清除支配边界、LoopInfo、LVI、SCEV 和 RangeAnalysis。
- 不保留 `LoopInfo` 时，同时清除依赖循环结构的 `LoopInfo`、SCEV 和 RangeAnalysis。
- 不保留 `SCEV` 时清除 `ScalarEvolution`。
- 不保留 `LazyValueInfo` 时清除 LVI。
- 不保留 `BasicAA` 时清除模块级 alias summary。
- `clearRangeAnalyses()` 用于清除所有函数的 RangeAnalysis，避免 RangeAnalysis
  查询期间递归调用自身产生旧结果。

分析默认遵循“证明失败即保守”的约定：

- alias 返回 `MayAlias`；
- dependence direction 返回 `DIR_ANY`；
- range 返回 `Top`；
- 成本返回未知；
- legality 查询返回拒绝。

## 2. 值、常量和别名分析

### 2.1 BasicAliasAnalysis

实现：[`basicAliasAnalysis.hpp`](../src/include/mid/analysis/basicAliasAnalysis.hpp)、
[`basicAliasAnalysis.cpp`](../src/mid/analysis/basicAliasAnalysis.cpp)。

#### 内存位置和指针解析

`MemoryLocation` 由指针、元素类型和字节大小组成。内部 `PointerInfo` 会沿着
`bitcast`、GEP 等指针表达式向后解析：

```text
pointer = base + constant byte offset
```

不能保留为常量的偏移会设置 `hasConstantOffset=false`。`getUnderlyingObject` 提供
穿透 GEP/bitcast 后的 alloca、global、argument 或其它根值。

`alias` 的判断顺序是：

1. 根对象不同且可证明不能相同，返回 `NoAlias`。
2. 根对象和常量范围完全相同，返回 `MustAlias`。
3. 偏移、大小或根对象不够确定，返回 `MayAlias`。

不同的局部 `alloca` 和不同的 global 是最容易证明不别名的情况；函数参数通常必须
保守处理，除非由额外的 `ArgumentAliasAnalysis` 提供调用点事实。

#### Mod/Ref 和函数摘要

`getModRefInfo` 与 `getCallModRef` 判断指令或调用对一个内存位置是否：

- 只读（`Ref`）；
- 只写（`Mod`）；
- 读写（`ModRef`）；
- 不访问（`NoModRef`）。

`analyze(Module*)` 为函数建立 `FunctionSummary`，摘要包含：

- `pure` 和 `sideEffect`；
- 总体 Mod/Ref 效果；
- 是否存在未知内存效果；
- 每个参数是否 no-capture；
- 可识别内存位置的效果列表。

`isPure`、`mayHaveSideEffect`、`isNoCapture` 和 `isLocalArrayPointer` 被 LICM、
内存标量提升和循环变换用作 legality 条件。分析中遇到未知调用或无法跟踪的指针
会保守保留副作用和 alias 可能性。

调试环境变量：`DEBUG_BASIC_AA=1`。

### 2.2 ArgumentAliasAnalysis

实现：[`argumentAliasAnalysis.hpp`](../src/include/mid/analysis/argumentAliasAnalysis.hpp)、
[`argumentAliasAnalysis.cpp`](../src/mid/analysis/argumentAliasAnalysis.cpp)。

BasicAA 无法仅凭两个形参证明它们不同，因为调用方可能把同一对象传给两个形参。
ArgumentAliasAnalysis 扫描模块内所有对目标函数的调用，建立：

```text
formal argument -> {global / alloca root objects}
```

解析过程会穿透 GEP/bitcast：

- 实参底层是 global 或 alloca：加入根对象集合；
- 实参底层是调用方的参数：递归合并该参数的根集合；
- 没有调用点、出现未知指针或递归环：标记为 unknown。

`noAlias` 只在两个根集合都已知且不相交时返回 true。任何一侧 unknown 都必须返回
false，因此该分析只能减少保守性，不能制造新的 alias 保证。

### 2.3 ConstantEvaluator

实现：[`constantEvaluator.hpp`](../src/include/mid/analysis/constantEvaluator.hpp)。

这是 namespace 级的 header-only 常量折叠工具，不负责遍历 CFG。整数二元运算使用
32 位 wrap 语义实现 add/sub/mul；除法和取模检查除数为零，并拒绝
`INT_MIN / -1`。浮点工具支持 add/sub/mul/div，并拒绝零除数。

该工具适合在已知操作数都是常量时使用。它不应被当作一般的 RangeAnalysis，也不负责
证明表达式在任意执行路径上可求值。

### 2.4 LazyValueInfo

实现：[`lazyValueInfo.hpp`](../src/include/mid/analysis/lazyValueInfo.hpp)、
[`lazyValueInfo.cpp`](../src/mid/analysis/lazyValueInfo.cpp)。

LVI 查询某个值在 block、block edge 或指定指令上下文中的事实：

- `getConstant`：求 block 内可确定的常量；
- `getConstantOnEdge`：只利用指定 CFG edge 的事实；
- `getPredicateAt`：判断比较谓词恒真、恒假或未知；
- `getPredicateOnEdge`：结合边方向判断谓词。

查询会递归处理常量、SSA 定义、PHI 和控制流事实；循环和多前驱路径必须经过合流，
不能只依据某一条可能路径下的值。`forgetValue`、`eraseBlock` 和 `clear` 是缓存
失效接口，当前的修改性维护较轻，IR 结构发生变化时应整体清理。

## 3. 循环和控制流分析

### 3.1 支配树、后支配树和支配边界

实现：[`dominanceAnalysis.hpp`](../src/include/mid/analysis/dominanceAnalysis.hpp)、
[`dominanceAnalysis.cpp`](../src/mid/analysis/dominanceAnalysis.cpp)。

`DominatorTreeAnalysis` 从 entry 对可达块做 reverse postorder，再用迭代式 immediate
dominator 算法建立树。节点保存父节点、children、深度、RPO 序号和 DFS 区间，因此块级
`dominates` 是常数时间查询；不可达块没有树节点，也不会被误当作由 entry 支配。接口还
提供最近公共支配点、子树遍历和指令级支配查询。

SSA use 查询专门处理 PHI：普通 operand 在 user 指令处使用，PHI incoming value 则在
对应前驱边末端使用。同一块中的普通定义按指令顺序判断。这与 LLVM `DominatorTree`
把 PHI use 视作 incoming edge use 的做法一致，pass 不应再各自拼一套近似规则。

`PostDominatorTreeAnalysis` 从实际 exit 反向标出能到达出口的区域。多个 return 通过隐式
virtual root 建模，所以某块没有共同的实际立即后支配点时 `getIPostDominator` 返回空；
完全不能到达出口的区域不进入当前后支配关系。`DominanceFrontierAnalysis` 与树分开缓存，
只有 Mem2Reg 等需要 PHI 放置的 pass 才会请求它。

实现思路参考 LLVM 的 `llvm/IR/Dominators.h`、`llvm/Analysis/PostDominators.h` 和
`llvm/docs/NewPassManager.md`：正向树、反向树和 pass 失效分别建模，不把结果塞回 IR
节点；但这里没有照搬 LLVM 的通用图模板。当前 CFG 规模下选择较简单的全量重算；LLVM
风格的增量 `DomTreeUpdater` 留待 CFG 高频局部改写确实成为瓶颈后再引入。

设置 `DEBUG_VERIFY_DOMINANCE=1` 时，分析完成后会用独立的集合不动点结果核对树关系，
发现不一致立即终止。`print` 可输出每个块的 idom/ipdom、深度和支配边界。

### 3.2 LoopInfo

实现：[`loopInfo.hpp`](../src/include/mid/analysis/loopInfo.hpp)、
[`loopInfo.cpp`](../src/mid/analysis/loopInfo.cpp)。

`LoopInfo::analyze(Function*, DominatorTreeAnalysis&)` 复用 AnalysisManager 中的支配树，
然后依次执行：

1. 将“回到被支配 header 的边”识别为回边，收集自然循环块；
2. 计算每个循环的 preheader、latch、exiting、exit 和 `blocksOrdered`；
3. 建立 parent/children/depth 嵌套树；
4. 分析 canonical IV、一般 `+1` induction IV 和控制归纳描述。

`Loop` 保存两种不同用途的归纳变量信息：

- `canonicalIV` 只表示从零开始的 `+1` 形式，供既有变换使用；
- `inductionIV`/`inductionInit` 表示允许非零初值的一般 unit-stride 形式；
- `controlInduction` 还记录 step、比较谓词、bound、guard 位置以及是否比较 update
  后的值，可表示一定范围内的非 unit stride。

`preheader` 的定义比较严格：必须是唯一的循环外前驱，且该前驱只有无条件跳转到
header。`singleLatch()` 和 `singleExit()` 也只在数量恰好为一时成功。因此变换不能
把“存在一个候选块”误认为规范循环形。

`describeEqualityControlInduction` 用于描述 `eq/ne` 终止的控制递推，但不会覆盖
旧的 ordered-predicate 字段，避免破坏只理解 `<` 的现有客户端。

### 3.3 loopUtils

实现：[`loopUtils.hpp`](../src/include/mid/analysis/loopUtils.hpp)。

`getSemanticUseBlock` 处理普通指令和 PHI 的使用位置：普通 operand 的使用块是 user
所在块，而 PHI incoming value 的语义使用块是对应 incoming predecessor。这一点在
循环外提、LCSSA 检查和 live-out 分析中很重要。

### 3.4 LoopVerify

实现：[`loopVerify.hpp`](../src/include/mid/analysis/loopVerify.hpp)、
[`loopVerify.cpp`](../src/mid/analysis/loopVerify.cpp)。

`verifyLoopForms` 分三层检查循环规范形：

- L1：dedicated preheader 和唯一 latch；
- L2：dedicated exits，exit 块的前驱都来自循环内部；
- L3：循环内部定义、循环外使用的值必须通过 exit 顶部的 LCSSA PHI 导出。

默认 `warnOnly=true`，发现违例只向 stderr 输出。`LOOP_VERIFY_STRICT=1` 可使变换后
违例直接 abort，适合定位第一个破坏循环形的 pass。`reportClean` 控制是否输出无违例
报告。

## 4. Scalar Evolution、范围和仿射表达式

### 4.1 ScalarEvolution

实现：[`scalarEvolution.hpp`](../src/include/mid/analysis/scalarEvolution.hpp)、
[`scalarEvolution.cpp`](../src/mid/analysis/scalarEvolution.cpp)。

SCEV 是针对 SSA 值的结构化表达式，当前支持：

- `SCEVConstant`：常量；
- `SCEVUnknown`：无法识别的值；
- `SCEVAddExpr`、`SCEVMulExpr`：规范化后的 n-ary 加法/乘法；
- `SCEVAddRecExpr`：相对于某个 Loop 的 start/step 递推；
- `SCEVCouldNotCompute`：分析失败。

主要查询：

- `getSCEV` 和 `getSCEVAtScope`：将值表示为当前或指定循环作用域中的 SCEV；
- `getTripCount`/`getConstantTripCount`：获得循环迭代次数；
- `getAddRecForLoop`：提取指定循环的 add recurrence；
- `getLinearizedGEP`：把数组 GEP 的多维索引线性化为元素偏移和 shape；
- `isLoopInvariant`：判断表达式是否不随某循环变化。

分析通过 `nodes_` 管理 SCEV 节点，用结构化 key 复用等价节点，并以
`value_cache_` 缓存 Value 到 SCEV 的映射。递归访问使用 `visiting_` 检测环；不能
安全构造的表达式返回 unknown 或 could-not-compute。

### 4.2 RangeAnalysis

实现：[`rangeAnalysis.hpp`](../src/include/mid/analysis/rangeAnalysis.hpp)、
[`rangeAnalysis.cpp`](../src/mid/analysis/rangeAnalysis.cpp)。

`IntRange` 有四种状态：

- invalid：没有有效结果；
- top：有效但上下界未知；
- bottom：不可达/空集合；
- bounded：`lower..upper` 闭区间。

`getRange` 的主要推导路径包括：

1. 常量和 intrinsic 的直接范围；
2. add/sub/mul/div/rem 等二元运算的 checked bounds；
3. zext、icmp、select 和 PHI；
4. 函数调用的返回范围和参数范围；
5. SCEV 作为循环值的 fallback；
6. load 通过 memory facts 推导已知上界。

### 控制流事实

RangeAnalysis 从条件分支的两个后继分别计算可达集合，只把某一侧独占区域对应的条件
记录为 `PredicateFact`；`applyFacts` 将当前 block 已知的比较条件收窄到查询值上。
因此同一个值在不同 block 上可以有不同范围。`getPredicateResult` 只有在区间足以证明谓词时才返回
`AlwaysTrue` 或 `AlwaysFalse`，否则返回 `Unknown`。

### Memory facts

内存事实按 `MemoryKey` 索引，key 包含底层对象、元素类型、常量偏移和最多一个符号
索引及其 scale。分析在 CFG 上传播 `MemoryFactSet`，遇到可能 alias 的 store 时通过
BasicAA 清除受影响的 pointer/element facts。多前驱合流使用 meet，不能保留所有路径
都不满足的事实。

### Call 和 modulo summary

`getCallRange` 会查询 callee 的返回值范围，并防止递归 RangeAnalysis 无限重入。
对于规范化的 `value % modulus` 返回值，RangeAnalysis 还维护函数级 summary，记录：

- 可证明的 modulus；
- 返回值是否非负；
- 哪些参数必须先满足 non-negative 条件；
- 所有调用点是否满足这些条件。

因此 call range 可能比普通的局部表达式推导更精确，但只在函数返回形态和所有调用点
都满足约束时生效。

`isKnownNonNegative` 和 `getGEPOffsetRange` 是循环向量化、并行化、归约和地址安全
检查常用的辅助查询。

调试环境变量：`DEBUG_RANGE_ANALYSIS=1`。

### 4.3 AffineAnalysis

实现：[`affineAnalysis.hpp`](../src/include/mid/analysis/affineAnalysis.hpp)、
[`affineAnalysis.cpp`](../src/mid/analysis/affineAnalysis.cpp)。

`AffineExpr` 表示：

```text
constant + Σ(coeff[iv] * iv)
```

当前可识别的值形态是常量、LoopInfo 识别的 canonical IV、add、sub、一侧为常量
的 mul，以及常量位数的左移。分析先通过内部 ScalarEvolution 转换表达式；当
SCEV 对移位或包含移位的复合算术返回 unknown 时，再按同一套仿射规则递归展开
IR 算术。遇到 load、未知 call、非线性乘法或不能证明的值返回 invalid。

`provablyIndependentOfIV` 沿 use-def 反向检查值是否到达目标 IV。常量、参数和 global
可判定独立，load/间接下标默认判定相关；循环环路用 visited 集合截断。该结果被
DependenceAnalysis 和 CostModel 用于区分“索引存在但不随当前 IV 变化”的情况。

## 5. 访存依赖和归约分析

### 5.1 LoopAccessAnalysis

实现：[`loopAccessAnalysis.hpp`](../src/include/mid/analysis/loopAccessAnalysis.hpp)、
[`loopAccessAnalysis.cpp`](../src/mid/analysis/loopAccessAnalysis.cpp)。

`collect(Loop*)` 收集循环内：

- 所有 load/store 对应的 `LoopMemoryAccess`；
- memory instructions 和 GEP；
- 所有 GEP；
- 是否包含 call、store。

`isAffineOverAncestorIVs` 检查 GEP 是否可以仅使用当前循环祖先的归纳变量表示。
`isGlobalOrArgument` 和 `innermostArrayDim` 为内存归约和循环交换提供 base 分类与
数组维度信息。

### 5.2 DependenceAnalysis

实现：[`dependenceAnalysis.hpp`](../src/include/mid/analysis/dependenceAnalysis.hpp)、
[`dependenceAnalysis.cpp`](../src/mid/analysis/dependenceAnalysis.cpp)。

`test(acc1, acc2)` 的步骤是：

1. 提取访问的 GEP 和底层 base；
2. 使用 BasicAA/ArgumentAliasAnalysis 判断 `NoAlias/MustAlias/MayAlias`；
3. 对共同嵌套循环建立两份独立迭代变量；
4. 对仿射下标建立地址相等方程；
5. 用 GCD test 排除不可能的整数解；
6. 对有界循环使用 Banerjee 区间和三角迭代域顶点估计方向；
7. 无法精确求解时返回 `DIR_ANY`。

方向从外层到内层记录为：

```text
DIR_EQ  = 同一循环层的同一迭代
DIR_LT  = 第二次访问位于更后迭代
DIR_GT  = 第二次访问位于更前迭代
DIR_ANY = 无法证明方向
```

`loopCarriesDependence` 将非 `DIR_EQ` 的相依对视为该层携带依赖；无法求方向也
保守视为携带。因此 `isLoopParallel` 只在没有循环携带依赖时返回 true。

`getConstantDistance(source, sink, nest)` 是 wavefront 变换使用的更严格接口。它只在
各维仿射系数一致、且每个循环距离都能由独立的单位系数方程精确解出时返回
`Exact`；不同底层对象返回 `NoDependence`，其余情况均为 `Unknown`。返回向量采用
`sink iteration - source iteration`，循环层次按从外到内排列。这个接口不会用
方向区间冒充精确距离，因此不能求解的耦合方程仍会保守拒绝。

`setArgAlias` 是可选的过程间 alias oracle；`setInductionOverride` 允许已经由其它
变换验证过、但没有写入 LoopInfo legacy 字段的控制 IV 参与方向分析。

### 5.3 ReductionAnalysis

实现：[`reductionAnalysis.hpp`](../src/include/mid/analysis/reductionAnalysis.hpp)、
[`reductionAnalysis.cpp`](../src/mid/analysis/reductionAnalysis.cpp)。

`detectScalarExpandableNest` 识别内层循环向外层每个迭代写一个 reduction cell 的
形态，记录：

- reduction PHI 的初值和 latch 值；
- 最终 store、store GEP 和 memory base；
- 内层数组维度；
- 内外层循环界以及循环体 GEP。

该分析不等于“可以直接并行化”。`isScalarExpansionMemoryLegal` 还要检查 base 是
global/argument、下标维度和 affine 访问是否满足 scalar expansion 的内存独立性。
无法证明 store 之间互不影响时必须拒绝。

### 5.4 RecurrenceAnalysis

实现：[`recurrenceAnalysis.hpp`](../src/include/mid/analysis/recurrenceAnalysis.hpp)、
[`recurrenceAnalysis.cpp`](../src/mid/analysis/recurrenceAnalysis.cpp)。

该分析在 ScalarEvolution 之上识别：

```text
value = coeff * iv + constant
```

以及由多个引用组合出的 accumulator step。`checkedAdd`、`checkedSub`、`checkedMul`
和 `fitsI32` 防止闭式计算在宿主机 `long long` 上得到一个无法表示的 i32 结果。
`computeAffineSumClosedForm` 只有在初始化值、step 和迭代次数都能安全计算时才成功。

### 5.5 ModuloRecurrenceAnalysis

实现：[`moduloRecurrenceAnalysis.hpp`](../src/include/mid/analysis/moduloRecurrenceAnalysis.hpp)、
[`moduloRecurrenceAnalysis.cpp`](../src/mid/analysis/moduloRecurrenceAnalysis.cpp)。

这是一组 namespace 级查询，不由 `AnalysisManager` 缓存。它处理的不是任意取模，
而是循环携带状态的下列形式：

```text
next = (state + term0 - term1 + ...) srem positive_constant
```

`analyze` 从 `srem` 的 dividend 向上展开位于指定 update blocks 中的 add/sub 链。
展开结束时，`state` 的总系数必须恰好为 `+1`；每个其余项都必须不依赖 state。这样
得到的 `Recurrence` 保存 state、remainder、正 modulus、带符号的 contribution terms
以及完整 update chain。比如 `state - x + y` 会保留为两个符号相反的 term，而不是先
重排成可能改变 i32 行为的新表达式。

后续变换还需要两层额外证明：

- `hasPrivateUpdateChain` 要求 update chain 除了回写 state phi 以外没有额外循环内
  use；处理 live-out 时调用方可以显式允许循环外 use。
- `inferContributionBounds` 要求 contribution 不依赖其它 loop-carried PHI；指定的
  induction state 可以出现。每个 term 必须能由 `inferBounds` 得到范围。

`inferBounds` 只接受能保持 signed-i32 非溢出解释的子集：常量、PHI 合流、zext、
正常量 `srem`、非负 mask、add/sub、常量乘、常量左移、正除数的 sdiv 和 select。
递归深度、值环、32 位以上整数或中间界超出 i32 都会失败。失败的含义是“不能改变
原标量操作顺序”，不是值域为空。

获得 contribution range 后，`advanceBounds` 将范围推进若干次；
`proveNoI32UpdateWrap` 以初始 state 和 `[-modulus + 1, modulus - 1]` 的已归一化
范围共同作为起点，证明一次更新的加减不会发生 i32 wrap。`needsAtMostOneCorrection`
进一步检查更新前后的范围严格落在 `(-2M, 2M)` 内，使延迟取模或合并多步时只需要
至多一次正/负修正。

当前调用方有三类：

- `instCombineMulDivRem` 在安全条件满足时保留 loop-carried remainder，等待后续
  LoopUnroll 合并多步后再做 bounded correction；
- `LoopModuloDelay` 用 `proveNoI32UpdateWrap` 作为将循环内 srem 延后的正确性门槛；
- `LoopUnroll` 根据展开因子推进 prefix/final bounds，只有每段都至多需要一次修正时
  才生成合并后的模递推。

### 5.6 SummableExpressionAnalysis

实现：[`summableExpressionAnalysis.hpp`](../src/include/mid/analysis/summableExpressionAnalysis.hpp)、
[`summableExpressionAnalysis.cpp`](../src/lib/mid/analysis/summableExpressionAnalysis.cpp)。

`SummableExpressionAnalysis` 为 LoopRepFold 提供一个
可组合的表达式摘要。成功时 `LinearFloorExpression` 表示：

```text
(linearMultiplier * u
 + quotientMultiplier * ((divisionMultiplier * u) / divisor)
 + constant) % modulus
```

其中 `u` 可以直接是 induction PHI，也可以是两个仿射式之间的单个 signed min/max
选择。后一种情况下，摘要会记录左右斜率、常数项以及 true edge 选择哪一侧，供调用方
在闭式展开时重建原选择关系。

分析从带正整数 modulus 的 remainder 开始。它会扁平化 dividend 中的 add/sub，接受：

- induction 或 selection basis 的线性项；
- 常量乘、常量左移组成的线性项；
- 至多一个正除数的常量倍数整除项，以及该商的常量倍数；
- 常量偏移。

仿射斜率、整除分子/商系数和除数都有明确上限，所有系数和常数累加都使用检查过的
i32 范围。分析还会尝试识别 `select` 配合有序 signed compare 的 min/max 形态，以及
一层或多层冗余的 `max(i, C - i)` 反射式。两个分支不是同一仿射变量、比较谓词不受
支持、出现第二个 floor term、线性系数为负或中间系数越界时都会退出。

`LoopRepFold::matchSummableContribution` 直接将这个摘要复制到它的
`SummableModularRecurrenceMatch`。因此该分析只负责说明单次 contribution 的结构；
循环控制、累积 state、范围检查和最终的闭式 IR 生成仍由 LoopRepFold 负责。

## 6. 循环交换、向量化与成本分析

### 6.1 CostModel

实现：[`costModel.hpp`](../src/include/mid/analysis/costModel.hpp)、
[`costModel.cpp`](../src/mid/analysis/costModel.cpp)。

`strideAlong(gep, iv)` 使用 AffineAnalysis 展开 GEP 的每个索引，再从嵌套
`PointerType/ArrayType` 计算维度大小，得到 IV 每增加一单位时的字节跨度。无法得到
数组层级、非仿射下标或静态 stride 时返回 `-1`。

`totalStride` 对一组 GEP 累加 stride 绝对值。LoopInterchangeAnalysis 用
`before/after` 两个总 stride 估计 cache 行为：只有 cost 已知且 after 小于 before
时才认为变换有收益。该模型是启发式，不是硬件 cache 模拟器。

### 6.2 LoopInterchangeAnalysis

实现：[`loopInterchangeAnalysis.hpp`](../src/include/mid/analysis/loopInterchangeAnalysis.hpp)、
[`loopInterchangeAnalysis.cpp`](../src/mid/analysis/loopInterchangeAnalysis.cpp)。

`isInterchangeLegal` 使用 DependenceAnalysis 检查交换后是否会把合法方向向量
`(>, <)` 反转为非法形式。`estimateCost` 比较交换前后的内层 IV stride。

两个候选分析接口返回结构化 reason，而不是只返回 bool：

- `analyzeParallelSink`：寻找外层有携带依赖、内层可并行且交换后有收益的候选；
- `analyzeParallelFloat`：寻找可将并行内层提升到更有利位置的单子循环嵌套。

常见拒绝原因包括 null loop、无子循环、缺少 canonical IV、pre/latch/exit 不唯一、
存在 scalar reduction、已有并行性、非法依赖、stride 未知和不盈利。

### 6.3 LoopVectorizationAnalysis

实现：[`loopVectorizationAnalysis.hpp`](../src/include/mid/analysis/loopVectorizationAnalysis.hpp)、
[`loopVectorizationAnalysis.cpp`](../src/mid/analysis/loopVectorizationAnalysis.cpp)。

这是 transformation-independent 的 plan 构造器，默认 vector width 为 4。`buildPlan`
按以下阶段建立 `Plan`：

1. 检查 canonical header/body/latch/exit 形态；
2. 查找 canonical signed-less-than unit-stride induction；
3. 识别固定步长 pointer recurrence；
4. 将每个 load/store 分类为 induction GEP、pointer recurrence 或 uniform；
5. 检查 i32/f32 元素、支持的算术和 live-out；
6. 检查循环携带依赖、跨 lane 依赖和必要的 runtime memory checks；
7. 使用 VectorizationCostModel 判断是否值得向量化。

`MemoryAccess` 保存 widened GEP 的 varying dimension、IV offset、address group 和
underlying object。无法分类的 memory access 是 legality failure，不会让 emitter
静默退回 scalar。`Plan` 还保存 scalar/vector/setup cost、最小盈利 trip count、
live vector 数量和是否为 rotated single-block loop。

当前实现针对固定的 i32/f32 向量表示和 AArch64/A53 合法指令，不支持的操作或类型
直接给出拒绝 reason。

### 6.4 VectorizationCostModel

实现：[`vectorizationCostModel.hpp`](../src/include/mid/analysis/vectorizationCostModel.hpp)、
[`vectorizationCostModel.cpp`](../src/mid/analysis/vectorizationCostModel.cpp)。

模型提供：

- `VectorWidth = 4`；
- scalar/vector instruction cost；
- splat、runtime check、address group 的 setup cost；
- vector loop control cost；
- 最大 live vector 数；
- 根据 scalar lane cost、vector part cost、setup 和 unroll factor 计算最小盈利
  trip count。

它只服务于“是否值得尝试”的决策，不保证最终后端指令数量与估算完全一致。

## 7. 调试与使用约定

### 常用环境变量

| 环境变量 | 作用 |
| --- | --- |
| `DEBUG_ANALYSIS_MANAGER=1` | 输出分析 cache hit/miss/invalidate |
| `DEBUG_BASIC_AA=1` | 输出 BasicAA 调试信息 |
| `DEBUG_RANGE_ANALYSIS=1` | 输出 RangeAnalysis 查询和范围 |
| `LOOP_VERIFY_STRICT=1` | 循环规范形违例直接终止 |

### 使用分析的基本流程

```cpp
LoopInfo &LI = AM.getLoopInfo(function);
ScalarEvolution &SE = AM.getScalarEvolution(function);
RangeAnalysis &RA = AM.getRangeAnalysis(function);
```

如果需要组合循环访存分析：

```cpp
AffineAnalysis affine(LI);
DependenceAnalysis dependence(LI, affine);
LoopAccessAnalysis access(affine);
CostModel cost(affine);
LoopInterchangeAnalysis interchange(dependence, access, cost);
```

变换修改 CFG、PHI、GEP、内存或函数调用后，必须通过正确的
`PreservedAnalyses` 返回值通知 AnalysisManager。不能跨失效继续保存旧的 `Loop*`、
SCEV 或 range result。尤其是以下修改通常至少会使 LoopInfo/SCEV 失效：

- 增删循环块或改变 backedge；
- 改写 induction PHI 或 loop bound；
- 交换、融合、分裂或旋转循环；
- 删除或移动会影响 alias/mod-ref 的 store、call。

### 失败结果的解释

- `IntRange::top()`：不是“值任意正确”，而是当前分析没有可用上下界；
- `DIR_ANY`：不代表没有依赖，而是方向无法证明；
- `MayAlias`：可能别名，任何依赖消除都必须停止；
- unknown function/argument：不能依据未分析的调用点假设纯函数或 no-capture；
- unknown cost：只能跳过 profitability 判断，不能当作盈利。

## 8. 文件索引

### 接口与 header-only 工具

- [`analysisManager.hpp`](../src/include/mid/analysis/analysisManager.hpp)
- [`preservedAnalyses.hpp`](../src/include/mid/analysis/preservedAnalyses.hpp)
- [`basicAliasAnalysis.hpp`](../src/include/mid/analysis/basicAliasAnalysis.hpp)
- [`argumentAliasAnalysis.hpp`](../src/include/mid/analysis/argumentAliasAnalysis.hpp)
- [`lazyValueInfo.hpp`](../src/include/mid/analysis/lazyValueInfo.hpp)
- [`constantEvaluator.hpp`](../src/include/mid/analysis/constantEvaluator.hpp)
- [`valueFacts.hpp`](../src/include/mid/analysis/valueFacts.hpp)
- [`loopInfo.hpp`](../src/include/mid/analysis/loopInfo.hpp)
- [`loopUtils.hpp`](../src/include/mid/analysis/loopUtils.hpp)
- [`loopVerify.hpp`](../src/include/mid/analysis/loopVerify.hpp)
- [`scalarEvolution.hpp`](../src/include/mid/analysis/scalarEvolution.hpp)
- [`rangeAnalysis.hpp`](../src/include/mid/analysis/rangeAnalysis.hpp)
- [`affineAnalysis.hpp`](../src/include/mid/analysis/affineAnalysis.hpp)
- [`dependenceAnalysis.hpp`](../src/include/mid/analysis/dependenceAnalysis.hpp)
- [`loopAccessAnalysis.hpp`](../src/include/mid/analysis/loopAccessAnalysis.hpp)
- [`reductionAnalysis.hpp`](../src/include/mid/analysis/reductionAnalysis.hpp)
- [`recurrenceAnalysis.hpp`](../src/include/mid/analysis/recurrenceAnalysis.hpp)
- [`moduloRecurrenceAnalysis.hpp`](../src/include/mid/analysis/moduloRecurrenceAnalysis.hpp)
- [`summableExpressionAnalysis.hpp`](../src/include/mid/analysis/summableExpressionAnalysis.hpp)
- [`costModel.hpp`](../src/include/mid/analysis/costModel.hpp)
- [`loopInterchangeAnalysis.hpp`](../src/include/mid/analysis/loopInterchangeAnalysis.hpp)
- [`loopVectorizationAnalysis.hpp`](../src/include/mid/analysis/loopVectorizationAnalysis.hpp)
- [`vectorizationCostModel.hpp`](../src/include/mid/analysis/vectorizationCostModel.hpp)
- [`functionTerminationAnalysis.hpp`](../src/include/mid/analysis/functionTerminationAnalysis.hpp)

### 实现文件

- [`analysisManager.cpp`](../src/mid/analysis/analysisManager.cpp)
- [`basicAliasAnalysis.cpp`](../src/mid/analysis/basicAliasAnalysis.cpp)
- [`argumentAliasAnalysis.cpp`](../src/mid/analysis/argumentAliasAnalysis.cpp)
- [`lazyValueInfo.cpp`](../src/mid/analysis/lazyValueInfo.cpp)
- [`loopInfo.cpp`](../src/mid/analysis/loopInfo.cpp)
- [`loopVerify.cpp`](../src/mid/analysis/loopVerify.cpp)
- [`scalarEvolution.cpp`](../src/mid/analysis/scalarEvolution.cpp)
- [`rangeAnalysis.cpp`](../src/mid/analysis/rangeAnalysis.cpp)
- [`affineAnalysis.cpp`](../src/mid/analysis/affineAnalysis.cpp)
- [`dependenceAnalysis.cpp`](../src/mid/analysis/dependenceAnalysis.cpp)
- [`loopAccessAnalysis.cpp`](../src/mid/analysis/loopAccessAnalysis.cpp)
- [`reductionAnalysis.cpp`](../src/mid/analysis/reductionAnalysis.cpp)
- [`recurrenceAnalysis.cpp`](../src/mid/analysis/recurrenceAnalysis.cpp)
- [`moduloRecurrenceAnalysis.cpp`](../src/mid/analysis/moduloRecurrenceAnalysis.cpp)
- [`summableExpressionAnalysis.cpp`](../src/mid/analysis/summableExpressionAnalysis.cpp)
- [`costModel.cpp`](../src/mid/analysis/costModel.cpp)
- [`loopInterchangeAnalysis.cpp`](../src/mid/analysis/loopInterchangeAnalysis.cpp)
- [`loopVectorizationAnalysis.cpp`](../src/mid/analysis/loopVectorizationAnalysis.cpp)
- [`vectorizationCostModel.cpp`](../src/mid/analysis/vectorizationCostModel.cpp)
- [`functionTerminationAnalysis.cpp`](../src/mid/analysis/functionTerminationAnalysis.cpp)
