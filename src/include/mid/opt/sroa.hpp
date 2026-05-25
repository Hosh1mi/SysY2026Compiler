#pragma once

// 这个pass一定要与mem2reg配合使用！
// =============================================================================
// SROA (Scalar Replacement of Aggregates) Pass
// =============================================================================
//
// 目标：将聚合类型（ArrayType）的 alloca 指令拆分为多个标量类型的 alloca，
//       以便后续的 Mem2Reg pass 将标量 alloca 提升为 SSA 寄存器。
//
// 1. 候选条件
//    对于每个 alloca [N x T]（alloca_ty_ 为 ArrayType），检查所有 use 是否"安全"：
//      - 每个 use 必须是 GetElementPtrInst，且其所有索引必须是 ConstantInt
//      - GEP 的结果类型必须是 PointerType(scalar)，即最终指向 IntegerType 或 FloatType
//        （这意味着前端生成了直达标量的完整 GEP，而非分步的部分 GEP）
//      - 每个 GEP 的 user 只能是 LoadInst 或 StoreInst
//      - 若 any use 不满足 → 跳过该 alloca（保守策略，防止地址逃逸等情况）
//
// 2. 重写策略（替换指针操作数，而非创建新指令）
//    对于每个 GEP，提取其常量索引元组（如 1D 数组的 [k]，2D 数组的 [i,j]）：
//      - 按索引元组去重，为每个唯一下标元组创建一个新的标量 alloca（插入函数入口块）
//      - 遍历该 GEP 的 use_list，对于每个 Load/Store user：
//          直接通过 set_operand() 将 Load/Store 的指针操作数从 GEP 改为新的标量 alloca
//      - 好处：无需创建新的 Load/Store 指令，保留已有的 use-def 链
//
// 3. GEP 索引提取
//    GEP 操作数布局：[0]=base_ptr, [1]=首层解引用(0), [2]=第一维下标,
//                      [3]=次层解引用(0), [4]=第二维下标, ...
//    提取 operand[2], operand[4], operand[6], ... 作为元素标识元组。
//    若任一偶数位索引不是 ConstantInt → 标记为不安全。
//
// 4. 清理
//    所有被处理的 GEP 和原始 alloca 加入待删除集合，在函数末尾统一删除。
//
// 注：这个pass只处理alloca[N x T]，alloca本身交给下一步的mem2reg使用
// 
// 未处理的情况（后续可扩展）：
//    - 部分 GEP（结果仍为聚合指针，需要递归 SROA）
//    - 整聚合 load/store（需要分解为逐元素操作，需要 insertvalue/extractvalue）
//    - 非 GEP 直接 use（如 alloca 作为 call 参数传递）
// =============================================================================

#include "../ir/ir.hpp"
#include "pass.hpp"
#include <set>
#include <vector>

class SROA : public Pass {
public:
    void execute(Module *module) override;

private:
    void runOnFunction(Function *func);

    /// 检查 alloca 是否可以作为 SROA 的候选
    bool isSROACandidate(AllocaInst *alloca);

    /// 将候选 alloca 拆分为多个标量 alloca，并重定向所有 Load/Store
    void rewriteAlloca(AllocaInst *alloca);

    /// 从 GEP 指令中提取常量索引元组
    /// 提取 operand[2], operand[4], ...（即实际维度下标，跳过解引用 0）
    bool getConstantIndices(GetElementPtrInst *gep, std::vector<int> &indices);

    /// 判断类型是否为标量（IntegerType 或 FloatType）
    static bool isScalarType(Type *ty);

    /// 待删除指令集合（在 runOnFunction 末尾统一清理）
    std::set<Instruction *> toDelete_;

    Module *module_ = nullptr;
    Function *curFunc_ = nullptr;
};
