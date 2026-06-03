#pragma once
// ScalarExpandedInterchange:
//   "标量提升 + 循环交换" 的融合变换。LLVM 把它拆成 ScalarExpansion +
//   LoopDistribution + LoopInterchange 三个 pass；这里打包成一个变换，
//   适用于内层带 reduction phi、收尾在 inner_exit 处写回数组的嵌套循环。
//
// 通用要求（不写死层数 / 不绑定 matmul 表达式形态）：
//   - inner_loop L 是带 reduction phi 的最内层（children 为空）
//   - L 有 parent P。L 与 P 都有规范 IV（init=0, step=+1, slt bound）
//   - L body 全部访存是 load；GEP 仿射、base loop-invariant；索引中出现的
//     所有 IV 必须来自 L 自身或它的祖先链（不能是外面无关的循环）
//   - L.singleExit 唯一一条 store: sum_phi → gep[base, 0, ..., P.IV]
//     （即写回数组的"最内一维"由 P.IV 索引；这就是 scalar expansion 后能
//     用 tmp[P.IV] 数组累加的依据）
//   - P 之外、P 之上的循环以及它们的 IV 全部保留原样
//
// 合法性：DependenceAnalysis 在 P↔L 互换下不反转依赖（reduction 通过
// scalar→tmp[] 的 expansion 自然消除）。
//
// 收益：CostModel 比较 swap 前后所有访存对内层 IV 的字节 stride 之和，
// 严格变小才执行。
//
// 变换：分配 @__mm_tmp_<inner_dim>，重写 P.preheader 之后的 CFG 为
//   clear → for L_new(nk) { for P_new(nj) { <L body clone> } } → store-back
// L body 的 clone 用 ValMap 重映射：L.IV→nk_iv、P.IV→nj_iv、sum_phi→
// load tmp[nj_iv]；body 内回到 L.header 的回边替换为 "store tmp[nj_iv]; br nj_l"。

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/costModel.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"
#include <map>
#include <vector>

class ScalarExpandedInterchange : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

    struct ReductionNestInfo {
        Loop              *inner_loop;             // L
        Loop              *parent_loop;            // P，与 L 互换
        PhiInst           *sum_phi;
        Value             *sum_init;               // P-loop 不变量
        Value             *sum_latch;              // 每次 L 迭代后 sum 的新值
        std::vector<GetElementPtrInst *> body_geps;  // L body 全部 GEP
        StoreInst         *store_inst;
        GetElementPtrInst *gep_store;
        Value             *base_store;
        Value             *inner_bound;            // L.tripCount
        Value             *parent_bound;           // P.tripCount
        int                inner_dim;              // gep_store 的最内一维（temp buffer 大小）
    };

    bool detectScalarExpandableReduction(Loop *L, LoopInfo &LI, AffineAnalysis &AA,
                                         ReductionNestInfo &out);
    bool isLegalAndProfitable(const ReductionNestInfo &info,
                              DependenceAnalysis &DA,
                              CostModel &CM);
    bool apply(const ReductionNestInfo &info, Module *module);

    GlobalVariable *getOrCreateTempBuffer(Module *module, int size);
    std::map<int, GlobalVariable *> temp_buf_;   // size → 全局 buffer
};
