// CostModel 根据 GEP 的数组维度和归纳变量系数估算一次循环迭代跨过的字节数。该信息用于
// 比较循环交换前后的 cache stride；地址不能线性化时返回未知，避免凭不完整信息改换循环。
#include "../../include/mid/analysis/costModel.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <cstdlib>

long CostModel::strideAlong(GetElementPtrInst *gep, PhiInst *iv) {
    if (!gep || !iv) return -1;

    // 从 base 类型剥出数组维度链，得到 dims[] 和最底层 elem_ty
    Value *base = gep->get_operand(0);
    auto  *ptr  = dynamic_cast<PointerType *>(base->type_);
    if (!ptr) return -1;
    Type *cur = ptr->contained_;
    std::vector<long> dims;
    while (auto *arr = dynamic_cast<ArrayType *>(cur)) {
        dims.push_back((long)arr->num_elements_);
        cur = arr->contained_;
    }
    long elem_bytes = typeStorageBytes(cur);
    if (elem_bytes < 0) return -1;

    unsigned n_idx = gep->num_ops() - 1;
    if (n_idx == 0) return 0;

    // GEP 字节权重：消耗第 m 个索引后剩余类型的 sizeof
    // 例：[1024 x [1024 x i32]]* + 3 个索引：
    //   weights[0] = 1024*1024*4  (idx_0 跨整个外层数组)
    //   weights[1] = 1024*4       (idx_1 跨一行 [1024 x i32])
    //   weights[2] = 4            (idx_2 跨一个 i32)
    std::vector<long> weights(n_idx, 0);
    weights[n_idx - 1] = elem_bytes;
    for (int m = (int)n_idx - 2; m >= 0; m--) {
        long dim = (m < (int)dims.size()) ? dims[m] : 1;
        weights[m] = weights[m + 1] * dim;
    }

    // 对每个 idx 提取仿射形式，按 iv 系数累加字节偏移。
    // 某维非仿射时：若可证明该维与 iv 无关，则步进 iv 不改变它 → 贡献 0；
    // 否则无法确定该维对 iv 的 stride → 整个 GEP stride 未知。
    long total = 0;
    for (unsigned m = 0; m < n_idx; m++) {
        Value     *idx = gep->get_operand(m + 1);
        AffineExpr e   = AA_->analyze(idx);
        if (e.valid) {
            total += (long)e.coeffOf(iv) * weights[m];
        } else if (AffineAnalysis::provablyIndependentOfIV(idx, iv)) {
            // 与 iv 无关 → 该维贡献 0
        } else {
            return -1;
        }
    }
    return std::abs(total);
}

// 一组访问对 iv 的总 stride。stride 未知的 GEP 跳过（不污染整体）；
// 全部未知才返回 -1。
long CostModel::totalStride(const std::vector<GetElementPtrInst *> &geps, PhiInst *iv) {
    long sum = 0;
    bool any = false;
    for (auto *g : geps) {
        long s = strideAlong(g, iv);
        if (s < 0) continue;
        sum += s;
        any = true;
    }
    return any ? sum : -1;
}
