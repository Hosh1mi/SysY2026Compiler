#pragma once
// LoopInterchange: 检测 j-k 循环交换 + 标量提升的合法+有益场景，并应用变换
//
// 不同于旧版的纯模式匹配，新版接口分为：
//   1. 检测：LoopInfo 找 3 层规范嵌套；AffineAnalysis 把每个 GEP 索引展开成仿射；
//      要求外层 IV 出现位置形成 reduction-friendly 形态
//   2. 合法性：DependenceAnalysis 给出方向向量，检查 j↔k 交换不反转任何依赖
//   3. 代价：内层访存 stride 在交换后变小（cache 友好）→ 才执行
//   4. 变换：分配 @__mm_tmp[N]，标量提升 sum→array，重写 CFG 为
//        for i: clear → for k_new: for j_inner: temp[j]+=c*A[k][j] → store back

#include "../analysis/affineAnalysis.hpp"
#include "../analysis/dependenceAnalysis.hpp"
#include "../analysis/loopInfo.hpp"
#include "pass.hpp"

class LoopInterchange : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

    // 检测：是否符合"3 层嵌套 + 内层带 reduction phi + 列访问 GEP"
    // 输出关键内部值供变换使用。返回 false 表示当前嵌套不匹配。
    struct MatmulInfo {
        Loop              *i_loop, *j_loop, *k_loop;
        PhiInst           *sum_phi;            // 累加器 phi
        BinaryInst        *sum_add;            // sum_next = add(sum_phi, product)
        BinaryInst        *prod_mul;           // product = mul(load, load)
        LoadInst          *load_ik;            // load X[i][k]
        LoadInst          *load_kj;            // load Y[k][j]
        GetElementPtrInst *gep_ik, *gep_kj;
        Value             *base_ik, *base_kj;
        StoreInst         *store_inst;         // store sum -> D[i][j]（在 k_loop.exit 中）
        GetElementPtrInst *gep_store;
        Value             *base_store;
        Value             *bound;              // 三层共用的 trip count
    };

    bool detectMatmul(Loop *k_loop, LoopInfo &LI, AffineAnalysis &AA,
                      MatmulInfo &out);
    bool isLegalAndProfitable(const MatmulInfo &info,
                              DependenceAnalysis &DA);
    bool apply(const MatmulInfo &info, Module *module);

    GlobalVariable *getOrCreateTempBuffer(Module *module);
    GlobalVariable *temp_buf_ = nullptr;
};
