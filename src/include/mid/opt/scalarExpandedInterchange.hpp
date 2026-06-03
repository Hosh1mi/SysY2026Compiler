#pragma once
// ScalarExpandedInterchange:
//   "标量提升 + 循环交换" 的融合变换。LLVM 把它拆成 ScalarExpansion +
//   LoopDistribution + LoopInterchange 三个 pass；这里打包成一个变换，
//   适用于内层带 reduction phi、收尾在 inner_exit 处写回数组的嵌套循环。
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
// 变换：每个 reduction 分配一个 @__mm_tmp_<idx>_<size>，重写 P.preheader
// 之后的 CFG 为
//   clear (init all tmps) → for L_new(nk) { for P_new(nj) { <L body clone> } }
//   → store-back (write each tmp to its target array)
// L body 的 clone 用 ValMap 重映射：L.IV→nk_iv、P.IV→nj_iv、每个 sum_phi_i→
// load tmp_i[nj_iv]；latch 的 clone 处对每个 reduction 追加 store。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"
#include <vector>

class ScalarExpandedInterchange : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

    struct ReductionInfo {
        PhiInst           *sum_phi;
        Value             *sum_init;   // P-loop 不变量
        Value             *sum_latch;  // 每次 L 迭代后 sum 的新值
        StoreInst         *store_inst; // 在 L.singleExit 中
        GetElementPtrInst *gep_store;
        Value             *base_store;
        int                inner_dim;  // gep_store 的最内一维（tmp buffer 大小）
    };

    struct ReductionNestInfo {
        Loop                            *inner_loop;             // L
        Loop                            *parent_loop;            // P，与 L 互换
        std::vector<GetElementPtrInst *> body_geps;              // L body 全部 GEP
        std::vector<ReductionInfo>       reductions;             // ≥ 1 个 reduction
        Value                           *inner_bound;            // L.tripCount
        Value                           *parent_bound;           // P.tripCount
    };

    bool detectScalarExpandableReduction(Loop *L, LoopInfo &LI, AffineAnalysis &AA,
                                         ReductionNestInfo &out);
    bool isLegalAndProfitable(const ReductionNestInfo &info,
                              DependenceAnalysis &DA,
                              CostModel &CM);
    bool apply(const ReductionNestInfo &info, Module *module);

    GlobalVariable *createTempBuffer(Module *module, int size);
    int tmp_counter_ = 0;
};
