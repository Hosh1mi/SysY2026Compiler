#include "../../include/mid/opt/accumElim.hpp"
#include "../../include/mid/opt/autoMemoization.hpp"
#include "../../include/mid/ir/ir.hpp"
#include "../../include/mid/ir/irBuilder.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define DBG(...) do { } while (0)

namespace {

// 单个候选函数的所有提取出来的语义信息
struct Pattern {
    Function *f = nullptr;
    unsigned accIdx = 0;
    int capConst = 0;
    bool hasCap = false;

    // acc-derived value → 从 acc 出发的累计 delta
    // （acc 自己 delta=0；Add(acc-derived, C) 的 delta = parent.delta + C）
    std::unordered_map<Value *, int> deltaOf;

    // 每条 incoming（直接 ret 或 PHI 中的某个槽）的语义分类
    // 直接 ret 的形式被 normalize 成"PHI 只有 1 个 incoming"
    struct IncomingInfo {
        BasicBlock *sourceBB;       // 该 incoming 来自的 BB
                                    // （对于直接 ret，sourceBB 就是 ret 所在 BB）
        enum Kind { ACC_DEP, CAP, SELF_CALL_PASS } kind;
        int delta;                  // ACC_DEP / SELF_CALL_PASS：累计 delta
        int capConst;               // CAP
        CallInst *selfCall;         // SELF_CALL_PASS
    };

    // 多入口的合并 ret：PhiInst + ReturnInst；或直接 ReturnInst（mergedPhi=nullptr）
    struct MergedRet {
        PhiInst *mergePhi;          // 可能为 nullptr：直接 ret 单个值
        ReturnInst *ret;            // 最终 ret 指令
        BasicBlock *retBB;          // ret 所在 BB
        std::vector<IncomingInfo> incomings;
    };
    std::vector<MergedRet> mergedRets;

    // 所有自调用（必然出现在 SELF_CALL_PASS 的 incomings 中）
    std::vector<CallInst *> selfCalls;
};

// 取 Add(a, b) 中"另一个"操作数（如果 a 就是 v，返回 b，反之）
static Value *otherOperand(BinaryInst *b, Value *v) {
    return (b->get_operand(0) == v) ? b->get_operand(1) : b->get_operand(0);
}

// 给定一个值 v，尝试把它解析成 (base_value + constant_offset)，
// 其中 base_value 在 derivedMap 中（即 acc-derived），constant 在两个操作数中。
// 仅穿透由 Add 组成的"acc + 常量链"。
// 成功返回 true 并写 base/delta；否则返回 false。
static bool resolveDerived(Value *v,
                           const std::unordered_map<Value *, int> &derived,
                           int &outDelta) {
    auto it = derived.find(v);
    if (it != derived.end()) { outDelta = it->second; return true; }
    return false;
}

// 检查某个 Value 是否依赖 acc（沿 use-def 反向 BFS）
// allowed = derivedMap（这些值"是" acc 派生）；任何不在 allowed
// 且依赖 acc 的值视作"违例"。
// 这里采用反向：从 acc 正向追到的派生集合 derivedMap 已经定义了边界；
// 其他指令只要其操作数都不在 derivedMap 即视为独立。
static bool dependsOnAcc(Value *v,
                         const std::unordered_map<Value *, int> &derived) {
    std::unordered_set<Value *> visited;
    std::function<bool(Value *)> dfs = [&](Value *cur) {
        if (derived.count(cur)) return true;
        if (!visited.insert(cur).second) return false;
        auto *inst = dynamic_cast<Instruction *>(cur);
        if (!inst) return false;
        if (dynamic_cast<PhiInst *>(inst)) {
            // 不沿 PHI 反向追（避免无限循环；保守可能错过 acc 依赖）
            // 我们在 detect 时已保证 PHI 不出现在 acc 链路上（acc 仅经 Add 流动）
            return false;
        }
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            if (dfs(inst->get_operand(i))) return true;
        }
        return false;
    };
    return dfs(v);
}

// ── 检测 ─────────────────────────────────────────────────────────────────

// 判断函数是否"纯"：无 store、所有 callee 是自身或 pure（保守地仅允许自调用 +
// 已知的 sysy I/O 之类的我们用不到；此处我们要求：自调用 + 仅 load 的简单形式）
static bool isPureCandidateBody(Function *f) {
    for (auto bb : f->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_store()) return false;
            if (auto *call = dynamic_cast<CallInst *>(inst)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (!callee) return false;
                if (callee != f) return false;   // 严格：只允许自调用
            }
        }
    }
    return true;
}

static bool hasSelfCall(Function *f) {
    for (auto bb : f->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (auto *c = dynamic_cast<CallInst *>(inst)) {
                if (c->get_operand(c->num_ops_ - 1) == static_cast<Value *>(f))
                    return true;
            }
        }
    }
    return false;
}

// 判断给定的 PHI 是否只服务一条直接 ret 的"合并返回 PHI"。
// 条件：phi 的所有用户都是同一条 ReturnInst 且 phi 在 ret 所在 BB 内。
static ReturnInst *mergePhiServesSingleRet(PhiInst *phi) {
    ReturnInst *theRet = nullptr;
    for (auto &use : phi->use_list_) {
        auto *user = dynamic_cast<Instruction *>(use.val_);
        if (!user) return nullptr;
        auto *ri = dynamic_cast<ReturnInst *>(user);
        if (!ri) return nullptr;
        if (theRet && theRet != ri) return nullptr;
        theRet = ri;
    }
    return theRet;
}

// 从 acc 出发正向构建 derivedMap（仅穿透 Add 与 const 的链），
// 同时验证 acc 的每个使用都"合法"。
//
// 允许的用户：
//   * Add(cur, ConstInt)：派生新值，delta 累加
//   * Ret(cur)：直接 acc-dep 返回
//   * 合并 ret PHI(..., cur, predBB, ...)：仅当该 PHI 服务唯一一条 ret 时
//   * self-call 的 acc-position 实参
static bool buildDerivedAndValidate(Function *f, Argument *acc,
                                    std::unordered_map<Value *, int> &derived,
                                    std::unordered_set<PhiInst *> &mergePhis) {
    derived[acc] = 0;
    std::vector<Value *> wl = {acc};
    while (!wl.empty()) {
        Value *cur = wl.back(); wl.pop_back();
        int curDelta = derived[cur];

        for (auto &use : cur->use_list_) {
            auto *user = dynamic_cast<Instruction *>(use.val_);
            if (!user) { DBG("[use is not Instruction] "); return false; }

            // Add(cur, ConstInt) → 派生
            if (auto *bin = dynamic_cast<BinaryInst *>(user);
                bin && bin->op_id_ == Instruction::Add) {
                Value *other = otherOperand(bin, cur);
                auto *ci = dynamic_cast<ConstantInt *>(other);
                if (!ci) {
                    DBG("[Add %s with non-const other] ", bin->name_.c_str());
                    return false;
                }
                int newDelta = curDelta + ci->value_;
                auto it = derived.find(bin);
                if (it == derived.end()) {
                    derived[bin] = newDelta;
                    wl.push_back(bin);
                } else if (it->second != newDelta) {
                    DBG("[conflicting delta] ");
                    return false;
                }
                continue;
            }

            // Ret(cur) → 合法 acc-dep return
            if (dynamic_cast<ReturnInst *>(user)) continue;

            // 合并 ret PHI 的一个槽 → 允许
            if (auto *phi = dynamic_cast<PhiInst *>(user)) {
                if (!mergePhiServesSingleRet(phi)) {
                    DBG("[PHI not serving single ret] ");
                    return false;
                }
                mergePhis.insert(phi);
                continue;
            }

            // self-call 的 acc-position 实参 → 合法
            if (auto *call = dynamic_cast<CallInst *>(user)) {
                auto *callee = dynamic_cast<Function *>(
                    call->get_operand(call->num_ops_ - 1));
                if (callee != f) { DBG("[call to non-self] "); return false; }
                if (use.arg_no_ != acc->arg_no_) {
                    DBG("[call arg pos %u != accIdx %u] ", use.arg_no_, acc->arg_no_);
                    return false;
                }
                continue;
            }

            DBG("[unsupported user op=%d] ", user->op_id_);
            return false;
        }
    }
    return true;
}

// 分类一个 "incoming value 配 sourceBB" 三选一：ACC_DEP / CAP / SELF_CALL_PASS
//
// 校验 self-call 时还会检查：除 acc-position 外的实参不依赖 acc。
// sourceBB 用于：SELF_CALL_PASS 时记录 self-call 所在 BB。
static bool classifyIncoming(Function *f, Argument *acc,
                             const std::unordered_map<Value *, int> &derived,
                             Value *rv, BasicBlock *sourceBB,
                             Pattern::IncomingInfo &info,
                             int &capKInOut, bool &sawCapInOut) {
    info.sourceBB = sourceBB;
    info.delta = 0;
    info.capConst = 0;
    info.selfCall = nullptr;

    if (auto it = derived.find(rv); it != derived.end()) {
        info.kind = Pattern::IncomingInfo::ACC_DEP;
        info.delta = it->second;
        return true;
    }
    if (auto *ci = dynamic_cast<ConstantInt *>(rv)) {
        info.kind = Pattern::IncomingInfo::CAP;
        info.capConst = ci->value_;
        if (!sawCapInOut) { capKInOut = ci->value_; sawCapInOut = true; }
        else if (capKInOut != ci->value_) return false;
        return true;
    }
    if (auto *call = dynamic_cast<CallInst *>(rv)) {
        auto *callee = dynamic_cast<Function *>(
            call->get_operand(call->num_ops_ - 1));
        if (callee != f) return false;
        // 该 self-call 必须只被一处使用（这条 ret/PHI 槽），方便我们把它
        // 替换成 aux-call + sentinel 分支。
        if (rv->use_list_.size() != 1) return false;
        // call 必须在 sourceBB 内（保证我们能 inline 改写为 aux+分支）
        if (call->parent_ != sourceBB) return false;
        Value *accArg = call->get_operand(acc->arg_no_);
        int d;
        if (!resolveDerived(accArg, derived, d)) return false;
        unsigned nArgs = f->arguments_.size();
        for (unsigned i = 0; i < nArgs; ++i) {
            if (i == acc->arg_no_) continue;
            if (dependsOnAcc(call->get_operand(i), derived)) return false;
        }
        info.kind = Pattern::IncomingInfo::SELF_CALL_PASS;
        info.selfCall = call;
        info.delta = d;
        return true;
    }
    return false;
}

// 把 f 内所有 ret 指令收集起来，分别拆开（直接 ret 或 phi + ret）。
static bool classifyReturns(Function *f, Argument *acc,
                            const std::unordered_map<Value *, int> &derived,
                            Pattern &pat) {
    bool sawCap = false;
    int capK = 0;
    std::unordered_set<CallInst *> selfCallSet;

    for (auto bb : f->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            auto *ri = dynamic_cast<ReturnInst *>(inst);
            if (!ri || ri->num_ops_ == 0) continue;
            Value *rv = ri->get_operand(0);

            Pattern::MergedRet mr;
            mr.ret = ri;
            mr.retBB = bb;
            mr.mergePhi = nullptr;

            // 是否为合并 PHI：phi 在同 BB 且 phi 的 use 是这条 ret
            if (auto *phi = dynamic_cast<PhiInst *>(rv)) {
                if (phi->parent_ != bb) return false;
                mr.mergePhi = phi;
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    Value *iv = phi->get_operand(i);
                    auto *pbb = dynamic_cast<BasicBlock *>(phi->get_operand(i + 1));
                    Pattern::IncomingInfo info;
                    if (!classifyIncoming(f, acc, derived, iv, pbb, info,
                                          capK, sawCap)) return false;
                    if (info.kind == Pattern::IncomingInfo::SELF_CALL_PASS)
                        selfCallSet.insert(info.selfCall);
                    mr.incomings.push_back(info);
                }
            } else {
                Pattern::IncomingInfo info;
                if (!classifyIncoming(f, acc, derived, rv, bb, info,
                                      capK, sawCap)) return false;
                if (info.kind == Pattern::IncomingInfo::SELF_CALL_PASS)
                    selfCallSet.insert(info.selfCall);
                mr.incomings.push_back(info);
            }

            pat.mergedRets.push_back(std::move(mr));
        }
    }

    if (!sawCap) return false;
    pat.capConst = capK;
    pat.hasCap = true;
    for (auto *c : selfCallSet) pat.selfCalls.push_back(c);
    return true;
}

// 顶层检测：找一个合法的 acc-arg
static bool detect(Function *f, Pattern &pat) {
    DBG("[AccumElim] try %s: ", f->name_.c_str());
    auto retTy = f->get_return_type();
    if (!retTy || retTy->tid_ != Type::IntegerTyID) { DBG("not i32 ret\n"); return false; }
    if (static_cast<IntegerType *>(retTy)->num_bits_ != 32) { DBG("not i32 ret\n"); return false; }
    if (f->arguments_.size() < 2) { DBG("too few args\n"); return false; }
    for (auto a : f->arguments_) {
        if (a->type_->tid_ != Type::IntegerTyID) { DBG("non-i32 arg\n"); return false; }
        if (static_cast<IntegerType *>(a->type_)->num_bits_ != 32) { DBG("non-i32 arg\n"); return false; }
    }
    if (!hasSelfCall(f)) { DBG("no self-call\n"); return false; }
    if (!isPureCandidateBody(f)) { DBG("impure body\n"); return false; }

    // 逐参数尝试
    for (auto arg : f->arguments_) {
        DBG("acc=%u: ", arg->arg_no_);
        std::unordered_map<Value *, int> derived;
        std::unordered_set<PhiInst *> mergePhis;
        if (!buildDerivedAndValidate(f, arg, derived, mergePhis)) {
            DBG("derived/validate failed; "); continue;
        }
        Pattern trial;
        trial.f = f;
        trial.accIdx = arg->arg_no_;
        trial.deltaOf = derived;
        if (!classifyReturns(f, arg, derived, trial)) { DBG("classify failed; "); continue; }

        DBG("OK (cap=%d, %zu mergedRets, %zu selfCalls)\n",
            trial.capConst, trial.mergedRets.size(), trial.selfCalls.size());
        pat = std::move(trial);
        return true;
    }
    DBG("no acc candidate matched\n");
    return false;
}

// ── 变换 ─────────────────────────────────────────────────────────────────

// 从 f 克隆 CFG 到 aux：
//   * acc-derived 值预绑定到 ConstInt(delta)（不克隆 acc-tracking Add）
//   * self-call → aux-call（同位置插入；返回值替换原 self-call）
//   * 合并 ret PHI / 直接 ret 的 incoming 值在 phase 2 按 IncomingInfo 重写：
//       - ACC_DEP: 直接是 ConstInt(delta)（来自 valMap 预绑定）
//       - CAP: value 返回占位 0，kind side-channel 写 1
//       - SELF_CALL_PASS: 根据递归调用写出的 kind side-channel 分流
static Function *buildAux(const Pattern &pat, GlobalVariable *&kindGV) {
    Function *f = pat.f;
    Module *m = f->parent_;
    auto *i32 = m->int32_ty_;

    // 构造 aux 的类型：去掉 acc-arg
    std::vector<Type *> paramTys;
    for (unsigned i = 0; i < f->arguments_.size(); ++i) {
        if (i == pat.accIdx) continue;
        paramTys.push_back(f->arguments_[i]->type_);
    }
    auto *auxTy = new FunctionType(m->int32_ty_, paramTys);
    auto *aux = new Function(auxTy, "__accumelim_aux_" + f->name_, m);
    kindGV = new GlobalVariable("__accumelim_kind_" + f->name_, m, i32,
                                false, new ConstantInt(i32, 0));

    std::unordered_map<Value *, Value *> valMap;
    std::unordered_map<BasicBlock *, BasicBlock *> bbMap;

    // 形参映射（跳过 accIdx）
    unsigned auxArgIdx = 0;
    for (unsigned i = 0; i < f->arguments_.size(); ++i) {
        if (i == pat.accIdx) continue;
        valMap[f->arguments_[i]] = aux->arguments_[auxArgIdx++];
    }

    // 预绑定：acc-derived 值 → ConstInt(delta)
    for (auto &[v, delta] : pat.deltaOf) {
        valMap[v] = new ConstantInt(m->int32_ty_, delta);
    }

    // 第一遍：建空 BB 框架
    for (auto *oldBB : f->basic_blocks_) {
        bbMap[oldBB] = new BasicBlock(m, "aux_" + oldBB->name_, aux);
    }

    auto mapOp = [&](Value *v) -> Value * {
        auto it = valMap.find(v);
        if (it != valMap.end()) return it->second;
        if (auto *bb = dynamic_cast<BasicBlock *>(v))
            return bbMap.at(bb);
        return v;
    };

    // 收集合并 PHI / 直接 ret，便于 phase 2 重写
    struct ClonedRet { const Pattern::MergedRet *orig; PhiInst *newPhi; ReturnInst *newRet; BasicBlock *newRetBB; };
    std::vector<ClonedRet> clonedRets;
    auto findOrigRet = [&](ReturnInst *r) -> const Pattern::MergedRet * {
        for (auto &mr : pat.mergedRets) if (mr.ret == r) return &mr;
        return nullptr;
    };

    // PHI 延迟填充
    struct PhiFixup { PhiInst *oldPhi; PhiInst *newPhi; };
    std::vector<PhiFixup> phiFixups;

    // 第二遍：克隆每个 BB 的内容
    for (auto *oldBB : f->basic_blocks_) {
        BasicBlock *newBB = bbMap[oldBB];

        for (auto *oldInst : oldBB->instr_list_) {
            // 跳过 acc 跟踪用的 Add
            if (pat.deltaOf.count(oldInst)) continue;

            // PHI
            if (auto *oldPhi = dynamic_cast<PhiInst *>(oldInst)) {
                auto *newPhi = PhiInst::create_phi(oldPhi->type_, newBB);
                newBB->add_instruction(newPhi);
                valMap[oldPhi] = newPhi;
                phiFixups.push_back({oldPhi, newPhi});
                continue;
            }
            // self-call → aux-call
            if (auto *oldCall = dynamic_cast<CallInst *>(oldInst)) {
                auto *callee = dynamic_cast<Function *>(
                    oldCall->get_operand(oldCall->num_ops_ - 1));
                if (callee == f) {
                    std::vector<Value *> auxArgs;
                    for (unsigned i = 0; i < f->arguments_.size(); ++i) {
                        if (i == pat.accIdx) continue;
                        auxArgs.push_back(mapOp(oldCall->get_operand(i)));
                    }
                    auto *newCall = new CallInst(aux, auxArgs, newBB);
                    valMap[oldCall] = newCall;
                    continue;
                }
                return nullptr;  // 非自调用：detect 应已拦截，防御性失败
            }
            if (auto *oldBr = dynamic_cast<BranchInst *>(oldInst)) {
                if (oldBr->num_ops_ == 3) {
                    auto *cond  = mapOp(oldBr->get_operand(0));
                    auto *tBB   = dynamic_cast<BasicBlock *>(mapOp(oldBr->get_operand(1)));
                    auto *fBB   = dynamic_cast<BasicBlock *>(mapOp(oldBr->get_operand(2)));
                    new BranchInst(cond, tBB, fBB, newBB);
                } else {
                    auto *tBB = dynamic_cast<BasicBlock *>(mapOp(oldBr->get_operand(0)));
                    new BranchInst(tBB, newBB);
                }
                continue;
            }
            if (auto *oldRi = dynamic_cast<ReturnInst *>(oldInst)) {
                if (oldRi->num_ops_ == 0) { new ReturnInst(newBB); continue; }
                // 先按"原样克隆"返回，phase 2 再重写
                auto *newRi = new ReturnInst(mapOp(oldRi->get_operand(0)),
                                              newBB);
                const Pattern::MergedRet *mr = findOrigRet(oldRi);
                PhiInst *newPhi = nullptr;
                if (mr && mr->mergePhi) {
                    // mergePhi 已经在前面被 PHI 分支处理过了，valMap 里有
                    newPhi = dynamic_cast<PhiInst *>(valMap.at(mr->mergePhi));
                }
                clonedRets.push_back({mr, newPhi, newRi, newBB});
                continue;
            }
            if (auto *oldLd = dynamic_cast<LoadInst *>(oldInst)) {
                auto *nl = new LoadInst(mapOp(oldLd->get_operand(0)), newBB);
                valMap[oldLd] = nl;
                continue;
            }
            if (auto *oldGEP = dynamic_cast<GetElementPtrInst *>(oldInst)) {
                std::vector<Value *> idxs;
                for (unsigned i = 1; i < oldGEP->num_ops_; ++i)
                    idxs.push_back(mapOp(oldGEP->get_operand(i)));
                auto *ng = new GetElementPtrInst(mapOp(oldGEP->get_operand(0)),
                                                  idxs, newBB);
                valMap[oldGEP] = ng;
                continue;
            }
            if (auto *oldBin = dynamic_cast<BinaryInst *>(oldInst)) {
                auto *nb = new BinaryInst(oldBin->type_, oldBin->op_id_,
                                           mapOp(oldBin->get_operand(0)),
                                           mapOp(oldBin->get_operand(1)), newBB);
                valMap[oldBin] = nb;
                continue;
            }
            if (auto *oldIcmp = dynamic_cast<ICmpInst *>(oldInst)) {
                auto *ni = new ICmpInst(oldIcmp->icmp_op_,
                                          mapOp(oldIcmp->get_operand(0)),
                                          mapOp(oldIcmp->get_operand(1)), newBB);
                valMap[oldIcmp] = ni;
                continue;
            }
            if (auto *oldZ = dynamic_cast<ZextInst *>(oldInst)) {
                auto *nz = new ZextInst(oldZ->op_id_, mapOp(oldZ->get_operand(0)),
                                          oldZ->dest_ty_, newBB);
                valMap[oldZ] = nz;
                continue;
            }
            if (auto *oldSel = dynamic_cast<SelectInst *>(oldInst)) {
                auto *ns = new SelectInst(mapOp(oldSel->get_operand(0)),
                                            mapOp(oldSel->get_operand(1)),
                                            mapOp(oldSel->get_operand(2)),
                                            newBB);
                valMap[oldSel] = ns;
                continue;
            }
            if (auto *oldBc = dynamic_cast<Bitcast *>(oldInst)) {
                auto *nb = new Bitcast(oldBc->op_id_, mapOp(oldBc->get_operand(0)),
                                         oldBc->dest_ty_, newBB);
                valMap[oldBc] = nb;
                continue;
            }
            if (dynamic_cast<AllocaInst *>(oldInst)) {
                return nullptr;   // 不应出现
            }
            // 兜底：unsupported
            return nullptr;
        }
    }

    // 填充 PHI 操作数（仅普通 PHI，不含 mergePhi）
    for (auto &fix : phiFixups) {
        for (unsigned i = 0; i < fix.oldPhi->num_ops_; i += 2) {
            Value *v = mapOp(fix.oldPhi->get_operand(i));
            auto *predBB = dynamic_cast<BasicBlock *>(
                mapOp(fix.oldPhi->get_operand(i + 1)));
            fix.newPhi->addIncoming(v, predBB);
        }
    }

    // ── Phase 2: 重写 ret 与 mergePhi 的 incoming ──
    //
    // kind side-channel: 0 表示 aux 返回的是可加到 acc 的 delta；1 表示 cap。
    auto zero = [&]() { return new ConstantInt(i32, 0); };
    auto one  = [&]() { return new ConstantInt(i32, 1); };

    for (auto &cr : clonedRets) {
        if (!cr.orig) continue;
        if (cr.newPhi) {
            PhiInst *phi = cr.newPhi;
            auto *kindPhi = PhiInst::create_phi(i32, cr.newRetBB);
            cr.newRetBB->add_instruction_front(kindPhi);
            const auto &incs = cr.orig->incomings;
            // PHI 操作数布局 [val0, bb0, val1, bb1, ...]
            // incs 顺序与原 PHI 一致；逐对处理
            for (size_t k = 0; k < incs.size(); ++k) {
                unsigned valIdx = 2 * k;
                unsigned bbIdx  = 2 * k + 1;
                BasicBlock *sourceBB = dynamic_cast<BasicBlock *>(
                    phi->get_operand(bbIdx));
                if (incs[k].kind == Pattern::IncomingInfo::ACC_DEP) {
                    phi->set_operand(valIdx, new ConstantInt(i32, incs[k].delta));
                    kindPhi->addIncoming(zero(), sourceBB);
                } else if (incs[k].kind == Pattern::IncomingInfo::CAP) {
                    phi->set_operand(valIdx, zero());
                    kindPhi->addIncoming(one(), sourceBB);
                } else {  // SELF_CALL_PASS
                    auto *newCallV = valMap.at(incs[k].selfCall);
                    Instruction *term = sourceBB->get_terminator();
                    sourceBB->delete_instr(term);
                    auto *childKind = new LoadInst(kindGV, sourceBB);
                    auto *isCap = new ICmpInst(ICmpInst::ICMP_NE, childKind, zero(),
                                               sourceBB);
                    auto *capBB = new BasicBlock(m, "aux_cap_" + sourceBB->name_, aux);
                    auto *valBB  = new BasicBlock(m, "aux_val_"  + sourceBB->name_, aux);
                    new BranchInst(isCap, capBB, valBB, sourceBB);
                    new BranchInst(cr.newRetBB, capBB);
                    auto *addV = new BinaryInst(i32, Instruction::Add, newCallV,
                                                  new ConstantInt(i32, incs[k].delta),
                                                  valBB);
                    new BranchInst(cr.newRetBB, valBB);
                    phi->set_operand(valIdx, zero());
                    phi->set_operand(bbIdx, capBB);
                    phi->addIncoming(addV, valBB);
                    kindPhi->addIncoming(one(), capBB);
                    kindPhi->addIncoming(zero(), valBB);
                }
            }
            auto *kindStore = new StoreInst(kindPhi, kindGV, cr.newRetBB, true);
            cr.newRetBB->add_instruction_before_inst(kindStore, cr.newRet);
        } else {
            // 直接 ret 单个值
            const Pattern::IncomingInfo &inc = cr.orig->incomings[0];
            if (inc.kind == Pattern::IncomingInfo::ACC_DEP) {
                cr.newRet->set_operand(0, new ConstantInt(i32, inc.delta));
                auto *kindStore = new StoreInst(zero(), kindGV, cr.newRetBB, true);
                cr.newRetBB->add_instruction_before_inst(kindStore, cr.newRet);
            } else if (inc.kind == Pattern::IncomingInfo::CAP) {
                cr.newRet->set_operand(0, zero());
                auto *kindStore = new StoreInst(one(), kindGV, cr.newRetBB, true);
                cr.newRetBB->add_instruction_before_inst(kindStore, cr.newRet);
            } else {  // SELF_CALL_PASS in retBB
                auto *newCallV = valMap.at(inc.selfCall);
                // 删除 newRet，改 retBB 终止符为 cond_br
                cr.newRetBB->delete_instr(cr.newRet);
                auto *childKind = new LoadInst(kindGV, cr.newRetBB);
                auto *isCap = new ICmpInst(ICmpInst::ICMP_NE, childKind, zero(),
                                           cr.newRetBB);
                auto *capBB = new BasicBlock(m, "aux_cap_" + cr.newRetBB->name_, aux);
                auto *valBB  = new BasicBlock(m, "aux_val_"  + cr.newRetBB->name_, aux);
                new BranchInst(isCap, capBB, valBB, cr.newRetBB);
                new StoreInst(one(), kindGV, capBB);
                new ReturnInst(zero(), capBB);
                auto *addV = new BinaryInst(i32, Instruction::Add, newCallV,
                                              new ConstantInt(i32, inc.delta),
                                              valBB);
                new StoreInst(zero(), kindGV, valBB);
                new ReturnInst(addV, valBB);
            }
        }
    }

    aux->set_instr_name();
    aux->invalidateDominatorInfo();
    return aux;
}

static void addKindAwareHashMemoization(Function *f, GlobalVariable *kindGV) {
    Module *m = f->parent_;
    auto *i32 = m->int32_ty_;
    unsigned nArgs = f->arguments_.size();
    unsigned slotFields = 1 + nArgs + 2; // valid + keys + value + kind
    unsigned totalI32 = AutoMemoization::HASH_SLOTS * slotFields;

    static constexpr int P1 = 0x45D9F3B;
    static constexpr int P2 = 0x119DE1F3;

    Type *arrTy = m->get_array_type(i32, totalI32);
    auto *hashGV = new GlobalVariable("__memo_hash_" + f->name_, m, arrTy,
                                       false, new ConstantZero(arrTy));

    BasicBlock *oldEntry = f->basic_blocks_.front();
    if (oldEntry->name_ == "label_entry")
        oldEntry->name_ = "label_entry_uncached";

    auto *newEntry = new BasicBlock(m, "label_entry", f);
    auto *checkKey = new BasicBlock(m, "memo_check_key", f);
    auto *hitBB = new BasicBlock(m, "memo_hit", f);

    auto &bbs = f->basic_blocks_;
    auto pull = [&](BasicBlock *bb) {
        bbs.erase(std::remove(bbs.begin(), bbs.end(), bb), bbs.end());
    };
    pull(newEntry);
    pull(checkKey);
    pull(hitBB);
    bbs.insert(bbs.begin(), {newEntry, checkKey, hitBB});

    auto *builder = new IRStmtBuilder(newEntry, m);

    auto computeBase = [&](BasicBlock *bb) -> Value * {
        Value *hashVal = nullptr;
        for (unsigned i = 0; i < nArgs; ++i) {
            int p = (i == 0) ? P1 : P2;
            auto *mul = new BinaryInst(i32, Instruction::Mul,
                                       f->arguments_[i],
                                       new ConstantInt(i32, p), bb);
            if (!hashVal)
                hashVal = mul;
            else
                hashVal = new BinaryInst(i32, Instruction::Xor, hashVal, mul, bb);
        }
        auto *idx = new BinaryInst(
            i32, Instruction::And, hashVal,
            new ConstantInt(i32, static_cast<int>(AutoMemoization::HASH_MASK)), bb);
        return new BinaryInst(i32, Instruction::Mul, idx,
                              new ConstantInt(i32, static_cast<int>(slotFields)), bb);
    };

    auto gepSlot = [&](Value *base, unsigned fieldIdx, BasicBlock *bb) -> Value * {
        Value *off = base;
        if (fieldIdx != 0) {
            off = new BinaryInst(i32, Instruction::Add, base,
                                 new ConstantInt(i32, static_cast<int>(fieldIdx)), bb);
        }
        std::vector<Value *> idxs = {new ConstantInt(i32, 0), off};
        return new GetElementPtrInst(hashGV, idxs, bb);
    };

    Value *base = computeBase(newEntry);
    Value *validPtr = gepSlot(base, 0, newEntry);
    builder->set_insert_point(newEntry);
    auto *validVal = builder->create_load(validPtr);
    auto *isValid = builder->create_icmp_ne(validVal, new ConstantInt(i32, 0));
    builder->create_cond_br(isValid, checkKey, oldEntry);

    Value *baseCK = computeBase(checkKey);
    Value *allEq = nullptr;
    for (unsigned i = 0; i < nArgs; ++i) {
        Value *keyPtr = gepSlot(baseCK, 1 + i, checkKey);
        auto *load = new LoadInst(keyPtr, checkKey);
        auto *eq = new ICmpInst(ICmpInst::ICMP_EQ, load, f->arguments_[i], checkKey);
        if (!allEq)
            allEq = eq;
        else
            allEq = new BinaryInst(m->int1_ty_, Instruction::And, allEq, eq, checkKey);
    }
    builder->set_insert_point(checkKey);
    builder->create_cond_br(allEq, hitBB, oldEntry);

    Value *baseHit = computeBase(hitBB);
    Value *valPtrHit = gepSlot(baseHit, 1 + nArgs, hitBB);
    Value *kindPtrHit = gepSlot(baseHit, 2 + nArgs, hitBB);
    builder->set_insert_point(hitBB);
    auto *cachedVal = builder->create_load(valPtrHit);
    auto *cachedKind = builder->create_load(kindPtrHit);
    builder->create_store(cachedKind, kindGV);
    builder->create_ret(cachedVal);

    std::vector<std::pair<BasicBlock *, ReturnInst *>> retSites;
    for (auto *bb : f->basic_blocks_) {
        if (bb == newEntry || bb == checkKey || bb == hitBB)
            continue;
        auto *term = bb->get_terminator();
        if (term && term->is_ret() && term->num_ops_ > 0)
            retSites.push_back({bb, static_cast<ReturnInst *>(term)});
    }

    for (auto &site : retSites) {
        BasicBlock *retBB = site.first;
        ReturnInst *retInst = site.second;
        Value *retVal = retInst->get_operand(0);

        retBB->delete_instr(retInst);
        Value *baseRet = computeBase(retBB);

        builder->set_insert_point(retBB);
        for (unsigned i = 0; i < nArgs; ++i) {
            Value *keyPtr = gepSlot(baseRet, 1 + i, retBB);
            builder->create_store(f->arguments_[i], keyPtr);
        }
        Value *valPtrRet = gepSlot(baseRet, 1 + nArgs, retBB);
        builder->create_store(retVal, valPtrRet);
        auto *kindVal = builder->create_load(kindGV);
        Value *kindPtrRet = gepSlot(baseRet, 2 + nArgs, retBB);
        builder->create_store(kindVal, kindPtrRet);
        Value *validPtrRet = gepSlot(baseRet, 0, retBB);
        builder->create_store(new ConstantInt(i32, 1), validPtrRet);
        builder->create_ret(retVal);
    }

    f->set_instr_name();
    f->invalidateDominatorInfo();
    delete builder;
}

// 把原 f 的函数体替换为 `kind ? capConst : value + acc`。
static void rewriteOriginal(const Pattern &pat, Function *aux, GlobalVariable *kindGV) {
    Function *f = pat.f;
    Module *m = f->parent_;

    // 清空 f 的现有 BB
    // 注意：要安全地删除，且断开 use-def。最简单的做法：删除每个 BB 内每条 inst
    // 的所有操作数引用，然后销毁 BB。但是销毁 BB 本身会处理。这里手工 detach。
    // 简化路径：把所有 BB 标记为不可达后只保留一个新的 entry。
    // 直接 erase 不安全（指令的 use_list_ 仍然挂在被它引用的 Value 上）。
    // 我们走"逐条删指令"路径，让 BB::delete_instr 处理 use-def。
    std::vector<BasicBlock *> oldBBs = f->basic_blocks_;
    f->basic_blocks_.clear();
    f->invalidateDominatorInfo();

    auto *newEntry = new BasicBlock(m, "label_entry", f);
    // 注意 add_basic_block 在 BasicBlock ctor 内已被调用

    IRStmtBuilder builder(newEntry, m);

    // aux 实参 = f 的非 acc 形参
    std::vector<Value *> auxArgs;
    for (unsigned i = 0; i < f->arguments_.size(); ++i) {
        if (i == pat.accIdx) continue;
        auxArgs.push_back(f->arguments_[i]);
    }
    auto *call = builder.create_call(aux, auxArgs);
    auto *kind = builder.create_load(kindGV);
    auto *isCap = builder.create_icmp_ne(kind, new ConstantInt(m->int32_ty_, 0));
    auto *sum  = builder.create_iadd(call, f->arguments_[pat.accIdx]);
    auto *sel  = new SelectInst(isCap,
                                  new ConstantInt(m->int32_ty_, pat.capConst),
                                  sum,
                                  newEntry);
    builder.create_ret(sel);

    // 销毁旧 BB（先删指令，再从内存里抹掉）
    for (auto *bb : oldBBs) {
        // delete_instr 会清 use-def；逐条删
        // 注意 instr_list_ 在 delete 时变化，复制一份
        std::vector<Instruction *> toDel(bb->instr_list_.begin(),
                                          bb->instr_list_.end());
        for (auto *inst : toDel) bb->delete_instr(inst);
        // 清空前驱/后继
        bb->pre_bbs_.clear();
        bb->succ_bbs_.clear();
    }
    // BasicBlock 析构是由 Function 析构链处理的；这里仅从 f->basic_blocks_ 里
    // 拿掉就够（已 clear）。被泄露的 BB 对象不影响正确性（短生命周期 IR）。

    f->set_instr_name();
    f->invalidateDominatorInfo();
}

}  // namespace

void AccumElim::execute(Module *module) {
    std::vector<Function *> originals;
    std::vector<Pattern> patterns;

    for (auto *f : module->function_list_) {
        if (f->is_declaration()) continue;
        Pattern pat;
        if (!detect(f, pat)) continue;
        patterns.push_back(std::move(pat));
        originals.push_back(f);
    }

    for (auto &pat : patterns) {
        GlobalVariable *kindGV = nullptr;
        Function *aux = buildAux(pat, kindGV);
        if (!aux) continue;       // 克隆失败（含不支持指令），跳过这个候选
        rewriteOriginal(pat, aux, kindGV);
        addKindAwareHashMemoization(aux, kindGV);
    }
}
