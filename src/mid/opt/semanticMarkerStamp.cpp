#include "../../include/mid/opt/semanticMarkerStamp.hpp"

#include <vector>

PreservedAnalyses SemanticMarkerStamp::execute(Module *module,
                                               AnalysisManager &AM) {
    BasicAliasAnalysis &BAA = AM.getBasicAA(module);
    objImmutableCache_.clear();
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        stampFunctionAttrs(func, BAA);
    }
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        LoopInfo &LI = AM.getLoopInfo(func);
        stampImmutableLoads(func, BAA, LI);
    }
    // 只置位，不动 IR 结构 —— 所有分析保持有效。
    return PreservedAnalyses::all();
}

void SemanticMarkerStamp::execute(Module *module) {
    // 无 AnalysisManager 的独立路径（单测/直接调用）：自建分析。
    BasicAliasAnalysis BAA;
    BAA.analyze(module);
    objImmutableCache_.clear();
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        stampFunctionAttrs(func, BAA);
    }
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        LoopInfo LI;
        LI.analyze(func);
        stampImmutableLoads(func, BAA, LI);
    }
}

// ── (1) const 内存不变性 ─────────────────────────────────────────────────────

bool SemanticMarkerStamp::addressEscapes(Value *obj, BasicAliasAnalysis &BAA) {
    std::vector<Value *> work{obj};
    std::unordered_map<Value *, bool> seen;
    seen[obj] = true;
    while (!work.empty()) {
        Value *v = work.back();
        work.pop_back();
        for (auto &use : v->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user) return true;          // 未知使用者，保守视为逃逸
            if (user->is_load()) continue;   // 读，安全
            if (user->is_store()) {
                // operand 0 = 被存的值，operand 1 = 目标指针。
                // 指针自身被写入内存（作为值）→ 逃逸；作为写目标（初始化）→ 安全。
                if (use.arg_no_ == 0) return true;
                continue;
            }
            if (user->is_gep() || dynamic_cast<Bitcast *>(user)) {
                if (!seen.count(user)) {
                    seen[user] = true;
                    work.push_back(user);
                }
                continue;
            }
            if (user->is_call()) {
                auto *call = static_cast<CallInst *>(user);
                if (use.arg_no_ >= call->num_ops_ - 1) return true;
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (!callee || callee->is_declaration()) return true;
                if (use.arg_no_ >= callee->arguments_.size()) return true;

                auto *formal = callee->arguments_[use.arg_no_];
                if (!formal->hasSemFlag(SemFlag::ArgNoCapture) &&
                    !BAA.isNoCapture(callee, formal))
                    return true;
                if (!formal->hasSemFlag(SemFlag::ArgReadOnly) &&
                    isModSet(BAA.getFunctionModRef(callee, formal)))
                    return true;
                continue;
            }
            // 其它未覆盖形态 → 保守逃逸。
            return true;
        }
    }
    return false;
}

bool SemanticMarkerStamp::isImmutableObject(Value *base, BasicAliasAnalysis &BAA,
                                            LoopInfo &LI) {
    if (!base) return false;
    auto cached = objImmutableCache_.find(base);
    if (cached != objImmutableCache_.end()) return cached->second;

    bool result = false;
    if (auto *gv = dynamic_cast<GlobalVariable *>(base)) {
        // const 全局：源级保证永不被写，任何读都是不变的（无需逃逸/支配判定）。
        result = gv->is_const_ || gv->hasSemFlag(SemFlag::ImmutableObject);
    } else if (auto *alloca = dynamic_cast<AllocaInst *>(base)) {
        // 局部 const 数组：要求
        //   (a) 标记为不变对象（源级 const），
        //   (b) alloca 不在任何循环内（初始化只跑一次，否则跨迭代会被重写），
        //   (c) 地址不逃逸（否则可能被 callee 写）。
        if (alloca->hasSemFlag(SemFlag::ImmutableObject) &&
            LI.getLoopFor(alloca->parent_) == nullptr &&
            !addressEscapes(alloca, BAA)) {
            result = true;
        }
    }

    objImmutableCache_[base] = result;
    return result;
}

void SemanticMarkerStamp::stampImmutableLoads(Function *func,
                                              BasicAliasAnalysis &BAA,
                                              LoopInfo &LI) {
    for (auto *bb : func->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_load()) continue;
            Value *base = BAA.getUnderlyingObject(inst->get_operand(0));
            if (isImmutableObject(base, BAA, LI))
                inst->setSemFlag(SemFlag::ImmutableLoad);
            else
                inst->clearSemFlag(SemFlag::ImmutableLoad);
        }
    }
}

// ── (2) 函数 / 参数属性（从 BasicAliasAnalysis 持久化） ───────────────────────

void SemanticMarkerStamp::stampFunctionAttrs(Function *func,
                                             BasicAliasAnalysis &BAA) {
    func->clearSemFlag(SemFlag::FnPure);
    func->clearSemFlag(SemFlag::FnReadOnly);

    if (BAA.isPure(func)) {
        func->setSemFlag(SemFlag::FnPure);
    } else {
        ModRefInfo mr = BAA.getFunctionModRef(func);  // overall
        if (isRefSet(mr) && !isModSet(mr))
            func->setSemFlag(SemFlag::FnReadOnly);
    }

    for (auto *arg : func->arguments_) {
        arg->clearSemFlag(SemFlag::ArgReadOnly);
        arg->clearSemFlag(SemFlag::ArgNoCapture);
        if (!dynamic_cast<PointerType *>(arg->type_)) continue;
        // 仅在证明 callee 从不经此指针写时才置只读（保守安全）。
        if (!isModSet(BAA.getFunctionModRef(func, arg)))
            arg->setSemFlag(SemFlag::ArgReadOnly);
        if (BAA.isNoCapture(func, arg))
            arg->setSemFlag(SemFlag::ArgNoCapture);
    }
}

void SemanticMarkerStamp::runOnFunction(Function *func, BasicAliasAnalysis &BAA,
                                        LoopInfo &LI) {
    stampFunctionAttrs(func, BAA);
    stampImmutableLoads(func, BAA, LI);
}
