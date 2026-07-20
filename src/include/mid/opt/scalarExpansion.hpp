#pragma once
// ScalarExpansion:
//   只负责为可展开的标量 reduction 分配函数局部 scratch buffer。
//   后续 LoopDistribution 把原循环拆成 clear / compute / store-back 区域，
//   LoopInterchange 再交换 compute 区域的两层循环。
//
// 通用要求（不写死层数 / 不绑定 matmul 形态 / 支持多 reduction）：
//   - inner_loop L 是最内层（children 为空），有 parent P
//   - L 与 P 都有规范 IV（init=0, step=+1, slt bound）
//   - L.header 有 N+1 个 phi：L.IV + N 个 reduction phi（N ≥ 1）
//   - 每个 reduction phi 的 init 在 P 外可见，latch 在 L 内
//   - L body 不含 store/call；GEP 仿射、base loop-invariant；索引中出现的
//     IV 只能来自 L 的祖先链（含 L 自身）
//   - L.singleExit 恰好 N 条 store，与 N 个 reduction phi 一一对应：
//     每条 store sum_phi_i → gep[base_i, 0, ..., P.IV]，最后一维必为 P.IV
//   - P 之外、P 之上的循环以及它们的 IV 全部保留原样
//
// 合法性：DependenceAnalysis 在 P↔L 互换下不反转依赖（每个 reduction 通过
// scalar→tmp[] 的 expansion 自然消除）。
//
// 收益：CostModel 比较 swap 前后所有访存对内层 IV 的字节 stride 之和，
// 严格变小才执行。
//
// 变换：每个 reduction 分配一个带 LoopExpansionScratch 语义标记的
// entry-block alloca [N x i32]。本 pass 不改写循环 CFG，不克隆循环体，
// 也不创建全局 scratch 对象。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "../analysis/loopAccessAnalysis.hpp"
#include "../analysis/loopInterchangeAnalysis.hpp"
#include "../analysis/reductionAnalysis.hpp"
#include "pass.hpp"
#include <vector>

class ScalarExpansion : public Pass {
public:
    void execute(Module *module) override;
    std::string name() const override { return "ScalarExpansion"; }

private:
    void runOnFunction(Function *func);

    bool isLegalAndProfitable(const ScalarReductionNestInfo &info,
                              LoopInterchangeAnalysis &IA);
    AllocaInst *createTempBuffer(Function *func, int size);
    int tmp_counter_ = 0;
};
