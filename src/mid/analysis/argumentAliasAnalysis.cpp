#include "../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/module.hpp"

Value *ArgumentAliasAnalysis::underlyingObject(Value *ptr) {
    while (ptr) {
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
            ptr = gep->get_operand(0);
        } else if (auto *bc = dynamic_cast<Bitcast *>(ptr)) {
            ptr = bc->get_operand(0);
        } else {
            break;
        }
    }
    return ptr;
}

static bool isIdentifiedObject(Value *v) {
    return dynamic_cast<GlobalVariable *>(v) || dynamic_cast<AllocaInst *>(v);
}

void ArgumentAliasAnalysis::analyze(Module *module) {
    module_ = module;
    callSites_.clear();
    roots_.clear();
    unknown_.clear();
    inProgress_.clear();

    // 收集所有直接调用点：callee 是 CallInst 的最后一个操作数。
    for (auto *func : module->function_list_) {
        for (auto *bb : func->basic_blocks_) {
            for (auto *inst : bb->instr_list_) {
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call || call->num_ops() == 0) continue;
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops() - 1));
                if (callee)
                    callSites_[callee].push_back(call);
            }
        }
    }

    // 预解析每个函数的每个指针参数。
    for (auto *func : module->function_list_) {
        for (auto *arg : func->arguments_) {
            if (dynamic_cast<PointerType *>(arg->type_))
                resolveArg(arg);
        }
    }
}

void ArgumentAliasAnalysis::resolveArg(Argument *a) {
    if (roots_.count(a) || unknown_.count(a)) return;   // 已解析
    if (!inProgress_.insert(a).second) {                // 递归环 → unknown
        unknown_.insert(a);
        return;
    }

    Function *callee = a->parent_;
    unsigned  idx    = a->arg_no_;
    std::set<Value *> rootSet;
    bool isUnknown = false;

    auto it = callSites_.find(callee);
    if (it == callSites_.end() || it->second.empty()) {
        isUnknown = true;   // 无调用点：参数来源不可知（如 main / 逃逸）
    } else {
        for (auto *call : it->second) {
            // call 操作数：[args..., callee]；第 idx 个实参。
            if (idx >= call->num_ops() - 1) { isUnknown = true; break; }
            Value *root = underlyingObject(call->get_operand(idx));
            if (isIdentifiedObject(root)) {
                rootSet.insert(root);
            } else if (auto *parg = dynamic_cast<Argument *>(root)) {
                resolveArg(parg);
                const std::set<Value *> *sub = argRoots(parg);
                if (!sub) { isUnknown = true; break; }
                rootSet.insert(sub->begin(), sub->end());
            } else {
                isUnknown = true;   // 未知来源
                break;
            }
        }
    }

    inProgress_.erase(a);
    if (isUnknown) unknown_.insert(a);
    else           roots_[a] = std::move(rootSet);
}

const std::set<Value *> *ArgumentAliasAnalysis::argRoots(Argument *a) const {
    auto it = roots_.find(a);
    return it == roots_.end() ? nullptr : &it->second;
}

bool ArgumentAliasAnalysis::noAlias(Value *baseA, Value *baseB) const {
    if (!baseA || !baseB) return false;
    if (baseA == baseB)   return false;   // 同一对象

    // 同一函数的两个指针形参：逐调用点看实参底层对象。形参的根集合
    // 可能相同（例如两个调用点交换了两个数组），但若每个调用点上
    // 这一对实参都可证明不同，则本函数任一执行里二者仍不别名。
    auto *argA = dynamic_cast<Argument *>(baseA);
    auto *argB = dynamic_cast<Argument *>(baseB);
    if (argA && argB && argA->parent_ == argB->parent_) {
        auto it = callSites_.find(argA->parent_);
        if (it != callSites_.end() && !it->second.empty()) {
            bool allCallSitesDistinct = true;
            for (auto *call : it->second) {
                if (argA->arg_no_ >= call->num_ops() - 1 ||
                    argB->arg_no_ >= call->num_ops() - 1) {
                    allCallSitesDistinct = false;
                    break;
                }
                Value *actualA = underlyingObject(call->get_operand(argA->arg_no_));
                Value *actualB = underlyingObject(call->get_operand(argB->arg_no_));
                if (actualA == actualB || !isIdentifiedObject(actualA) ||
                    !isIdentifiedObject(actualB)) {
                    allCallSitesDistinct = false;
                    break;
                }
            }
            if (allCallSitesDistinct) return true;
        }
    }

    // 解析成根对象集；identified 对象自身即单元素集，参数查表，其它 unknown。
    auto rootsOf = [&](Value *v, std::set<Value *> &out) -> bool {
        if (isIdentifiedObject(v)) { out.insert(v); return true; }
        if (auto *arg = dynamic_cast<Argument *>(v)) {
            const std::set<Value *> *r = argRoots(arg);
            if (!r) return false;
            out = *r;
            return true;
        }
        return false;   // unknown
    };

    std::set<Value *> ra, rb;
    if (!rootsOf(baseA, ra) || !rootsOf(baseB, rb)) return false;
    if (ra.empty() || rb.empty()) return false;

    // 两个已知根集不相交 ⇒ 任一执行里所指对象必不相同 ⇒ 不别名。
    for (Value *x : ra)
        if (rb.count(x)) return false;
    return true;
}
