# IndVarSimplify 重做计划

## 当前进度

已完成只读 `InductionDescriptor`、分析 dump 和第一层 canonical live-out
folding。全量扫描确认目前能稳定受益的是 `dead-code-elimination-*`、
`instruction-combining-*` 和 `integer-divide-optimization-*`：这些程序中
固定 60/300 次的次级递推可以直接物化，继而删除不再有副作用的内层循环。

第二层 constant-stride normalization 和第三层 variable-stride rewrite 暂不
接入。当前没有证据表明它们能降低动态成本或解锁下游变换；尤其 `h-4-*`
只证明了 variable-stride 分析覆盖，不能证明改写有收益。按本计划的停止条件，
实现保留在第一层。

## 基线与已知事实

新实现以 `refractor/IVS` 当前工作区为唯一基线：

- 分支基点为 `e0facebba615b422701dcf050757644f39c0c74e`。
- 仅保留 `750e6446a8461213b37c7a63664ce4a90b26104f` 的 SCEV
  复杂度控制，以及 `45fbf4399c19688f45fdd7b618b65b07f580ba8f` 到
  `103d7749d26221d07b4d68c700bb6068e503db80` 的 loopFusion、README
  和后续修正。
- 不包含旧 IndVarSimplify、其配套的 canonical-only 分析限制，以及
  `9e0151a40e9e53fb6af1330f80059613178def76` 到
  `851b5b7a218149e15e8d76db0edabd007513ef7d` 的改动。
- 当前基线全量性能测试为 162/162 AC，结果记录在
  `test/results/result_performance_20260729_091928.txt`。

关键观测：

- `h-4-*` 在移除旧 IndVarSimplify 后汇编不变并命中缓存。旧实现最终已经
  拒绝 variable-stride canonicalization，因此不能把 h-4 当作已有收益。
- `h-5-*` 和 `ludcmp-*` 在新基线上会正常进入 scalarExpansion：
  `LoopDistribution` 后存在 `[1400 x i32]` 的 `scalar.expansion.tmp`。
  旧 `851b5b7` 不会生成该结构，原因是旧路线把 ReductionAnalysis 等分析
  限制为只接受 `canonicalIV`。
- 恢复 scalarExpansion 后，本机容器中的 h-5/ludcmp 时间从约
  0.725--0.729s 变为约 0.792--0.808s。这是稳定的结构变化，不应解释成
  噪声；但本机是 M2，最终 A53 效果仍需要目标机反馈。
- 新基线上的 `crypto-*` 比旧基线快约 27%--38%，`conv2d-*` 快约
  15%--44%。新 IVS 不得破坏这些结果。

## 旧路线的问题

1. 变换和分析规范混在一起。旧提交把 DependenceAnalysis、
   LoopAccessAnalysis、ReductionAnalysis、LoopDistribution 和
   ScalarExpansion 从一般的 `getInductionIV()` 改成只接受
   `canonicalIV`。这使 pass 尚未产生收益时，就先阻断了已有分析与变换。
2. variable-stride 规范化会在循环前生成除法、取余和 select，并可能把原本
   的递增 recurrence 变成循环内乘法。该变换对 h-4 没有天然收益保证。
3. 为支持 IVS 而扩大 RangeAnalysis 的全局推理范围，实际影响了 crypto 和
   conv2d；后续提交又反复撤销这些改动，说明证明边界没有独立稳定下来。
4. 旧流水线每轮连续运行两次 IVS，并在同一 pass 中同时做控制 IV
   规范化、trip count 物化和多类 live-out 重写，难以定位收益或回退来源。
5. 旧路线依赖扩大 InstCombine 来清理 IVS 产物，造成职责倒置。

## 新设计

### 1. 先建立可复用的归纳变量分析

在 LoopInfo/ScalarEvolution 一侧提供只读的 `InductionDescriptor`，至少记录：

- control phi、初值、更新值；
- 常量或循环不变步长及方向；
- guard 所在位置、比较谓词和 bound；
- exact/upper-bound trip count 是否可证明；
- zero-trip、整数溢出和 live-out 可重写性结论。

`canonicalIV` 继续只表示 `0,+1,<` 的旧语义；一般归纳变量通过独立描述符
暴露。现有分析不得为了配合变换而退化成 canonical-only，也不得要求先改写
IR 才能识别一般 recurrence。

第一阶段只补分析、dump 和针对分析的测试，不修改循环 IR。SCEV 无法证明时
返回 unknown，不扩大 RangeAnalysis 的全局语义。

### 2. 把变换拆成三个独立层级

按风险从低到高依次实现，每一层单独测试和评估：

1. **Canonical loop live-out folding**：对已经规范的循环，仅在 exact trip
   count 和递推式都可直接证明时重写 exit value，不创建新的控制 IV。等价式
   按 i32 模运算成立，不使用 signed-overflow 或其他未定义行为假设。
2. **Constant-stride normalization**：只处理常量步长；必须保留有用的原始
   recurrence，避免把循环内加法改成乘法。没有下游收益时不变换。
3. **Variable-stride handling**：初期仅供分析使用。只有在目标成本模型证明
   前置除法/取余可摊销，且 IR/汇编显示确实解锁后续优化时，才允许改写。

三层不能以函数名、基本块名或测试输入特征作决策。

### 3. 明确收益条件

变换前先生成计划并估算：

- 新增的循环外除法、取余、比较和 select；
- 新增或删除的循环内 add、mul、地址计算和分支；
- 是否解锁 LICM、loopFusion、loopInterchange、vectorize 或 IVSR；
- live-out 重写是否减少 phi/循环迭代，而不是仅改变表达式形状。

只有本层自身减少动态成本，或确定解锁某个下游变换时才提交改写。不能假设
后续 InstCombine 会清理结果，也不修改 InstCombine。

### 4. 流水线接入

- 第一版每个 loop pipeline round 最多运行一次，不再连续运行两次。
- 接入点必须在 `LoopSimplify + LCSSA` 之后。
- 早期 `LoopDistribution` 的 scalarExpansion 输入和分析保持不变。
- pass 必须幂等；第二次看到已处理循环时不得继续增长 IR。
- 每次改写后只失效实际受影响的分析；在证明清楚前可保守失效全部函数分析。

## 收益集与回归集

### 收益候选

- `h-4-*`：用于验证 variable-stride 归纳分析。它首先是分析覆盖目标，不是
  默认 canonicalization 目标。只有最终汇编减少循环动态成本时才能计为收益。
- 从全量 dump 中筛出的 constant-stride、non-zero-init、descending-loop 和
  live-out recurrence 用例。候选必须由通用结构统计产生，再逐项检查 IR。
- 在 `test/pass` 新增最小结构测试，覆盖 `<`/`<=`、正负常量步长、零迭代、
  nested bound、live-out 和溢出拒绝。

### 硬回归门禁

- `h-5-*`、`ludcmp-*`：
  - `LoopDistribution` 后必须保留 scalarExpansion scratch 和预期的
    clear/compute/storeback 结构；
  - 单独复测取平均值，任何稳定回退都要定位到 IR/汇编，不得归因于噪声；
  - A53 上的最终取舍依据目标机反馈，本机 M2 时间只作筛查。
- `crypto-*`、`conv2d-*`：
  - 不修改 InstCombine 和全局 RangeAnalysis 语义；
  - 若汇编变化，必须逐项复测取平均值；
  - 不接受稳定性能回退。
- 全量 `performance`：
  - 仅使用 `arm_performance.sh`，保持缓存用于比较；
  - 162 项必须全部 AC；
  - 编译不得超过 10s。

## 实施顺序与验收点

1. 记录当前基线的 IR、汇编和结果文件。
2. 实现 `InductionDescriptor` 与 dump；只验证分析覆盖率，不接入变换。
3. 根据 dump 形成真实候选表，明确每个候选预期减少的动态操作。
4. 实现 canonical live-out folding，检查 IR/汇编后跑全量。
5. 实现 constant-stride normalization，重复相同门禁。
6. 仅当 h-4 或其他候选有明确下游收益时，再评估 variable-stride 改写。
7. 在目标 A53 上复核本机筛出的收益和 h-5/ludcmp 风险。

每一步都必须能单独回退；某一层没有可验证收益时，停止在上一层，不通过扩大
InstCombine、削弱分析或放宽未定义行为假设来掩盖问题。
