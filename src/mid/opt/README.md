# Mid-Opt 实验记录

`src/mid/opt` 放的是中端里不依赖某一种循环变换的优化。这里既有早期的
SSA 化和控制流收缩，也有内联、调用图处理、库函数 idiom 识别，以及最后一段
基本块级的 SLP 向量化。循环规范化、循环交换、循环并行和循环向量化的实现位于
`src/mid/transform/loop`；本文件只在说明调度关系时提到它们。

每个 pass 都有自己的匹配形状、别名前提和退出条件。下面记录的是源码里实际发生的改写，以及用 IR dump 能观察到的结果。没有命中的程序应当保持原样。

## 1. 观察方式

最直接的入口是：

```bash
build/compiler -O1 --dump-ir -c input.sy -o /tmp/out.ir
```

`--dump-ir` 会在 pass 前后打印 IR，适合确认某个模式到底是在谁那里消失的。
循环相关改写还可以配合 `--verify-ir`；要把循环形态违例变成第一个出错点，设置：

```bash
LOOP_VERIFY_STRICT=1 build/compiler -O1 --verify-ir -c input.sy -o /tmp/out.ir
```

本目录可用的调试开关如下。它们只用于观察，不改变 pass 的匹配规则。

| 开关 | 观察内容 |
| --- | --- |
| `DEBUG_DUMP_ANALYSIS=1` | 在循环变换前插入 `AnalysisDump` |
| `PROFILE_PASSES=1` | PassManager 的单 pass 用时 |
| `DEBUG_LOOP_PIPELINE=1` | 重复循环组中的 IR dump |
| `LOOP_PIPELINE_MAX_ROUNDS=N` | 覆盖循环重复组的轮数上限 |
| `DEBUG_EARLY_CSE_AA=1` | EarlyCSE 的内存版本和别名判断 |
| `DEBUG_CFG_SELECT_COST=1` | CFGSimplify 将 diamond 变 select 的成本 |
| `DEBUG_REASSOCIATE_LINEAR=1` | Reassociate 的线性式重建 |
| `DEBUG_IDIOM=1` | memset/memcpy idiom 的拒绝或命中信息 |
| `DEBUG_PHI_OP_SINK=1` | PhiOpSink 的 sink 决策 |
| `DEBUG_PHI_OP_SINK_LIMIT=N` | 限制 PhiOpSink 的调试输出 |
| `DEBUG_SLP_VECTORIZE=1` | SLP pack 搜索和发射过程 |
| `DEBUG_SPLIT_GEP=1` | SplitGEP 的 row-base 外提 |
| `GVN_DISABLE=1` | 临时关闭 GVN，便于与前一阶段 IR 对照 |

## 2. PassManager 与 O1 顺序

接口在 [`pass.hpp`](../../include/mid/opt/pass.hpp) 和
[`passManager.hpp`](../../include/mid/opt/passManager.hpp)，实现在
[`passManager.cpp`](passManager.cpp)。一个 pass 至少实现无参分析版本的
`execute(Module*)`。需要复用分析缓存时，可以重载：

```cpp
PreservedAnalyses execute(Module *module, AnalysisManager &AM)
```

约定是“没有改动 IR 就返回 `all()`”。默认实现不知道 pass 做了什么，按最保守方式
返回 `none()`。返回值交给 `AnalysisManager` 清除 LoopInfo、SCEV、LVI 或 BasicAA
缓存，详细规则见 [`src/mid/analysis/README.md`](../analysis/README.md)。

### 2.1 重复组

`PassManager::addRepeatGroup` 记录一个连续 pass 区间。每一轮结束时，只有那些
`convergenceRelevant()` 为 true 的 pass 仍报告改动，组才继续执行。清理型 pass 如
`DeadCodeEliminate`、`SCCP`、`InstCombine` 不参与这个收敛判定，否则它们往往会因
重新暴露的小机会把轮数用满。

循环重复组最多执行八轮。每轮会比较指令数与入组前的数量；异常增长会停止后续轮次。
当 pass 宣称已经建立 LoopSimplify 或 LCSSA 所要求的结构时，PassManager 调用
`LoopVerify` 做相应级别的检查。严格检查只在 `LOOP_VERIFY_STRICT` 已开启且当前
结构确实应该满足时执行，避免把普通 CFG pass 的中间状态误报成循环变换错误。

### 2.2 清理组合

`main.cpp` 中的四个组合函数固定了清理次序：

| 组合 | 组成 |
| --- | --- |
| canonical cleanup | DCE、LinearBlockMerge、SCCP、InstCombine、DSE、DCE、LinearBlockMerge |
| correlated cleanup | CorrelatedValuePropagation、JumpThreadingLite、DCE、CFGSimplify、DCE |
| deep cleanup | canonical cleanup、CFGSimplify、canonical cleanup |
| scalar cleanup | Reassociate、canonical cleanup、LocalCopyPropagation、canonical cleanup、CodeSink |

这里的重复并非装饰。比如 SCCP 折叠分支以后，DCE 才能删掉死块；块合并后原本跨块的
同一表达式才会落进 EarlyCSE/GVN 的可见范围。反过来，若先做 DSE 再改 CFG，也可能
把本来不可达的 store 留到下一轮。

### 2.3 O1 的几段工作

O1 先进行 DCE、CFGSimplify、Mem2Reg、EarlyCSE 与基础清理，然后处理尾递归、
LoopDistribution、代数式和内联。内联之后再次执行 Mem2Reg/Reassociate/清理，使
clone 进来的局部 alloca 和算术 DAG 回到常规 SSA 形状。

循环重复组结束后，`src/mid/opt` 自己实现的 pass 会穿插在如下位置：

- `PhiOpSink` 位于 LoopRotate 与 LICM 之间；
- `IfConversion`、`IdiomRecognize`、`SplitGEP` 与循环变换一起为向量化准备形状；
- `SLPVectorize` 在第二次 LoopVectorize 之后运行，补基本块内而非循环级的 SIMD；
- `GVN`、CodeSink、TailDuplication、UnifyExitNodes、LateValueCleanup 在 late
  target-independent 阶段清理由前面变换带来的冗余；
- `AutoMemoization` 只在模块预扫描存在候选时注册；`TailCallOpt` 最后给后端留 tail
  标记。

## 3. SSA、常量和冗余表达式

### 3.1 Mem2Reg

实现：[`mem2reg.hpp`](../../include/mid/opt/mem2reg.hpp)、
[`mem2reg.cpp`](mem2reg.cpp)。

Mem2Reg 的入口按函数工作。它先收集可提升的 `alloca`，然后按由简单到复杂的顺序处理：

1. 删除没有使用的 alloca；
2. 只有一个 store 的 alloca 直接以 stored value 替换 load；
3. 单 basic block 内的 alloca 按指令顺序转成 SSA；
4. 对跨块 alloca 放置 PHI，再沿支配树 rename load/store。

零初始化不是凭空假定的。需要进入尚未看到 store 的路径时，`zeroValueFor` 按类型构造
初始值；`hasStoreBeforeFirstLoad` 用块内顺序避免把后写的值倒灌给前读。

文件末尾还有一个小型 scalar replacement：对常量下标的数组 GEP，把每个元素拆成单独
alloca 后再走同一套提升流程。下标必须全是常量，元素必须是可提升的 scalar；动态下标、
地址逃逸或非标量元素不会尝试拆开。

观察点：`alloca/load/store` 会被 value、PHI 和边上的 incoming value 替代。出现新的
PHI 是正常结果，不是 Mem2Reg 失败。

### 3.2 SCCP

实现：[`sccp.hpp`](../../include/mid/opt/sccp.hpp)、[`sccp.cpp`](sccp.cpp)。

SCCP 使用 `UNDEF`、常量和 `OVERDEF` 三态 lattice。它一边沿 SSA use 更新值状态，
一边只把确定可达的 CFG edge 放进工作表。二元整数操作的常量计算使用本文件自己的
`evalBinOp`，遇到无效算术不会伪造常量。

收敛后，常量值替换对应 use；恒定条件的分支改成无条件分支；不可达块从函数中移除，
并同步删掉后继 PHI 的 incoming edge。SCCP 不负责把所有留下的空块合并，因此它总是
和 LinearBlockMerge、DCE 放在一组。

### 3.3 EarlyCSE 与 GVN

实现：[`earlyCSE.hpp`](../../include/mid/opt/earlyCSE.hpp)、
[`earlyCSE.cpp`](earlyCSE.cpp)、[`gvn.hpp`](../../include/mid/opt/gvn.hpp)、
[`gvn.cpp`](gvn.cpp)，公共签名工具在 [`cse_common.hpp`](../../include/mid/opt/cse_common.hpp)。

两者都用表达式签名做值编号。签名包含 opcode、结果类型、比较谓词和经过已有 value
number 替换的 operands；可交换整数/浮点操作会先稳定 operand 顺序，常量会被
`canonical_constants` 归一化。

EarlyCSE 是一次 DFS 中的局部早期消除。它维护到达当前块的 value-number 表和最近的
memory modification；load 只有在中间没有可能修改同一位置的 store/call 时才复用。
循环体使用 sentinel 隔开，以免把一次迭代的可用 load 误带入下一迭代。`DEBUG_EARLY_CSE_AA`
可看到这部分 alias 判断。

GVN 在支配树上做更完整的全局编号。除普通纯表达式外，它还处理：

- PHI 所有 incoming 已归到同一个值时的折叠；
- BasicAA 证明 pure/read-only 的调用；
- 内联、展开或 IV strength reduction 产生的重复 GEP；
- store/call 后对受影响 load 编号的失效。


### 3.4 Reassociate 与 LocalCopyPropagation

实现：[`reassociate.hpp`](../../include/mid/opt/reassociate.hpp)、
[`reassociate.cpp`](reassociate.cpp)、
[`localCopyPropagation.hpp`](../../include/mid/opt/localCopyPropagation.hpp)、
[`localCopyPropagation.cpp`](localCopyPropagation.cpp)。

Reassociate 有两条路径。普通路径按 RPO rank 排序加法/乘法树，把常量和低 rank 值靠近
一起，便于下一次 CSE。另一条线性路径把 i32 add/sub/mul-by-constant 收成带系数的
`LinearForm`，比较旧树和重建树的成本，只在重建更便宜时替换。这里必须保持 i32 wrap
语义；不满足可线性化条件的节点留在原树里。`DEBUG_REASSOCIATE_LINEAR` 能打印线性
重建的选择。

LocalCopyPropagation 很小，只消除本地 identity copy，例如可安全移除的 `x + 0`、
`x * 1` 或等价拷贝。它不跨 CFG 追踪值，因此被安排在 Reassociate 和 canonical cleanup
之间，作为让后者少处理一层中间值的整理。

### 3.5 DeadCodeEliminate、DeadStoreEliminate 与 LateValueCleanup

实现：[`deadCodeEliminate.hpp`](../../include/mid/opt/deadCodeEliminate.hpp)、
[`deadCodeEliminate.cpp`](deadCodeEliminate.cpp)、
[`deadStoreEliminate.hpp`](../../include/mid/opt/deadStoreEliminate.hpp)、
[`deadStoreEliminate.cpp`](deadStoreEliminate.cpp)、
[`lateValueCleanup.hpp`](../../include/mid/opt/lateValueCleanup.hpp)、
[`lateValueCleanup.cpp`](lateValueCleanup.cpp)。

DCE 不只删无 use 的二元运算，模块级还会从入口函数出发删除不可达函数，删除不再被
引用的 global；函数内再删没有 required effect 的指令和 trivial PHI。并行 loop body
函数通过 runtime dispatch 的引用关系特别标记，不因普通 call graph 看不见而误删。

DSE 判断一个 store 是否在任意可达读之前一定被后继 store 覆盖。实现同时计算 store
之后的前向可达块和潜在覆盖点之前的反向可达块，并使用 BasicAA 判断地址是否相同。
存在 call、load 或可能 alias 的 store 时进行保守性的保留，不会跨不确定的
控制流做推测删除。

LateValueCleanup 放在较晚的位置，只看“单前驱块里与前驱已经出现过的纯值表达式”。
它重新检查 opcode、比较谓词和 operand 相等，再用前驱里的等价指令替换。这一小步专门
收内联、tail duplication 和 exit 合并后相邻块重复出来的值，不试图代替 GVN。

## 4. CFG 和路径改写

### 4.1 CFGSimplify

实现：[`CFGSimplify.hpp`](../../include/mid/opt/CFGSimplify.hpp)、
[`CFGSimplify.cpp`](CFGSimplify.cpp)。

CFGSimplify 是本目录里改 CFG 最广的一项，循环扫描直到没有新的局部机会。它做的事情
包括：折叠常量条件、删除不可达分支、合并空跳转块、合并线性块、修补 PHI incoming
edge，以及在可以证明安全时外提 loop-invariant branch。

文件后半部分专门处理 diamond：两条支路的可克隆纯指令会复制到汇合处，PHI 改为
`select`。该路径有额外条件：指令必须可投机执行，两个分支的比较/控制形状一致，且
复制成本不能大于预期收益。`DEBUG_CFG_SELECT_COST` 用来查看成本计算。含 store、call、
除法或可能产生不同行为的指令不会被这条路径克隆。

### 4.2 CorrelatedValuePropagation 与 JumpThreadingLite

实现：[`correlatedValuePropagation.hpp`](../../include/mid/opt/correlatedValuePropagation.hpp)、
[`correlatedValuePropagation.cpp`](correlatedValuePropagation.cpp)、
[`jumpThreadingLite.hpp`](../../include/mid/opt/jumpThreadingLite.hpp)、
[`jumpThreadingLite.cpp`](jumpThreadingLite.cpp)。

CorrelatedValuePropagation 从 `LazyValueInfo` 取得当前块或 edge 上的条件事实。它会折叠
确定的 select、icmp 和 trivial PHI，也会把已确定的条件分支改写为无条件分支。因为
每次 CFG 修改都会破坏原有 LVI 语境，pass 通过 AnalysisManager 按函数重新查询分析。

JumpThreadingLite 针对“到达中间块时条件已知”的边。它为 predecessor edge 收集
`BoolFactMap`/`ICmpFactMap`，对中间块的比较做有限 substitution；若结果唯一，就将该
edge 直接改到选定后继，并预先规划 successor PHI 的修复。循环 header 和可能破坏
回边语义的 edge 有专门拒绝条件，不能因为局部条件看起来确定就绕过 loop control。

`branchFactUtils.cpp` 是两者共享的布尔事实工具。它支持 i1 值、icmp、比较取反以及
`(bool == 0/1)` 这类包裹形式。`cfgUtils.cpp` 的 `removeUnreachableBlocks` 则以
terminator operands 为真相重算可达性，再删除死块与 PHI incoming，避免依赖某个 pass
暂时没维护好的 `succ_bbs_` 链表。

### 4.3 线性块、尾部和出口

实现：[`linearBlockMerge.hpp`](../../include/mid/opt/linearBlockMerge.hpp)、
[`linearBlockMerge.cpp`](linearBlockMerge.cpp)、[`tailDuplication.hpp`](../../include/mid/opt/tailDuplication.hpp)、
[`tailDuplication.cpp`](tailDuplication.cpp)、[`unifyExitNodes.hpp`](../../include/mid/opt/unifyExitNodes.hpp)、
[`unifyExitNodes.cpp`](unifyExitNodes.cpp)。

LinearBlockMerge 合并唯一前驱/唯一后继的线性块，并替换后继 PHI 的 predecessor。它不
承担不可达块删除，通常由前后的 CFGSimplify/DCE 完成。

TailDuplication 只复制非常小的尾块，`MAX_DUP_INSTS` 为 3，且所有待复制指令都必须
可克隆。目的不是普遍展开 CFG，而是让多个前驱直接拥有末尾的简单计算和跳转，随后
比较、跳转和 PHI 更容易被折叠。

UnifyExitNodes 将有多个 return/exit 的函数改到统一出口，使后续 late cleanup、
memoization 和 tail-call 识别不必对每个 return 块重复处理。该 pass 只处理出口结构，
不会把任意两个语义不同的 block 合并。

### 4.4 CodeSink、PhiOpSink 与 IfConversion

实现：[`codeSink.hpp`](../../include/mid/opt/codeSink.hpp)、
[`codeSink.cpp`](codeSink.cpp)、[`phiOpSink.hpp`](../../include/mid/opt/phiOpSink.hpp)、
[`phiOpSink.cpp`](phiOpSink.cpp)、[`ifConversion.hpp`](../../include/mid/opt/ifConversion.hpp)、
[`ifConversion.cpp`](ifConversion.cpp)。

CodeSink 在所有 use 汇聚到一个更深或更晚的 block 时下沉纯指令。它检查 operands 在
目标位置支配可用，并比较 LoopInfo 给出的 loop depth，避免把原本只算一次的值错误地
移入更深循环。

PhiOpSink 针对 header 中“多个 incoming 做同一种二元操作，再在 PHI 汇合”的形状。
它将可逐边计算的操作推到 predecessor，减少循环头重复算术。实现会检查操作可交换性、
operand 对应关系、支配性和 exit/live-out 使用；调试输出可用
`DEBUG_PHI_OP_SINK` 控制。

IfConversion 的目标是四块内层循环：header 做退出判断，body 做 if 判断，true block
只含可投机的算术/load，latch 做 PHI、store 和 IV 更新。命中后 true block 的值进入 body，
PHI 用 `select` 表达，四块压成三块。store/call/div、不能投机的指令和不符合的 PHI
都会使转换退出。这个限制是刻意的，转换后的三块形态正好是 LoopVectorize 接受的
上限之一。

## 5. 调用图、全局和递归

### 5.1 InlineExpand

实现：[`inlineExpand.hpp`](../../include/mid/opt/inlineExpand.hpp)、
[`inlineExpand.cpp`](inlineExpand.cpp)。

InlineExpand 从调用点出发，检查 callee 是否可达、是否自递归、调用
是否位于循环内，以及 clone 后的 weighted instruction cost。头文件中的阈值体现当前
取舍：普通 inline threshold 为 80，极小函数的 always threshold 为 6；递归候选还有
单独的阈值、热点乘数和总预算。

真正内联时，调用块在 call 后拆开，callee 的 RPO blocks 克隆进 caller，参数与 basic
block 通过两张映射表重写，所有 return 汇到 continuation。新的 call site 会重新加入
工作队列，因此多层小函数可能在一次 pass 内继续展开。内联后的 alloca、PHI 和算术
不在这里立即清理，后面的 Mem2Reg、Reassociate 和 canonical cleanup 承接这些工作。

### 5.2 TailRecursionEliminate 与 TailCallOpt

实现：[`tailRecursionEliminate.hpp`](../../include/mid/opt/tailRecursionEliminate.hpp)、
[`tailRecursionEliminate.cpp`](tailRecursionEliminate.cpp)、[`tailCallOpt.hpp`](../../include/mid/opt/tailCallOpt.hpp)、
[`tailCallOpt.cpp`](tailCallOpt.cpp)。

TailRecursionEliminate 只处理 self call 已经处在 return tail 的函数。它识别直接
`call + ret`，也识别 call 后先跳到只返回结果的块；随后建立循环入口、参数 PHI 与
回跳，把递归调用改成下一轮参数更新。不是 self call、call 后仍有可观察计算或返回值
使用不符合尾位置的函数不会变换。

TailCallOpt 不消除调用。它把 `call + br ret_block` 先规范为同块的 `call + ret`，再在
CallInst 上设置 tail 标记，供 ARM64 后端在 ABI 条件满足时使用 `b` 而不是 `bl`。因此
它必须放在 UnifyExitNodes 和 memoization 之后，避免后续 CFG 改写把尾位置重新拆散。

### 5.3 GlobalScalarPromotion 与 SemanticMarkerStamp

实现：[`globalScalarPromotion.hpp`](../../include/mid/opt/globalScalarPromotion.hpp)、
[`globalScalarPromotion.cpp`](globalScalarPromotion.cpp)、
[`semanticMarkerStamp.hpp`](../../include/mid/opt/semanticMarkerStamp.hpp)、
[`semanticMarkerStamp.cpp`](semanticMarkerStamp.cpp)。

GlobalScalarPromotion 只处理整数 scalar global。函数入口把 global 读入一个局部 alloca，
函数内读写转到该镜像，所有 return 前把镜像写回 global；紧接着的 Mem2Reg 再把镜像
提升为 SSA。任何残留 call 只要不是 `FnPure`，都可能经别的函数观察 global，此时不做
提升。float global、数组和地址逃逸也在范围外。

SemanticMarkerStamp 不改 CFG 或指令序列。它把 BasicAA 已经证明的事实写成 IR semantic
flag：`FnPure`、`FnReadOnly`、`ArgReadOnly`、`ArgNoCapture`，以及满足初始化支配、
不逃逸和非循环内声明条件的 `ImmutableLoad`。标记在 inline/mem2reg 之后只跑一次，
返回 `PreservedAnalyses::all()`；它的作用是让 LICM、GVN 等后续 pass 不必每次从头
重建同一类语义判断。

### 5.4 AutoMemoization

实现：[`autoMemoization.hpp`](../../include/mid/opt/autoMemoization.hpp)、
[`autoMemoization.cpp`](autoMemoization.cpp)。

模块先用 `moduleHasAnyCandidate` 预扫描，完全没有候选就不把 pass 加入 O1。候选函数
需要是内部可调用的递归函数，参数和返回类型受限，且函数体不能依赖未冻结的全局内存。
`BasicAliasAnalysis` 用来判断读内存、逃逸和副作用；`deriveArgBound` 为参数推导可用于
表索引的界。

命中后 pass outline 原始函数体，创建 memo table 和 visited table，在 wrapper 中先做
范围检查和 cache lookup，再调用 outline 后写回结果。线性尾递归在这之前已经由 TRE
消掉，因此不会被错误地当成指数型重叠子问题。若参数界或内存不变性无法证明，保持
递归调用，不能为了缓存强行引入未经检查的数组访问。

### 5.5 RadixRecurrenceEliminate 与 BitFuncRecognize

实现：[`radixRecurrenceEliminate.hpp`](../../include/mid/opt/radixRecurrenceEliminate.hpp)、
[`radixRecurrenceEliminate.cpp`](radixRecurrenceEliminate.cpp)、
[`bitFuncRecognize.hpp`](../../include/mid/opt/bitFuncRecognize.hpp)、
[`bitFuncRecognize.cpp`](bitFuncRecognize.cpp)。

RadixRecurrenceEliminate 匹配纯 radix-2 自递归：`F(a,0)=0`，`F(a,1)=a srem M`，
递归分支由 `F(a,b/2)` 乘二取模组成，奇数分支可再加 `a` 取模。匹配是 CFG 和指令
结构匹配，不依赖函数名。若已证明 i32 加法不溢出，函数可降为 `MulMod`；否则正数 `b`
使用保持原 i32 操作顺序的 bit-walking fallback，非正数仍保留零结果。

BitFuncRecognize 更偏向布尔/位级等价。它将 32 位结果表示为 interned 的 bit expression
arena，能构造常量、源 bit、and/or/xor/not、select、移位、常数乘、部分除法和取模。对
函数 CFG 做可达性与后支配推理后，尝试恢复可用的 closed form，并重写所有对应 call
site。无法得到完整 bit-vector 或闭式不在支持集合时不改写。

## 6. 内存 idiom 与地址形状

### 6.1 IdiomRecognize 和 libFunc

实现：[`idiomRecognize.hpp`](../../include/mid/opt/idiomRecognize.hpp)、
[`idiomRecognize.cpp`](idiomRecognize.cpp)、[`libFunc.hpp`](../../include/mid/opt/libFunc.hpp)、
[`libFunc.cpp`](libFunc.cpp)。

IdiomRecognize 目前识别两类循环：连续填充和连续复制。它会先验证 counting IV、loop
header/latch/body 形态、store/load 地址的 unit element stride，以及外层二维填充时的
canonical nested shape。memset 还要求写入常量可转换为填充 byte；memcpy 需要源、目的
地址的变化形状相同，且能得出固定复制字节数。

对于匹配到的循环，pass 通过 `getOrInsertLibFunc` 请求 `memset`、`memcpy` 或 `memmove`
声明，再用 call 替换循环并修复 CFG/PHI。`getOrInsertLibFunc` 如果同名函数已经有定义，
返回空而不是把用户定义的函数当 libc 调用。不能证明范围、地址或循环形状时，原循环
完整保留。

`DEBUG_IDIOM` 特别适合看“为什么只是看上去像 memcpy 却没有被替换”：常见原因是
动态 byte count、非单位跨度、含嵌套循环或地址 base 不稳定。

### 6.2 SplitGEP

实现：[`splitGEP.hpp`](../../include/mid/opt/splitGEP.hpp)、[`splitGEP.cpp`](splitGEP.cpp)。

多维 `A[i][j]` 在中端常表现为一个 GEP，外层 `i * rowStride` 直到后端展开地址时才
显形。SplitGEP 在 loop preheader 中先建立固定行的 row-base GEP，再把循环内访问改成
以 row-base 为基址的剩余下标。它要求多维 GEP、存在可用 preheader，并且要剥离的 index
前缀在当前 loop 内不变。

该 pass 安排在 ParallelizeLoops 和 LoopVectorize 之后。前两个 pass 的匹配器依赖
原本扁平的 GEP 形状；提前拆地址反而会减少它们的识别率。SplitGEP 自身不尝试把每个
访问都拆开，任何 loop-invariant 前缀不足或类型层级不匹配都会跳过。

## 7. 基本块级 SIMD

### SLPVectorize

实现：[`slpVectorize.hpp`](../../include/mid/opt/slpVectorize.hpp)、
[`slpVectorize.cpp`](slpVectorize.cpp)。

SLP 与 loop vectorizer 的入口不同：它在一个 basic block 内从相邻 store 开始，而不是
从 induction variable 开始。当前固定 `VF=4`，面向 A53 NEON 的 `<4 x i32>` 和
`<4 x float>`。

执行分四步：

1. `findAdjacentMemoryRefs` 找到相邻 store/load；
2. `extendPackSet` 沿 use-def 链把同构的 scalar binary/load 扩展成 pack；
3. `combinePacks` 合并重叠或可连接的 pack；
4. `scheduleAndEmit` 按依赖次序发射 vector load、binary 和 store。

`isIsomorphic` 要求 opcode、类型和形状一致，`isIndependent` 排除同一 pack 内互相依赖。
发射前还检查 pack 之间是否夹着可能影响内存的指令，并通过 BasicAA 过滤可能 alias 的
load/store。只要某个标量 user 无法安全接收 vector lane，或者 pack 的数量不足以覆盖
setup 成本，`isProfitable` 会拒绝发射。


## 8. 诊断和小型辅助文件

### 8.1 AnalysisDump

实现：[`analysisDump.hpp`](../../include/mid/opt/analysisDump.hpp)、
[`analysisDump.cpp`](analysisDump.cpp)。

AnalysisDump为每个函数打印 LoopInfo、归纳变量、
访存 GEP、dependence direction、可能的参数 no-alias 事实和 live-out recurrence。
它使用临时 `AffineAnalysis`、`DependenceAnalysis`、`ArgumentAliasAnalysis`，所以输出是
一次观察快照，不是被 AnalysisManager 永久缓存的报告。

### 8.2 共享头文件

- [`branchFactUtils.hpp`](../../include/mid/opt/branchFactUtils.hpp)：布尔条件和 icmp
  事实的 key、取反与传播，供路径优化复用。
- [`cfgUtils.hpp`](../../include/mid/opt/cfgUtils.hpp)：从 terminator 重新计算可达块的
  工具，避免直接相信暂时过期的 CFG 邻接表。
- [`cse_common.hpp`](../../include/mid/opt/cse_common.hpp)：EarlyCSE/GVN 共用的常量
  归一化、表达式签名和可消除指令判断。
- [`deadCodeDelete.hpp`](../../include/mid/opt/deadCodeDelete.hpp)：兼容旧调用点的轻量
  声明；实际的 module/function 级删除逻辑在 `DeadCodeEliminate`。
- [`optPasses.hpp`](../../include/mid/opt/optPasses.hpp)：供 `main.cpp` 使用的 umbrella
  header，汇总本目录 pass 和位于 loop transform 的 pass 声明。

这些文件通常没有独立的优化实验结果，但它们决定了上层 pass 在遇到 PHI、边事实、
常量和临时 CFG 不一致时如何保持一致行为。

## 9. 维护时常见的误区

1. **改了 CFG 却没有修 PHI。** 任何删边、thread edge、合并 block 或替换 branch 的
   pass 都要同步处理后继 PHI incoming。`cfgUtils` 和 CFGSimplify 里的辅助函数可以作为
   参考。
2. **把可投机执行和纯指令混为一谈。** 纯算术未必可以提前执行；除法、load、call 和
   可能触发不同语义的指令在 IfConversion/CFGSimplify 中有额外限制。
3. **修改内存后仍保留 BasicAA/LVI/SCEV。** 保留分析的 pass 必须真的没有破坏对应
   前提。拿不准时返回 `PreservedAnalyses::none()`。
4. **把 `MayAlias` 当作没有 alias。** EarlyCSE、GVN、DSE、IdiomRecognize 和 SLP 都应
   在不确定时保守放弃。
5. **忽略流水线位置。** SplitGEP 提前运行会改变循环向量化的识别形状；全局标量提升
   必须在 InlineExpand 后、Mem2Reg 前；TailCallOpt 要等出口形态稳定。
6. **把 debug 输出当作证明。** 输出只说明某条实现路径执行过，正确性仍取决于全部
   guard、支配关系、别名判断和随后 verifier 的结果。

## 10. 文件索引

### 有 `.cpp` 实现的 pass 与辅助文件

- [`CFGSimplify.cpp`](CFGSimplify.cpp)
- [`analysisDump.cpp`](analysisDump.cpp)
- [`autoMemoization.cpp`](autoMemoization.cpp)
- [`bitFuncRecognize.cpp`](bitFuncRecognize.cpp)
- [`branchFactUtils.cpp`](branchFactUtils.cpp)
- [`cfgUtils.cpp`](cfgUtils.cpp)
- [`codeSink.cpp`](codeSink.cpp)
- [`correlatedValuePropagation.cpp`](correlatedValuePropagation.cpp)
- [`deadCodeEliminate.cpp`](deadCodeEliminate.cpp)
- [`deadStoreEliminate.cpp`](deadStoreEliminate.cpp)
- [`earlyCSE.cpp`](earlyCSE.cpp)
- [`globalScalarPromotion.cpp`](globalScalarPromotion.cpp)
- [`gvn.cpp`](gvn.cpp)
- [`idiomRecognize.cpp`](idiomRecognize.cpp)
- [`ifConversion.cpp`](ifConversion.cpp)
- [`inlineExpand.cpp`](inlineExpand.cpp)
- [`jumpThreadingLite.cpp`](jumpThreadingLite.cpp)
- [`lateValueCleanup.cpp`](lateValueCleanup.cpp)
- [`libFunc.cpp`](libFunc.cpp)
- [`linearBlockMerge.cpp`](linearBlockMerge.cpp)
- [`localCopyPropagation.cpp`](localCopyPropagation.cpp)
- [`mem2reg.cpp`](mem2reg.cpp)
- [`passManager.cpp`](passManager.cpp)
- [`phiOpSink.cpp`](phiOpSink.cpp)
- [`radixRecurrenceEliminate.cpp`](radixRecurrenceEliminate.cpp)
- [`reassociate.cpp`](reassociate.cpp)
- [`sccp.cpp`](sccp.cpp)
- [`semanticMarkerStamp.cpp`](semanticMarkerStamp.cpp)
- [`slpVectorize.cpp`](slpVectorize.cpp)
- [`splitGEP.cpp`](splitGEP.cpp)
- [`tailCallOpt.cpp`](tailCallOpt.cpp)
- [`tailDuplication.cpp`](tailDuplication.cpp)
- [`tailRecursionEliminate.cpp`](tailRecursionEliminate.cpp)
- [`unifyExitNodes.cpp`](unifyExitNodes.cpp)

### 仅在调度关系中出现的声明

[`src/include/mid/opt`](../../include/mid/opt) 中还包含 LoopSimplify、LCSSA、LICM、
LoopInterchange、ParallelizeLoops、LoopVectorize、LoopUnroll 等声明。它们的实现不在
本目录，属于 `src/mid/transform/loop`；本次记录不展开其实现细节。
