/**
 * @file loopCloneUtils.cpp
 * @brief 循环区域克隆实现：实现循环区域克隆时的值重映射、指令复制以及退出 PHI 入边维护。
 * @details 普通指令、PHI 和终结指令分阶段处理，确保操作数映射完整后才向目标块提交克隆。
 */

// Shared remapping / instruction-clone helpers for loop peel and related
// CFG region copies.

#include "../../../include/mid/transform/loopCloneUtils.hpp"

#include <vector>

namespace loop_clone {

/**
 * @brief 查询 PHI 来自指定前驱的入值。
 * @param phi 待查询的 PHI 指令。
 * @param pred 指定前驱基本块。
 * @return 找到时返回对应值，否则返回 nullptr。
 */
Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

/**
 * @brief 原地执行 removeIncomingFrom 对应的 IR/CFG 改写，并维护相关 SSA 关系。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param pred 前驱基本块。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool removeIncomingFrom(PhiInst *phi, BasicBlock *pred) {
    bool removed = false;
    for (int i = (int)phi->num_ops() - 2; i >= 0; i -= 2) {
        if (phi->get_operand(i + 1) == pred) {
            phi->remove_operands(i, i + 1);
            removed = true;
        }
    }
    return removed;
}

/**
 * @brief 返回值的克隆映射；循环外不变量可直接复用。
 * @param v 待重映射的原值。
 * @param valueMap 已建立的原值到克隆值映射。
 * @param loopBlocks 被克隆循环的基本块集合。
 * @return 映射值或可复用不变量；循环内值尚未映射时返回 nullptr。
 */
Value *remapValueOrInvariant(Value *v,
                             const std::unordered_map<Value *, Value *> &valueMap,
                             const std::unordered_set<BasicBlock *> &loopBlocks) {
    if (!v)
        return nullptr;
    auto it = valueMap.find(v);
    if (it != valueMap.end())
        return it->second;

    if (auto *inst = dynamic_cast<Instruction *>(v)) {
        if (inst->parent_ && loopBlocks.count(inst->parent_))
            return nullptr;
    }
    return v;
}

/**
 * @brief 克隆一条受支持指令，并重映射其全部非基本块操作数。
 * @param orig 待克隆的原指令。
 * @param destBB 克隆指令的目标基本块。
 * @param valueMap 原值到克隆值的映射表，成功后写入新映射。
 * @param loopBlocks 被克隆循环的基本块集合。
 * @return 成功时返回克隆指令，不支持或操作数无法映射时返回 nullptr。
 */
Instruction *cloneInstruction(Instruction *orig, BasicBlock *destBB,
                              std::unordered_map<Value *, Value *> &valueMap,
                              const std::unordered_set<BasicBlock *> &loopBlocks) {
    // 克隆采用“先完整映射操作数、后创建指令”的事务式顺序。
    // 任何循环局部值缺少映射都返回 nullptr，避免克隆区域错误引用原循环定义。
    auto remap = [&](Value *v) -> Value * {
        return remapValueOrInvariant(v, valueMap, loopBlocks);
    };

    auto failIfNull = [&](Value *v) -> bool { return v == nullptr; };

    if (auto *bi = dynamic_cast<BinaryInst *>(orig)) {
        Value *a = remap(bi->get_operand(0));
        Value *b = remap(bi->get_operand(1));
        if (failIfNull(a) || failIfNull(b))
            return nullptr;
        auto *inst = new BinaryInst(bi->type_, bi->op_id_, a, b, destBB);
        inst->copySemFlagsFrom(bi);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *ui = dynamic_cast<UnaryInst *>(orig)) {
        Value *a = remap(ui->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new UnaryInst(ui->type_, ui->op_id_, a, destBB);
        inst->copySemFlagsFrom(ui);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *ci = dynamic_cast<ICmpInst *>(orig)) {
        Value *a = remap(ci->get_operand(0));
        Value *b = remap(ci->get_operand(1));
        if (failIfNull(a) || failIfNull(b))
            return nullptr;
        auto *inst = new ICmpInst(ci->icmp_op_, a, b, destBB);
        inst->copySemFlagsFrom(ci);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *fi = dynamic_cast<FCmpInst *>(orig)) {
        Value *a = remap(fi->get_operand(0));
        Value *b = remap(fi->get_operand(1));
        if (failIfNull(a) || failIfNull(b))
            return nullptr;
        auto *inst = new FCmpInst(fi->fcmp_op_, a, b, destBB);
        inst->copySemFlagsFrom(fi);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *si = dynamic_cast<SelectInst *>(orig)) {
        Value *c = remap(si->get_operand(0));
        Value *t = remap(si->get_operand(1));
        Value *f = remap(si->get_operand(2));
        if (failIfNull(c) || failIfNull(t) || failIfNull(f))
            return nullptr;
        auto *inst = new SelectInst(c, t, f, destBB);
        inst->copySemFlagsFrom(si);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        Value *base = remap(gi->get_operand(0));
        if (failIfNull(base))
            return nullptr;
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops(); ++i) {
            Value *idx = remap(gi->get_operand(i));
            if (failIfNull(idx))
                return nullptr;
            idxs.push_back(idx);
        }
        auto *inst = new GetElementPtrInst(base, idxs, destBB);
        inst->copySemFlagsFrom(gi);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *li = dynamic_cast<LoadInst *>(orig)) {
        Value *ptr = remap(li->get_operand(0));
        if (failIfNull(ptr))
            return nullptr;
        auto *inst = new LoadInst(ptr, destBB);
        inst->copySemFlagsFrom(li);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *si = dynamic_cast<StoreInst *>(orig)) {
        Value *val = remap(si->get_operand(0));
        Value *ptr = remap(si->get_operand(1));
        if (failIfNull(val) || failIfNull(ptr))
            return nullptr;
        auto *inst = new StoreInst(val, ptr, destBB);
        inst->copySemFlagsFrom(si);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *zi = dynamic_cast<ZextInst *>(orig)) {
        Value *a = remap(zi->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new ZextInst(zi->op_id_, a, zi->type_, destBB);
        inst->copySemFlagsFrom(zi);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *fp = dynamic_cast<FpToSiInst *>(orig)) {
        Value *a = remap(fp->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new FpToSiInst(fp->op_id_, a, fp->type_, destBB);
        inst->copySemFlagsFrom(fp);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *sf = dynamic_cast<SiToFpInst *>(orig)) {
        Value *a = remap(sf->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new SiToFpInst(sf->op_id_, a, sf->type_, destBB);
        inst->copySemFlagsFrom(sf);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *bc = dynamic_cast<Bitcast *>(orig)) {
        Value *a = remap(bc->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new Bitcast(bc->op_id_, a, bc->type_, destBB);
        inst->copySemFlagsFrom(bc);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *ci = dynamic_cast<CallInst *>(orig)) {
        // CallInst 的最后一个操作数保存被调函数，前面的操作数才是需要逐一重映射的实参。
        auto *callee = dynamic_cast<Function *>(ci->get_operand(ci->num_ops() - 1));
        if (!callee)
            return nullptr;
        std::vector<Value *> args;
        for (unsigned i = 0; i + 1 < ci->num_ops(); ++i) {
            Value *arg = remap(ci->get_operand(i));
            if (failIfNull(arg))
                return nullptr;
            args.push_back(arg);
        }
        auto *inst = new CallInst(callee, args, destBB);
        inst->copySemFlagsFrom(ci);
        if (ci->is_tail())
            inst->set_tail(true);
        valueMap[orig] = inst;
        return inst;
    }
    return nullptr;
}

/**
 * @brief 实现 addRemappedIncomingForClonedEdge 对应的局部分析或变换辅助逻辑。
 * @param phi 参数 `phi`，用于本函数的分析、匹配或 IR 构造。
 * @param originalPred 参数 `originalPred`，用于本函数的分析、匹配或 IR 构造。
 * @param clonedPred 参数 `clonedPred`，用于本函数的分析、匹配或 IR 构造。
 * @param valueMap 从原 IR 值到克隆值的映射表。
 * @param loopBlocks 循环所包含的基本块集合。
 * @return 条件成立、匹配成功或变换完成时返回 true，否则返回 false。
 */
bool addRemappedIncomingForClonedEdge(
    PhiInst *phi, BasicBlock *originalPred, BasicBlock *clonedPred,
    const std::unordered_map<Value *, Value *> &valueMap,
    const std::unordered_set<BasicBlock *> &loopBlocks) {
    Value *origVal = incomingFrom(phi, originalPred);
    if (!origVal)
        return false;
    Value *mapped =
        remapValueOrInvariant(origVal, valueMap, loopBlocks);
    if (!mapped)
        return false;
    phi->addIncoming(mapped, clonedPred);
    return true;
}

} // namespace loop_clone
