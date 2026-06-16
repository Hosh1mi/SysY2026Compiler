#pragma once
// LoopInterchange:
//   通用的"循环交换"。本质是把一个【完全并行】的循环 K 下沉到最内层，
//   遇到不完美嵌套时顺带做循环分配(distribution)——这正好等价于
//   LoopDistribution + LoopInterchange 的组合，但用单一依赖驱动的变换表达。
//
// 与 ScalarExpandedInterchange 的分工：
//   - ScalarExpandedInterchange 处理"内层 reduction + 标量展开"的交换；
//   - LoopInterchange 处理"把无依赖的并行循环移到最内"的纯置换/分配，
//     不需要标量展开（被移动的循环对内存无 loop-carried 依赖）。
//   两者共用 LoopInfo / AffineAnalysis / DependenceAnalysis / CostModel 分析层。
//
// 通用要求（不写死层数 / 不绑定具体核形 / 不识别函数名）：
//   - K 有规范 IV（init=0, step=+1, slt bound）、单 preheader/latch/exit；
//   - K 不是最内层（有后代循环），否则下沉无意义；
//   - K 对其体内全部访存【完全并行】：DependenceAnalysis 判定 K 这一层不携带
//     任何依赖（所有相依访问对在 K 层方向为 '='）；
//   - 收益：CostModel 下，把 K 作为最内层后，访存对最内 IV 的字节 stride 之和
//     严格小于当前最深可测后代循环的 stride（即 K 更 cache 友好）；
//   - K 体可被结构化地走成一串"段"：每段是一条直线代码组(无循环)或一个子循环；
//   - 跨段不存在被分配打乱的 SSA 活跃值（保守：拒绝跨段 SSA 依赖），内存通道
//     由依赖分析保证。
//
// 变换：递归重建 K 区域——把 K 从外层摘除，对每个叶子直线段包一层新的规范
// k 循环；对每个子循环，原样保留(包括 j=i+1 这类非规范 IV)，并把 K 下沉进它
// 的体内继续递归。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class ArgumentAliasAnalysis;

class LoopInterchange : public Pass {
public:
    void execute(Module *module) override;
    PreservedAnalyses execute(Module *module, AnalysisManager &AM) override;
    std::string name() const override { return "LoopInterchange"; }

private:
    bool runOnFunction(Function *func);

    const ArgumentAliasAnalysis *argAA_ = nullptr;  // 过程间参数别名 oracle
};
