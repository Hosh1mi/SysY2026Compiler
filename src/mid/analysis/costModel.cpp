#include "../../include/mid/analysis/costModel.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <cstdlib>

// 标量元素字节数：i32/float=4，pointer=8
static long bytesOfElement(Type *ty) {
    if (auto *it = dynamic_cast<IntegerType *>(ty)) return (it->num_bits_ + 7) / 8;
    if (ty->tid_ == Type::FloatTyID)               return 4;
    if (ty->tid_ == Type::PointerTyID)             return 8;
    return -1;
}

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
    long elem_bytes = bytesOfElement(cur);
    if (elem_bytes < 0) return -1;

    unsigned n_idx = gep->num_ops_ - 1;
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

    // 对每个 idx 提取仿射形式，按 iv 系数累加字节偏移
    long total = 0;
    for (unsigned m = 0; m < n_idx; m++) {
        AffineExpr e = AA_->analyze(gep->get_operand(m + 1));
        if (!e.valid) return -1;
        int c = e.coeffOf(iv);
        total += (long)c * weights[m];
    }
    return std::abs(total);
}

long CostModel::totalStride(const std::vector<GetElementPtrInst *> &geps, PhiInst *iv) {
    long sum = 0;
    for (auto *g : geps) {
        long s = strideAlong(g, iv);
        if (s < 0) return -1;
        sum += s;
    }
    return sum;
}
