/**
 * @file loopCloneUtils.hpp
 * @brief 声明循环剥离、展开等变换共用的指令克隆、值映射与 PHI 入边维护工具。
 * @details 克隆失败必须整体放弃当前候选，尤其不能让克隆区域继续引用原循环中的局部定义。
 */

#pragma once

#include "../analysis/loopInfo.hpp"
#include "../ir/instruction.hpp"

#include <unordered_map>
#include <unordered_set>

namespace loop_clone {

/**
 * @brief 将原循环中的值映射到克隆区域使用的值。
 * @param v 待映射的原值。
 * @param valueMap 已建立的“原值到克隆值”映射。
 * @param loopBlocks 原循环包含的基本块集合，用于区分局部定义和循环不变量。
 * @return 循环不变量原样返回，已克隆的局部值返回其映射；局部值缺少映射时
 * 返回 nullptr，调用方必须把它视为克隆失败，不能误用原定义。
 */
Value *remapValueOrInvariant(Value *v,
                             const std::unordered_map<Value *, Value *> &valueMap,
                             const std::unordered_set<BasicBlock *> &loopBlocks);

/**
 * @brief 把普通指令克隆到目标基本块，并重映射全部操作数。
 * @param orig 待克隆的原指令；PHI 和终结指令不由本接口处理。
 * @param destBB 接收新指令的目标基本块。
 * @param valueMap 原值到克隆值的映射；成功后会加入 orig 到新指令的映射。
 * @param loopBlocks 原循环包含的基本块集合。
 * @return 成功时返回新指令；操作码不受支持或任一操作数无法映射时返回 nullptr。
 */
Instruction *cloneInstruction(Instruction *orig, BasicBlock *destBB,
                              std::unordered_map<Value *, Value *> &valueMap,
                              const std::unordered_set<BasicBlock *> &loopBlocks);

/**
 * @brief 为克隆出的退出边向出口 PHI 追加一组重映射入值。
 * @param phi 需要更新的出口 PHI。
 * @param originalPred 被复制的原前驱，函数从对应入边读取原值。
 * @param clonedPred 新增的克隆前驱。
 * @param valueMap 原值到克隆值的映射。
 * @param loopBlocks 原循环包含的基本块集合。
 * @return 成功追加入边时返回 true；找不到原入边或值无法映射时返回 false。
 */
bool addRemappedIncomingForClonedEdge(
    PhiInst *phi, BasicBlock *originalPred, BasicBlock *clonedPred,
    const std::unordered_map<Value *, Value *> &valueMap,
    const std::unordered_set<BasicBlock *> &loopBlocks);

/**
 * @brief 查询 PHI 在指定前驱边上的入值。
 * @param phi 待查询的 PHI。
 * @param pred 指定的前驱基本块。
 * @return 找到时返回入值，否则返回 nullptr。
 */
Value *incomingFrom(PhiInst *phi, BasicBlock *pred);

/**
 * @brief 删除 PHI 中来自指定前驱的入值/前驱操作数对。
 * @param phi 待修改的 PHI。
 * @param pred 要移除的前驱基本块。
 * @return 找到并删除入边时返回 true，否则返回 false。
 */
bool removeIncomingFrom(PhiInst *phi, BasicBlock *pred);

} // namespace loop_clone
