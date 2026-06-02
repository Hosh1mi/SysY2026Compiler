#pragma once
// CostModel: 简化版 cache cost 估算
//
// 给定一个 GEP 和它"内层迭代"的 IV：
//   - 用 AffineAnalysis 把每个索引展开成 c₀ + Σ cᵢ·IVᵢ
//   - 计算 IV 在每个数组维度上贡献的字节偏移
//   - 累加得到该 IV 步进 1 时 GEP 的字节 stride
//
// 用法：判断 loop interchange 是否有收益——
//   gain = Σ accesses [stride_before(acc) - stride_after(acc)]
//   gain > 0 即"内层访存平均 stride 变小"，cache 友好。

#include "affineAnalysis.hpp"
#include "loopInfo.hpp"
#include <vector>

class CostModel {
public:
    explicit CostModel(AffineAnalysis &AA) : AA_(&AA) {}

    // 计算 GEP 在 wrt IV 步进 1 时的字节 stride
    // 假设 GEP 形如 gep <T*> %base, i32 0, idx_1, idx_2, ...
    // 数组从外到内的字节维度大小可以从 base 的 PointerType→ArrayType 链推出。
    // 返回 -1 表示无法静态计算（非仿射 / 类型不是数组层级）。
    long strideAlong(GetElementPtrInst *gep, PhiInst *iv);

    // 一组访问在 iv 上的总 stride（绝对值之和，作为粗略 cache 不友好度量）
    long totalStride(const std::vector<GetElementPtrInst *> &geps, PhiInst *iv);

private:
    AffineAnalysis *AA_;
};
