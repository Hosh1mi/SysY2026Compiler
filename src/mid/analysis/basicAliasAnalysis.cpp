#include "../../include/mid/analysis/basicAliasAnalysis.hpp"
#include "../../include/mid/analysis/loopInfo.hpp"
#include "../../include/mid/ir/constant.hpp"
#include "../../include/mid/ir/globalVariable.hpp"

#include <cstdlib>
#include <iostream>

static bool isBasicAADebugEnabled() {
    static bool enabled = std::getenv("DEBUG_BASIC_AA") != nullptr;
    return enabled;
}

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
            summary.hasUnknownMemoryEffect = false;
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
                next.hasUnknownMemoryEffect != cur.hasUnknownMemoryEffect ||
                next.argNoCapture != cur.argNoCapture ||
                next.locationEffects != cur.locationEffects) {
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
        for (unsigned i = 1; i < gep->num_ops(); i++) {
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

MemoryLocation BasicAliasAnalysis::getMemoryLocation(Value *ptr) const {
    MemoryLocation loc;
    loc.ptr = ptr;
    auto *ptrTy = ptr ? dynamic_cast<PointerType *>(ptr->type_) : nullptr;
    if (!ptrTy) return loc;
    loc.elemType = ptrTy->contained_;
    loc.sizeBytes = typeSize(ptrTy->contained_);
    return loc;
}

std::string BasicAliasAnalysis::aliasResultName(AliasResult result) const {
    switch (result) {
    case AliasResult::NoAlias:
        return "NoAlias";
    case AliasResult::MayAlias:
        return "MayAlias";
    case AliasResult::MustAlias:
        return "MustAlias";
    }
    return "MayAlias";
}

AliasResult BasicAliasAnalysis::alias(const MemoryLocation &a,
                                      const MemoryLocation &b) const {
    if (!a.ptr || !b.ptr) return AliasResult::MayAlias;

    // A pointer must-aliases itself: identical SSA values address the same
    // location, regardless of whether the symbolic offset is a constant. This
    // is important when the pointer carries a variable index (e.g. a flat GEP),
    // where getPointerInfo() reports hasConstantOffset == false.
    if (a.ptr == b.ptr && a.sizeBytes >= 0 && a.sizeBytes == b.sizeBytes)
        return AliasResult::MustAlias;

    PointerInfo pa = getPointerInfo(a.ptr);
    PointerInfo pb = getPointerInfo(b.ptr);
    AliasResult result = AliasResult::MayAlias;
    if (pa.base == pb.base) {
        if (pa.hasConstantOffset && pb.hasConstantOffset) {
            bool sameRange = pa.offsetBytes == pb.offsetBytes &&
                             a.sizeBytes == b.sizeBytes;
            bool knownRanges = a.sizeBytes >= 0 && b.sizeBytes >= 0;
            if (sameRange) {
                result = AliasResult::MustAlias;
            } else if (knownRanges) {
                long long aEnd = pa.offsetBytes + a.sizeBytes;
                long long bEnd = pb.offsetBytes + b.sizeBytes;
                result = (aEnd <= pb.offsetBytes || bEnd <= pa.offsetBytes)
                             ? AliasResult::NoAlias
                             : AliasResult::MayAlias;
            }
        }
    } else {
        Value *aObject = getUnderlyingObject(a.ptr);
        Value *bObject = getUnderlyingObject(b.ptr);
        bool distinctObjects = aObject && bObject && aObject != bObject;
        bool aGlobal = dynamic_cast<GlobalVariable *>(aObject) != nullptr;
        bool bGlobal = dynamic_cast<GlobalVariable *>(bObject) != nullptr;
        bool aAlloca = dynamic_cast<AllocaInst *>(aObject) != nullptr;
        bool bAlloca = dynamic_cast<AllocaInst *>(bObject) != nullptr;

        if (distinctObjects &&
            ((aGlobal && bGlobal) || (aAlloca && bAlloca) ||
             (aGlobal && bAlloca) || (aAlloca && bGlobal)))
            result = AliasResult::NoAlias;
    }

    if (isBasicAADebugEnabled()) {
        std::cerr << "[BasicAA] alias result=" << aliasResultName(result)
                  << " a_base=" << (pa.base ? pa.base->name_ : "<null>")
                  << " a_off=" << (pa.hasConstantOffset ? std::to_string(pa.offsetBytes) : "?")
                  << " a_size=" << a.sizeBytes
                  << " b_base=" << (pb.base ? pb.base->name_ : "<null>")
                  << " b_off=" << (pb.hasConstantOffset ? std::to_string(pb.offsetBytes) : "?")
                  << " b_size=" << b.sizeBytes
                  << "\n";
    }

    return result;
}

AliasResult BasicAliasAnalysis::alias(Value *a, Value *b) const {
    return alias(getMemoryLocation(a), getMemoryLocation(b));
}

bool BasicAliasAnalysis::getConstantOffsetFromObject(
    Value *ptr, Value *&object, long long &offsetBytes) const {
    PointerInfo info = getPointerInfo(ptr);
    if (!info.base || !info.hasConstantOffset)
        return false;
    object = getUnderlyingObject(ptr);
    if (!object)
        return false;
    offsetBytes = info.offsetBytes;
    return true;
}

void BasicAliasAnalysis::addLocationEffect(FunctionSummary &summary,
                                           MemoryLocation loc,
                                           ModRefInfo effect) const {
    summary.overall = combineModRef(summary.overall, effect);
    PointerInfo info = getPointerInfo(loc.ptr);
    if (!isTrackedMemoryObject(info.base)) {
        summary.hasUnknownMemoryEffect = true;
        return;
    }

    for (auto &record : summary.locationEffects) {
        if (record.loc == loc) {
            record.effect = combineModRef(record.effect, effect);
            return;
        }
    }
    summary.locationEffects.push_back({loc, effect});
}

BasicAliasAnalysis::FunctionSummary
BasicAliasAnalysis::computeFunctionSummary(Function *func) const {
    FunctionSummary summary;
    summary.pure = true;
    summary.sideEffect = false;
    summary.overall = ModRefInfo::NoModRef;
    summary.hasUnknownMemoryEffect = false;
    summary.argNoCapture.resize(func->arguments_.size(), false);

    for (auto *arg : func->arguments_) {
        if (dynamic_cast<PointerType *>(arg->type_))
            summary.argNoCapture[arg->arg_no_] = true;
    }

    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->is_load()) {
                summary.pure = false;
                addLocationEffect(summary,
                                  getMemoryLocation(inst->get_operand(0)),
                                  ModRefInfo::Ref);
                continue;
            }

            if (inst->is_store()) {
                summary.pure = false;
                summary.sideEffect = true;
                addLocationEffect(summary,
                                  getMemoryLocation(inst->get_operand(1)),
                                  ModRefInfo::Mod);
                continue;
            }

            if (inst->is_call()) {
                auto *call = static_cast<CallInst *>(inst);
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops() - 1));
                // Intrinsics and other declarations can carry a proven pure
                // contract even though they have no body to summarize.  Treat
                // that contract before the generic declaration fallback;
                // otherwise one pure intrinsic makes every transitive caller
                // appear to have unknown memory effects.
                if (callee && callee->is_declaration() &&
                    callee->hasSemFlag(SemFlag::FnPure))
                    continue;
                auto it = callee ? summaries_.find(callee) : summaries_.end();
                if (!callee || it == summaries_.end() || callee->is_declaration()) {
                    summary.pure = false;
                    summary.sideEffect = true;
                    summary.overall = combineModRef(summary.overall, ModRefInfo::ModRef);
                    summary.hasUnknownMemoryEffect = true;
                    continue;
                }

                const FunctionSummary &calleeSummary = it->second;
                if (!calleeSummary.pure) summary.pure = false;
                if (calleeSummary.sideEffect) summary.sideEffect = true;
                summary.overall = combineModRef(summary.overall, calleeSummary.overall);

                if (calleeSummary.hasUnknownMemoryEffect)
                    summary.hasUnknownMemoryEffect = true;

                for (auto &record : calleeSummary.locationEffects) {
                    MemoryLocation actualLoc = record.loc;
                    if (auto *arg = dynamic_cast<Argument *>(record.loc.ptr)) {
                        if (arg->arg_no_ < call->num_ops() - 1)
                            actualLoc = getMemoryLocation(call->get_operand(arg->arg_no_));
                    }
                    addLocationEffect(summary, actualLoc, record.effect);
                }
            }
        }
    }

    for (auto *arg : func->arguments_) {
        if (!dynamic_cast<PointerType *>(arg->type_))
            continue;
        std::unordered_set<Value *> visited;
        summary.argNoCapture[arg->arg_no_] = valueDoesNotCapture(arg, visited);
    }

    if (summary.overall != ModRefInfo::NoModRef)
        summary.pure = false;
    return summary;
}

ModRefInfo BasicAliasAnalysis::getFunctionModRef(Function *func,
                                                 Value *ptrOrGlobal) const {
    if (!func) return ModRefInfo::ModRef;
    if (func->hasSemFlag(SemFlag::FnPure))
        return ModRefInfo::NoModRef;

    auto it = summaries_.find(func);
    if (it == summaries_.end()) {
        if (func->hasSemFlag(SemFlag::FnReadOnly))
            return ptrOrGlobal ? ModRefInfo::Ref : ModRefInfo::Ref;
        return ModRefInfo::ModRef;
    }

    const FunctionSummary &summary = it->second;
    if (!ptrOrGlobal) return summary.overall;

    MemoryLocation query = getMemoryLocation(ptrOrGlobal);
    ModRefInfo result = ModRefInfo::NoModRef;
    for (auto &record : summary.locationEffects) {
        if (alias(record.loc, query) != AliasResult::NoAlias)
            result = combineModRef(result, record.effect);
    }
    if (result == ModRefInfo::NoModRef && summary.hasUnknownMemoryEffect)
        return ModRefInfo::ModRef;
    return result;
}

ModRefInfo BasicAliasAnalysis::getModRefInfo(Instruction *inst, Value *ptr) const {
    if (!inst) return ModRefInfo::NoModRef;

    if (inst->is_load()) {
        return alias(getMemoryLocation(inst->get_operand(0)),
                     getMemoryLocation(ptr)) == AliasResult::NoAlias
                   ? ModRefInfo::NoModRef
                   : ModRefInfo::Ref;
    }

    if (inst->is_store()) {
        return alias(getMemoryLocation(inst->get_operand(1)),
                     getMemoryLocation(ptr)) == AliasResult::NoAlias
                   ? ModRefInfo::NoModRef
                   : ModRefInfo::Mod;
    }

    if (inst->is_call()) {
        return getCallModRef(static_cast<CallInst *>(inst), ptr);
    }

    return ModRefInfo::NoModRef;
}

ModRefInfo BasicAliasAnalysis::getCallModRef(CallInst *call, Value *ptr) const {
    if (!call || !ptr) return ModRefInfo::ModRef;

    auto *callee = dynamic_cast<Function *>(
        call->get_operand(call->num_ops() - 1));
    if (!callee) return ModRefInfo::ModRef;
    if (callee->hasSemFlag(SemFlag::FnPure) || isPure(callee))
        return ModRefInfo::NoModRef;
    if (callee->is_declaration()) {
        return callee->hasSemFlag(SemFlag::FnReadOnly)
                   ? ModRefInfo::Ref
                   : ModRefInfo::ModRef;
    }

    auto it = summaries_.find(callee);
    if (it == summaries_.end()) return ModRefInfo::ModRef;

    const FunctionSummary &summary = it->second;
    MemoryLocation query = getMemoryLocation(ptr);
    ModRefInfo result = ModRefInfo::NoModRef;
    for (const auto &record : summary.locationEffects) {
        MemoryLocation effectLoc = record.loc;
        PointerInfo effectInfo = getPointerInfo(record.loc.ptr);
        if (auto *formal = dynamic_cast<Argument *>(effectInfo.base)) {
            if (formal->parent_ != callee ||
                formal->arg_no_ >= call->num_ops() - 1) {
                return ModRefInfo::ModRef;
            }

            // For call-site disambiguation the actual underlying object is
            // sufficient.  Discarding a formal GEP's offset is conservative:
            // it can lose NoAlias for two ranges of the same object, but it
            // cannot invent NoAlias between distinct objects.
            effectLoc = getMemoryLocation(call->get_operand(formal->arg_no_));
        }

        if (alias(effectLoc, query) != AliasResult::NoAlias)
            result = combineModRef(result, record.effect);
    }

    if (result == ModRefInfo::NoModRef && summary.hasUnknownMemoryEffect)
        return ModRefInfo::ModRef;
    return result;
}

bool BasicAliasAnalysis::isPure(Function *func) const {
    if (func && func->hasSemFlag(SemFlag::FnPure))
        return true;
    auto it = summaries_.find(func);
    return it != summaries_.end() && it->second.pure;
}

bool BasicAliasAnalysis::mayHaveSideEffect(Function *func) const {
    if (func &&
        (func->hasSemFlag(SemFlag::FnPure) ||
         func->hasSemFlag(SemFlag::FnReadOnly))) {
        return false;
    }
    auto it = summaries_.find(func);
    return it == summaries_.end() || it->second.sideEffect;
}

bool BasicAliasAnalysis::isNoCapture(Function *func, Argument *arg) const {
    if (!func || !arg || arg->parent_ != func)
        return false;
    if (arg->hasSemFlag(SemFlag::ArgNoCapture))
        return true;

    auto it = summaries_.find(func);
    if (it == summaries_.end())
        return false;
    if (arg->arg_no_ >= it->second.argNoCapture.size())
        return false;
    return it->second.argNoCapture[arg->arg_no_];
}

bool BasicAliasAnalysis::isLocalArrayPointer(Value *ptr) const {
    PointerInfo info = getPointerInfo(ptr);
    auto *alloca = dynamic_cast<AllocaInst *>(info.base);
    return alloca && alloca->allocated_type()->tid_ == Type::ArrayTyID;
}

bool BasicAliasAnalysis::isImmutableLoad(Instruction *load,
                                         const LoopInfo &LI) const {
    if (!load || !load->is_load()) return false;
    Value *object = getUnderlyingObject(load->get_operand(0));
    if (auto *global = dynamic_cast<GlobalVariable *>(object))
        return global->is_const_ ||
               global->hasSemFlag(SemFlag::ImmutableObject);

    auto *alloca = dynamic_cast<AllocaInst *>(object);
    if (!alloca || !alloca->hasSemFlag(SemFlag::ImmutableObject) ||
        LI.getLoopFor(alloca->parent_))
        return false;

    std::unordered_set<Value *> visited;
    return immutableObjectHasSafeUses(alloca, visited);
}

namespace {

bool isPointerDerivedFrom(Value *value, Value *target,
                          std::unordered_set<Value *> &visited) {
    if (value == target) return true;
    if (!value || !visited.insert(value).second) return false;
    if (auto *bc = dynamic_cast<Bitcast *>(value))
        return isPointerDerivedFrom(bc->get_operand(0), target, visited);
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(value))
        return isPointerDerivedFrom(gep->get_operand(0), target, visited);
    return false;
}

Value *resolveUnderlyingObject(Value *value,
                               std::unordered_set<Value *> &visiting) {
    if (!value) return nullptr;
    if (auto *bc = dynamic_cast<Bitcast *>(value))
        return resolveUnderlyingObject(bc->get_operand(0), visiting);
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(value))
        return resolveUnderlyingObject(gep->get_operand(0), visiting);

    auto *phi = dynamic_cast<PhiInst *>(value);
    if (!phi) return value;
    if (!visiting.insert(phi).second) return nullptr;

    Value *object = nullptr;
    for (unsigned i = 0; i < phi->num_ops(); i += 2) {
        Value *incoming = phi->get_operand(i);
        std::unordered_set<Value *> derivedVisited;
        if (isPointerDerivedFrom(incoming, phi, derivedVisited))
            continue;

        auto nestedVisiting = visiting;
        Value *incomingObject = resolveUnderlyingObject(incoming, nestedVisiting);
        if (!incomingObject || (object && object != incomingObject)) {
            visiting.erase(phi);
            return nullptr;
        }
        object = incomingObject;
    }

    visiting.erase(phi);
    return object;
}

} // namespace

Value *BasicAliasAnalysis::getUnderlyingObject(Value *ptr) const {
    std::unordered_set<Value *> visiting;
    return resolveUnderlyingObject(ptr, visiting);
}

bool BasicAliasAnalysis::valueDoesNotCapture(
    Value *value, std::unordered_set<Value *> &visited) const {
    if (!value)
        return false;
    if (!visited.insert(value).second)
        return true;

    for (const auto &use : value->use_list_) {
        auto *user = use.user_;
        if (!user)
            return false;

        if (user->is_load())
            continue;

        if (user->is_store()) {
            if (use.operand_index_ == 0)
                return false;
            continue;
        }

        if (user->is_ret())
            return false;

        if (user->is_call()) {
            auto *call = static_cast<CallInst *>(user);
            if (use.operand_index_ >= call->num_ops() - 1)
                return false;

            auto *callee = dynamic_cast<Function *>(
                call->get_operand(call->num_ops() - 1));
            if (!callee || callee->is_declaration())
                return false;
            if (use.operand_index_ >= callee->arguments_.size())
                return false;
            if (!isNoCapture(callee, callee->arguments_[use.operand_index_]))
                return false;
            continue;
        }

        if (user->is_cmp() || user->is_fcmp())
            continue;

        if (user->is_gep() || dynamic_cast<Bitcast *>(user) ||
            dynamic_cast<PhiInst *>(user) || dynamic_cast<SelectInst *>(user)) {
            if (!dynamic_cast<PointerType *>(user->type_))
                return false;
            if (!valueDoesNotCapture(user, visited))
                return false;
            continue;
        }

        return false;
    }

    return true;
}

bool BasicAliasAnalysis::immutableObjectHasSafeUses(
    Value *value, std::unordered_set<Value *> &visited) const {
    if (!value || !visited.insert(value).second)
        return value != nullptr;

    for (const Use &use : value->use_list_) {
        auto *user = use.user_;
        if (!user) return false;
        if (user->is_load()) continue;
        if (user->is_store()) {
            if (use.operand_index_ == 0) return false;
            continue;
        }
        if (user->is_call()) {
            auto *call = static_cast<CallInst *>(user);
            if (use.operand_index_ >= call->num_ops() - 1) return false;
            auto *callee = dynamic_cast<Function *>(
                call->get_operand(call->num_ops() - 1));
            if (!callee || callee->is_declaration() ||
                use.operand_index_ >= callee->arguments_.size())
                return false;
            Argument *formal = callee->arguments_[use.operand_index_];
            if (!isNoCapture(callee, formal) ||
                isModSet(getFunctionModRef(callee, formal)))
                return false;
            continue;
        }
        if (user->is_gep() || dynamic_cast<Bitcast *>(user)) {
            if (!immutableObjectHasSafeUses(user, visited)) return false;
            continue;
        }
        return false;
    }
    return true;
}
