#include "../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"

#include <limits>

void BasicAliasAnalysis::analyze(Module *module) {
    module_ = module;
    summaries_.clear();
    if (!module_) return;

    for (auto *func : module_->function_list_) {
        FunctionSummary summary;
        if (!func->is_declaration()) {
            summary.pure = true;
            summary.sideEffect = false;
            summary.overall = ModRefInfo::NoModRef;
        }
        summaries_[func] = summary;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *func : module_->function_list_) {
            if (func->is_declaration()) continue;
            FunctionSummary next = computeFunctionSummary(func);
            FunctionSummary &cur = summaries_[func];
            if (next.pure != cur.pure ||
                next.sideEffect != cur.sideEffect ||
                next.overall != cur.overall ||
                next.objectEffects != cur.objectEffects) {
                cur = next;
                changed = true;
            }
        }
    }
}

long long BasicAliasAnalysis::typeSize(Type *ty) const {
    if (!ty) return -1;
    switch (ty->tid_) {
    case Type::IntegerTyID:
        return static_cast<IntegerType *>(ty)->num_bits_ <= 1 ? 1 : 4;
    case Type::FloatTyID:
        return 4;
    case Type::PointerTyID:
        return 8;
    case Type::ArrayTyID: {
        auto *arr = static_cast<ArrayType *>(ty);
        long long elem = typeSize(arr->contained_);
        return elem < 0 ? -1 : elem * arr->num_elements_;
    }
    case Type::VectorTyID: {
        auto *vec = static_cast<VectorType *>(ty);
        long long elem = typeSize(vec->contained_);
        return elem < 0 ? -1 : elem * vec->num_elements_;
    }
    default:
        return -1;
    }
}

BasicAliasAnalysis::PointerInfo BasicAliasAnalysis::getPointerInfo(Value *ptr) const {
    PointerInfo info;
    info.base = ptr;

    while (true) {
        if (auto *bc = dynamic_cast<Bitcast *>(info.base)) {
            info.base = bc->get_operand(0);
            continue;
        }

        auto *gep = dynamic_cast<GetElementPtrInst *>(info.base);
        if (!gep) break;

        Value *base = gep->get_operand(0);
        PointerInfo baseInfo = getPointerInfo(base);
        info.base = baseInfo.base;
        info.hasConstantOffset = info.hasConstantOffset && baseInfo.hasConstantOffset;
        info.offsetBytes += baseInfo.offsetBytes;

        Type *curTy = static_cast<PointerType *>(base->type_)->contained_;
        for (unsigned i = 1; i < gep->num_ops_; i++) {
            auto *ci = dynamic_cast<ConstantInt *>(gep->get_operand(i));
            if (!ci) {
                info.hasConstantOffset = false;
            } else {
                long long elemSize = typeSize(curTy);
                if (elemSize < 0) {
                    info.hasConstantOffset = false;
                } else {
                    info.offsetBytes += static_cast<long long>(ci->value_) * elemSize;
                }
            }

            if (curTy->tid_ == Type::ArrayTyID) {
                curTy = static_cast<ArrayType *>(curTy)->contained_;
            } else if (curTy->tid_ == Type::PointerTyID) {
                curTy = static_cast<PointerType *>(curTy)->contained_;
            }
        }
        break;
    }

    return info;
}

bool BasicAliasAnalysis::isTrackedMemoryObject(Value *value) const {
    return dynamic_cast<GlobalVariable *>(value) ||
           dynamic_cast<AllocaInst *>(value) ||
           dynamic_cast<Argument *>(value);
}

AliasResult BasicAliasAnalysis::alias(Value *a, Value *b) const {
    if (!a || !b) return AliasResult::MayAlias;
    if (a == b) return AliasResult::MustAlias;

    PointerInfo pa = getPointerInfo(a);
    PointerInfo pb = getPointerInfo(b);
    if (pa.base == pb.base) {
        if (pa.hasConstantOffset && pb.hasConstantOffset &&
            pa.offsetBytes == pb.offsetBytes)
            return AliasResult::MustAlias;
        return AliasResult::MayAlias;
    }

    bool aGlobal = dynamic_cast<GlobalVariable *>(pa.base) != nullptr;
    bool bGlobal = dynamic_cast<GlobalVariable *>(pb.base) != nullptr;
    bool aAlloca = dynamic_cast<AllocaInst *>(pa.base) != nullptr;
    bool bAlloca = dynamic_cast<AllocaInst *>(pb.base) != nullptr;

    if ((aGlobal && bGlobal) || (aAlloca && bAlloca) ||
        (aGlobal && bAlloca) || (aAlloca && bGlobal))
        return AliasResult::NoAlias;

    return AliasResult::MayAlias;
}

void BasicAliasAnalysis::addObjectEffect(FunctionSummary &summary, Value *ptr,
                                         ModRefInfo effect) const {
    summary.overall = combineModRef(summary.overall, effect);
    PointerInfo info = getPointerInfo(ptr);
    if (!isTrackedMemoryObject(info.base)) return;
    summary.objectEffects[info.base] =
        combineModRef(summary.objectEffects[info.base], effect);
}

BasicAliasAnalysis::FunctionSummary
BasicAliasAnalysis::computeFunctionSummary(Function *func) const {
    FunctionSummary summary;
    summary.pure = true;
    summary.sideEffect = false;
    summary.overall = ModRefInfo::NoModRef;

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load()) {
                summary.pure = false;
                addObjectEffect(summary, inst->get_operand(0), ModRefInfo::Ref);
                continue;
            }

            if (inst->is_store()) {
                summary.pure = false;
                summary.sideEffect = true;
                addObjectEffect(summary, inst->get_operand(1), ModRefInfo::Mod);
                continue;
            }

            if (inst->is_call()) {
                auto *call = static_cast<CallInst *>(inst);
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                auto it = callee ? summaries_.find(callee) : summaries_.end();
                if (!callee || it == summaries_.end() || callee->is_declaration()) {
                    summary.pure = false;
                    summary.sideEffect = true;
                    summary.overall = combineModRef(summary.overall, ModRefInfo::ModRef);
                    continue;
                }

                const FunctionSummary &calleeSummary = it->second;
                if (!calleeSummary.pure) summary.pure = false;
                if (calleeSummary.sideEffect) summary.sideEffect = true;
                summary.overall = combineModRef(summary.overall, calleeSummary.overall);

                for (auto &kv : calleeSummary.objectEffects) {
                    Value *formalObj = kv.first;
                    ModRefInfo effect = kv.second;
                    Value *actualObj = formalObj;
                    if (auto *arg = dynamic_cast<Argument *>(formalObj)) {
                        if (arg->arg_no_ < call->num_ops_ - 1)
                            actualObj = call->get_operand(arg->arg_no_);
                    }
                    addObjectEffect(summary, actualObj, effect);
                }
            }
        }
    }

    if (summary.overall != ModRefInfo::NoModRef)
        summary.pure = false;
    return summary;
}

ModRefInfo BasicAliasAnalysis::getFunctionModRef(Function *func,
                                                 Value *ptrOrGlobal) const {
    auto it = summaries_.find(func);
    if (!func || it == summaries_.end()) return ModRefInfo::ModRef;
    const FunctionSummary &summary = it->second;
    if (!ptrOrGlobal) return summary.overall;

    PointerInfo query = getPointerInfo(ptrOrGlobal);
    ModRefInfo result = ModRefInfo::NoModRef;
    for (auto &kv : summary.objectEffects) {
        if (alias(kv.first, query.base) != AliasResult::NoAlias)
            result = combineModRef(result, kv.second);
    }
    if (result == ModRefInfo::NoModRef && summary.overall != ModRefInfo::NoModRef)
        return ModRefInfo::ModRef;
    return result;
}

ModRefInfo BasicAliasAnalysis::getModRefInfo(Instruction *inst, Value *ptr) const {
    if (!inst) return ModRefInfo::NoModRef;

    if (inst->is_load()) {
        return alias(inst->get_operand(0), ptr) == AliasResult::NoAlias
                   ? ModRefInfo::NoModRef
                   : ModRefInfo::Ref;
    }

    if (inst->is_store()) {
        return alias(inst->get_operand(1), ptr) == AliasResult::NoAlias
                   ? ModRefInfo::NoModRef
                   : ModRefInfo::Mod;
    }

    if (inst->is_call()) {
        auto *call = static_cast<CallInst *>(inst);
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
        if (!callee || callee->is_declaration()) return ModRefInfo::ModRef;
        if (isPure(callee)) return ModRefInfo::NoModRef;
        return getFunctionModRef(callee, ptr);
    }

    return ModRefInfo::NoModRef;
}

bool BasicAliasAnalysis::isPure(Function *func) const {
    auto it = summaries_.find(func);
    return it != summaries_.end() && it->second.pure;
}

bool BasicAliasAnalysis::mayHaveSideEffect(Function *func) const {
    auto it = summaries_.find(func);
    return it == summaries_.end() || it->second.sideEffect;
}

bool BasicAliasAnalysis::isLocalArrayPointer(Value *ptr) const {
    PointerInfo info = getPointerInfo(ptr);
    auto *alloca = dynamic_cast<AllocaInst *>(info.base);
    return alloca && alloca->alloca_ty_->tid_ == Type::ArrayTyID;
}
