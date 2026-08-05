# Mid-Level Runtime 实现文档

`src/mid/runtime` 存放由编译器按需生成的 IR runtime。它不是 `libsysy.a` 中的 SysY IO 库，
也不是后端追加的并行汇编 runtime。当前本目录只实现 `LoopRepFold` 需要的可求和
模递推 runtime。

## 1. 入口与物化时机

- 接口：[`summableModSumRuntime.hpp`](../src/include/mid/runtime/summableModSumRuntime.hpp)
- 实现：[`summableModSumRuntime.cpp`](../src/mid/runtime/summableModSumRuntime.cpp)
- 唯一公开入口：`materializeSummableModSumRuntime(Module*)`

`LoopRepFold` 只在匹配成功时声明并调用 `__compiler.summable_mod_sum`。所有 O1 pass
运行完后，`main.cpp` 调用 materializer：

```text
LoopRepFold 匹配可求和递推
  └── 插入 __compiler.summable_mod_sum 声明和 call
        └── PassManager 完成目标无关优化
              └── materializeSummableModSumRuntime
                    └── 在同一 Module 中生成 i64 实现
```

如果模块中没有该声明，或声明已有函数体，materializer 直接返回。因此 O0 和未命中用例
不会生成未使用的 runtime，同一入口也不会重复物化。

物化位于 pass pipeline 之后，因此 runtime 内部的 `i64` 计算不参与前面的目标无关优化。
开启 `--verify-ir` 时，物化后会额外执行一次
`Module::verify("summable-runtime-materialization")`。

## 2. RuntimeBuilder

文件内部的 `RuntimeBuilder` 是一个小型 IR 构建器，专门用于生成 runtime 函数。它支持：

- 创建并切换基本块；
- 构造 `i32`/`i64` 常量和二元算术；
- 构造比较、select、call、load/store 和分支；
- 在 `i32` 与 `i64` 之间使用 `sext`/`trunc` 转换。

生成的函数使用普通 IR 调用约定并进入 AArch64 指令选择，无需额外链接对象文件。

## 3. 辅助函数层次

materializer 会创建以下内部函数：

| 函数 | 主要职责 |
| --- | --- |
| `__compiler.sms.floor_div` | 把 C 截断除法修正为数学 floor division |
| `__compiler.sms.gcd` | 用 Euclid 算法计算周期所需的最大公约数 |
| `__compiler.sms.floor_sum` | 计算等差序列上 floor 值之和 |
| `__compiler.sms.sum_nonnegative` | 对同号非负等差序列求带模余数和 |
| `__compiler.sms.sum_signed` | 处理全非负、全非正和穿越零点的三种区间 |
| `__compiler.sms.key` | 计算 i32 半区/回绕区间的分段 key |
| `__compiler.sms.end_key_run` | 二分查找当前 key 保持不变的最大区间 |
| `__compiler.sms.sum_monotone` | 在每个单调、同号的分段上使用闭式求和 |
| `__compiler.summable_mod_sum` | 对外入口，连接分段、闭式和与保守尾部 |

内部名称使用 `__compiler.sms.*` 命名空间，不与 SysY 源函数或 `libsysy.a` 的符号混淆。

## 4. 计算策略

入口接收由 `SummableExpressionAnalysis` 和 `LoopRepFold` 已经验证的摘要，包括：

- 迭代起点、终点和步长；
- 可选 select 两侧的线性式与选择方向；
- 线性项、乘法/除法项、常量项和内层模数；
- 每轮 additive、外层模数与初始状态。

入口先用宽位算术计算 trip count。对除最后 4096 轮以外的长前缀，它按 select 结果和
i32 回绕 key 分成单调段；各段再使用 GCD、周期分解、floor-sum 和等差求和累加贡献。

保留最后 4096 轮作为标量尾部，按原始 `i32` 算术、select 和 `srem` 顺序执行。闭式部分
同时构造正余数和其等价负代表两个候选状态；尾部结束后两者一致才返回结果。
若不一致，返回 `INT32_MIN` 作为保守失败哨兵，由调用点保留的原循环路径处理。

## 5. 正确性边界

- 宽位闭式用 `i64` 承载，不把 SysY `i32` 中间回绕误当成无限精度整数语义。
- `floor_div` 显式修正负余数，因为 C/AArch64 `sdiv` 是向零截断。
- 穿越零点、select 切换、i32 半区或回绕边界都先分段，不在非单调区间套用闭式。
- runtime 仅执行分析阶段已验证的可求和表达式摘要，不再依赖源级符号信息。
- 对不能确认的结果退回原循环，而不是接受猜测性快路径。

## 6. 调试与维护

查看物化后的 IR：

```bash
build/compiler -O1 --verify-ir -c input.sy -o /tmp/out.ir
```

只有实际命中 `LoopRepFold` 的输入才会出现 `__compiler.summable_mod_sum` 及
`__compiler.sms.*`。因为 runtime 在 PassManager 之后生成，修改它时不能依赖后续 Mem2Reg、DCE
或 CFGSimplify 修复 IR；新建函数体本身必须直接满足终结指令、PHI incoming、CFG 缓存和
use-def 不变式。
