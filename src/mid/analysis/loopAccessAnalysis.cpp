// LoopAccessAnalysis 收集循环内的 load/store，记录可识别的 GEP、未知间接访问和数组维度。
// 它本身不决定变换是否合法，而是为依赖分析、循环交换和向量化准备统一的访存清单。
#include "../../include/mid/analysis/loopAccessAnalysis.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"

// collect：遍历相关指令或控制流汇总事实；合流处只保留安全结论。
LoopAccessInfo LoopAccessAnalysis::collect(Loop *loop) const {
    LoopAccessInfo info;
    if (!loop) return info;

    for (auto *bb : loop->blocksOrdered) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_call()) info.has_call = true;
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
                info.all_geps.push_back(gep);

            Value *ptr = nullptr;
            if (inst->is_load()) {
                ptr = inst->get_operand(0);
            } else if (inst->is_store()) {
                ptr = inst->get_operand(1);
                info.has_store = true;
            } else {
                continue;
            }

            auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
            info.memory_accesses.push_back({inst, ptr, gep});
            info.memory_instructions.push_back(inst);
            if (gep) info.memory_geps.push_back(gep);
        }
    }

    return info;
}

bool LoopAccessAnalysis::isAffineOverAncestorIVs(
    GetElementPtrInst *gep, Loop *inner) const {
    if (!gep || !inner || gep->num_ops() < 2) return false;

    for (unsigned i = 1; i < gep->num_ops(); i++) {
        AffineExpr expr = AA_->analyze(gep->get_operand(i));
        if (!expr.valid) return false;
        for (auto &term : expr.coeffs) {
            bool isAncestor = false;
            for (Loop *loop = inner; loop; loop = loop->parent) {
                if (loop->canonicalIV == term.first ||
                    loop->inductionIV == term.first) {
                    isAncestor = true;
                    break;
                }
            }
            if (!isAncestor) return false;
        }
    }

    return true;
}

bool LoopAccessAnalysis::isGlobalOrArgument(Value *value) {
    return dynamic_cast<GlobalVariable *>(value) ||
           dynamic_cast<Argument *>(value);
}

int LoopAccessAnalysis::innermostArrayDim(Value *base) {
    auto *ptr = dynamic_cast<PointerType *>(base->type_);
    if (!ptr) return -1;
    Type *type = ptr->contained_;
    int last = -1;
    while (auto *arr = dynamic_cast<ArrayType *>(type)) {
        last = static_cast<int>(arr->num_elements_);
        type = arr->contained_;
    }
    return last;
}
