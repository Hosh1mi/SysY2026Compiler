#pragma once
// LoopInterchange: 检测 j-k 循环交换 + 标量提升的合法+有益场景，并应用变换
//
// 通用版本（不绑定具体 matmul 表达式形态）：
//   1. 检测：3 层规范嵌套；k_loop body 内全部访存是 load 且 GEP 仿射于 {i,j,k}；
//      k_exit 单 store: sum_phi → D[i][j]；body 内无 store/call
//   2. 合法性：DependenceAnalysis 在 j↔k 交换下不反转依赖
//   3. 代价：CostModel 比较 swap 前后内层 byte-stride 之和
//   4. 变换：分配 @__mm_tmp[N]，重写 CFG 为
//        for i: clear → for k_new: for j_inner: <k_loop body clone> → store back
//      内层体来自原 k_loop body 的"单次迭代"复制：i_phi 保持、k_phi→nk_iv、
//      j_phi→nj_iv、sum_phi→load tmp[nj_iv]、back-edge 替换为 store tmp[nj_iv]; br nj_l

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"
#include <map>
#include <vector>

class LoopInterchange : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

    // 适合 scalar expansion 的 reduction 嵌套：i⊃j⊃k，内层 k 是 reduction 维，
    // body 全部访存是 load 且 GEP 仿射于 {i,j,k}，单 store 在 k_exit 写回 D[i][j]。
    struct ReductionNestInfo {
        Loop              *i_loop, *j_loop, *k_loop;
        PhiInst           *sum_phi;
        Value             *sum_init;           // sum 在 k_preheader 入值（loop-invariant in j-loop）
        Value             *sum_latch;          // sum 从 latch 流回 sum_phi 的值
        std::vector<GetElementPtrInst *> body_geps;  // k_loop body 全部 GEP（用于 cost 分析）
        StoreInst         *store_inst;
        GetElementPtrInst *gep_store;
        Value             *base_store;
        Value             *k_bound;
        Value             *j_bound;
        int                inner_dim;          // 写回数组的内维（决定 temp buffer 大小）
    };

    bool detectScalarExpandableReduction(Loop *k_loop, LoopInfo &LI, AffineAnalysis &AA,
                                         ReductionNestInfo &out);
    bool isLegalAndProfitable(const ReductionNestInfo &info,
                              DependenceAnalysis &DA,
                              CostModel &CM);
    bool apply(const ReductionNestInfo &info, Module *module);

    GlobalVariable *getOrCreateTempBuffer(Module *module, int size);
    std::map<int, GlobalVariable *> temp_buf_;   // size → 全局 buffer
};
