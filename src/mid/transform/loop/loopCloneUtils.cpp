// Shared remapping / instruction-clone helpers for loop peel and related
// CFG region copies.

#include "../../../include/mid/transform/loopCloneUtils.hpp"

#include <vector>

namespace loop_clone {

Value *incomingFrom(PhiInst *phi, BasicBlock *pred) {
    for (unsigned i = 0; i < phi->num_ops_; i += 2) {
        if (phi->get_operand(i + 1) == pred)
            return phi->get_operand(i);
    }
    return nullptr;
}

bool removeIncomingFrom(PhiInst *phi, BasicBlock *pred) {
    bool removed = false;
    for (int i = (int)phi->num_ops_ - 2; i >= 0; i -= 2) {
        if (phi->get_operand(i + 1) == pred) {
            phi->remove_operands(i, i + 1);
            removed = true;
        }
    }
    return removed;
}

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

Instruction *cloneInstruction(Instruction *orig, BasicBlock *destBB,
                              std::unordered_map<Value *, Value *> &valueMap,
                              const std::unordered_set<BasicBlock *> &loopBlocks) {
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
        for (unsigned i = 1; i < gi->num_ops_; ++i) {
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
        auto *inst = new ZextInst(zi->op_id_, a, zi->dest_ty_, destBB);
        inst->copySemFlagsFrom(zi);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *fp = dynamic_cast<FpToSiInst *>(orig)) {
        Value *a = remap(fp->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new FpToSiInst(fp->op_id_, a, fp->dest_ty_, destBB);
        inst->copySemFlagsFrom(fp);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *sf = dynamic_cast<SiToFpInst *>(orig)) {
        Value *a = remap(sf->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new SiToFpInst(sf->op_id_, a, sf->dest_ty_, destBB);
        inst->copySemFlagsFrom(sf);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *bc = dynamic_cast<Bitcast *>(orig)) {
        Value *a = remap(bc->get_operand(0));
        if (failIfNull(a))
            return nullptr;
        auto *inst = new Bitcast(bc->op_id_, a, bc->dest_ty_, destBB);
        inst->copySemFlagsFrom(bc);
        valueMap[orig] = inst;
        return inst;
    }
    if (auto *ci = dynamic_cast<CallInst *>(orig)) {
        // Last operand is the callee function.
        auto *callee = dynamic_cast<Function *>(ci->get_operand(ci->num_ops_ - 1));
        if (!callee)
            return nullptr;
        std::vector<Value *> args;
        for (unsigned i = 0; i + 1 < ci->num_ops_; ++i) {
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
