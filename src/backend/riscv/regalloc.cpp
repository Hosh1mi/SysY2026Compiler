#include "../../include/backend/riscv/regalloc.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
bool isFloatTy(Type *t) { return t && t->tid_ == Type::FloatTyID; }
bool isGprTy(Type *t) {
    return t && (t->tid_ == Type::IntegerTyID || t->tid_ == Type::PointerTyID);
}
}  // namespace

bool RiscvRegAlloc::canAssignRegister(Value *v) const {
    if (!v || dynamic_cast<Constant *>(v) || dynamic_cast<GlobalVariable *>(v))
        return false;
    if (auto *inst = dynamic_cast<Instruction *>(v)) {
        if (inst->is_void() || inst->is_alloca())
            return false;
    }
    return isFloatTy(v->type_) || isGprTy(v->type_);
}

void RiscvRegAlloc::colorPool(const std::vector<Interval> &pool,
                              const std::vector<std::string> &colorToReg,
                              int callerSavedColors,
                              const std::set<Value *> &requiresCalleeSaved,
                              const std::map<Value *, double> &spillCost,
                              const std::map<Value *, std::set<Value *>> &phiAffinity,
                              const std::function<bool(Value *, Value *)> &trulyInterferes) {
    if (pool.empty()) return;
    int K = (int)colorToReg.size();

    std::vector<Interval> sorted = pool;
    std::sort(sorted.begin(), sorted.end(),
              [](const Interval &a, const Interval &b) { return a.start < b.start; });
    std::map<Value *, Interval> intervalForValue;
    for (auto &iv : sorted) intervalForValue[iv.value] = iv;

    // 干涉图：区间按 start 排序，i 与 j(>i) 相交 ⇔ j.start < i.end。
    std::map<Value *, std::set<Value *>> adj;
    for (auto &iv : sorted) adj[iv.value];
    for (size_t i = 0; i < sorted.size(); i++) {
        for (size_t j = i + 1; j < sorted.size() && sorted[j].start < sorted[i].end; j++) {
            adj[sorted[i].value].insert(sorted[j].value);
            adj[sorted[j].value].insert(sorted[i].value);
        }
    }

    // ---- Phi 合并（Briggs 保守准则）----
    std::map<Value *, Value *> ufParent;
    std::function<Value *(Value *)> ufFind = [&](Value *v) -> Value * {
        auto it = ufParent.find(v);
        if (it == ufParent.end()) return v;
        Value *root = ufFind(it->second);
        ufParent[v] = root;
        return root;
    };

    std::map<Value *, double> cost;
    for (auto &iv : sorted) {
        auto it = spillCost.find(iv.value);
        cost[iv.value] = (it != spillCost.end()) ? it->second : 1.0;
    }

    std::vector<std::pair<Value *, Value *>> affEdges;
    for (auto &iv : sorted) {
        auto it = phiAffinity.find(iv.value);
        if (it == phiAffinity.end()) continue;
        for (auto *p : it->second)
            if (iv.value < p && intervalForValue.count(p))
                affEdges.push_back({iv.value, p});
    }

    std::map<Value *, std::vector<Value *>> members;
    std::map<Value *, bool> calleeOnly;
    for (auto &iv : sorted) {
        members[iv.value] = {iv.value};
        calleeOnly[iv.value] = requiresCalleeSaved.count(iv.value) != 0;
    }

    bool mergedAny = true;
    while (mergedAny) {
        mergedAny = false;
        for (auto &e : affEdges) {
            Value *a = ufFind(e.first);
            Value *b = ufFind(e.second);
            if (a == b) continue;
            if (adj[a].count(b)) {
                bool conflict = false;
                for (auto *x : members[a]) {
                    for (auto *y : members[b])
                        if (trulyInterferes(x, y)) { conflict = true; break; }
                    if (conflict) break;
                }
                if (conflict) continue;
            }
            std::set<Value *> nbrs = adj[a];
            nbrs.insert(adj[b].begin(), adj[b].end());
            nbrs.erase(a);
            nbrs.erase(b);
            bool mergedCalleeOnly = calleeOnly[a] || calleeOnly[b];
            int availableColors = mergedCalleeOnly ? K - callerSavedColors : K;
            int highDegree = 0;
            for (auto *n : nbrs) {
                int d = (int)adj[n].size();
                if (adj[n].count(a) && adj[n].count(b)) d--;
                if (d >= availableColors) highDegree++;
            }
            if (highDegree >= availableColors) continue;

            for (auto *n : adj[b]) {
                adj[n].erase(b);
                adj[n].insert(a);
                adj[a].insert(n);
            }
            adj.erase(b);
            adj[a].erase(a);
            ufParent[b] = a;
            intervalForValue[a].crossesCall =
                intervalForValue[a].crossesCall || intervalForValue[b].crossesCall;
            cost[a] = std::max(cost[a], cost[b]);
            calleeOnly[a] = mergedCalleeOnly;
            auto &ma = members[a];
            auto &mb = members[b];
            ma.insert(ma.end(), mb.begin(), mb.end());
            members.erase(b);
            mergedAny = true;
        }
    }

    std::set<Value *> worklist;
    for (auto &kv : adj) worklist.insert(kv.first);
    std::vector<Value *> stack;
    std::set<Value *> potentialSpills;

    std::vector<Value *> nodes(worklist.begin(), worklist.end());
    int N = (int)nodes.size();
    std::unordered_map<Value *, int> idOf;
    idOf.reserve(N * 2);
    for (int i = 0; i < N; i++) idOf[nodes[i]] = i;

    std::vector<std::vector<int>> nbr(N);
    std::vector<int> deg(N);
    std::vector<double> costV(N);
    std::vector<char> inWL(N, 1);
    for (int i = 0; i < N; i++) {
        auto &a = adj[nodes[i]];
        nbr[i].reserve(a.size());
        for (auto *m : a) {
            auto it = idOf.find(m);
            if (it != idOf.end()) nbr[i].push_back(it->second);
        }
        deg[i] = (int)nbr[i].size();
        costV[i] = cost[nodes[i]];
    }

    int remaining = N;
    while (remaining > 0) {
        int pick = -1;
        for (int i = 0; i < N; i++) {
            int availableColors = calleeOnly[nodes[i]] ? K - callerSavedColors : K;
            if (inWL[i] && deg[i] < availableColors) { pick = i; break; }
        }
        if (pick < 0) {
            double bestCost = 1e100;
            for (int i = 0; i < N; i++) {
                if (!inWL[i]) continue;
                double c = costV[i] / (deg[i] + 1);
                if (c < bestCost) { bestCost = c; pick = i; }
            }
            potentialSpills.insert(nodes[pick]);
        }
        stack.push_back(nodes[pick]);
        inWL[pick] = 0;
        remaining--;
        for (int n : nbr[pick])
            if (inWL[n]) deg[n]--;
    }

    std::map<Value *, int> colors;
    while (!stack.empty()) {
        Value *v = stack.back();
        stack.pop_back();

        std::set<int> neighborColors;
        for (auto n : adj[v]) {
            auto it = colors.find(n);
            if (it != colors.end()) neighborColors.insert(it->second);
        }

        auto colorAllowed = [&](int c) {
            return c >= 0 && c < K &&
                   (!calleeOnly[ufFind(v)] || c >= callerSavedColors);
        };

        int color = -1;
        auto affIt = phiAffinity.find(v);
        if (affIt != phiAffinity.end()) {
            for (auto partner : affIt->second) {
                auto pc = colors.find(ufFind(partner));
                if (pc != colors.end() && colorAllowed(pc->second) &&
                    !neighborColors.count(pc->second)) {
                    color = pc->second;
                    break;
                }
            }
        }
        if (color < 0) {
            for (int c = 0; c < K; c++)
                if (colorAllowed(c) && !neighborColors.count(c)) {
                    color = c;
                    break;
                }
        }
        if (color >= 0) {
            colors[v] = color;
            potentialSpills.erase(v);
        }
    }

    for (auto &iv : sorted) {
        Value *v = iv.value;
        auto it = colors.find(ufFind(v));
        if (it == colors.end()) continue;
        assignedRegs_[v] = colorToReg[it->second];
    }
}

void RiscvRegAlloc::allocate() {
    std::map<Value *, int> defPos;
    std::map<Value *, int> lastUse;
    std::vector<Interval> intervals;

    // ---- 1. RPO 块序 & 前驱表 ----
    std::map<BasicBlock *, std::vector<BasicBlock *>> preds;
    std::vector<BasicBlock *> blocksOrder;
    {
        std::set<BasicBlock *> visited;
        std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
            visited.insert(bb);
            auto term = bb->get_terminator();
            if (term)
                for (unsigned i = 0; i < term->num_ops_; ++i)
                    if (auto succ = dynamic_cast<BasicBlock *>(term->get_operand(i)))
                        if (!visited.count(succ)) dfs(succ);
            blocksOrder.push_back(bb);
        };
        if (!func_->basic_blocks_.empty()) dfs(func_->basic_blocks_[0]);
        for (auto bb : func_->basic_blocks_)
            if (!visited.count(bb)) dfs(bb);
        std::reverse(blocksOrder.begin(), blocksOrder.end());
    }
    for (auto bb : blocksOrder) {
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i)
            if (auto succ = dynamic_cast<BasicBlock *>(term->get_operand(i)))
                preds[succ].push_back(bb);
    }

    // Caller-saved 颜色排在前面，使短生命周期值优先避免序言/尾声保存。
    // t0-t6/ft0-ft2 是指令选择 scratch，不得分配。
    std::vector<std::string> intColorToReg = {
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11"};
    std::vector<std::string> floatColorToReg = {
        "ft3", "ft4", "ft5", "ft6", "ft7", "ft8", "ft9", "ft10", "ft11",
        "fs0", "fs1", "fs2", "fs3", "fs4", "fs5",
        "fs6", "fs7", "fs8", "fs9", "fs10", "fs11"};

    // ---- 2. 指令编号 + def/lastUse ----
    std::map<BasicBlock *, int> blockStart, blockEnd;
    int idx = 0;
    for (auto arg : func_->arguments_) {
        if (canAssignRegister(arg)) {
            defPos[arg] = 0;
            lastUse[arg] = 0;
        }
    }
    for (auto bb : blocksOrder) {
        if (bb->instr_list_.empty()) {
            blockStart[bb] = blockEnd[bb] = idx;
            continue;
        }
        blockStart[bb] = idx + 1;
        for (auto inst : bb->instr_list_) {
            ++idx;
            if (canAssignRegister(inst)) {
                defPos[inst] = idx;
                lastUse[inst] = idx;
            }
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val))
                    lastUse[val] = std::max(lastUse[val], idx);
            }
        }
        blockEnd[bb] = idx;
    }

    // ---- 3. 活跃性数据流（LiveIn/LiveOut）----
    std::map<BasicBlock *, std::set<Value *>> phiOut;
    for (auto bb : blocksOrder)
        for (auto inst : bb->instr_list_) {
            auto phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) continue;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto val = phi->get_operand(i);
                auto pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                if (canAssignRegister(val)) phiOut[pred].insert(val);
            }
        }

    struct BBInfo { std::set<Value *> def, use; };
    std::map<BasicBlock *, BBInfo> bbInfo;
    for (auto bb : blocksOrder) {
        BBInfo info;
        for (auto inst : bb->instr_list_) {
            if (auto phi = dynamic_cast<PhiInst *>(inst)) {
                if (canAssignRegister(phi)) info.def.insert(phi);
                continue;
            }
            if (canAssignRegister(inst)) info.def.insert(inst);
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val) && !info.def.count(val)) info.use.insert(val);
            }
        }
        bbInfo[bb] = info;
    }

    bool changed;
    std::map<BasicBlock *, std::set<Value *>> liveIn, liveOut;
    do {
        changed = false;
        for (auto bb : blocksOrder) {
            std::set<Value *> newIn, newOut;
            auto term = bb->get_terminator();
            if (term)
                for (unsigned i = 0; i < term->num_ops_; ++i)
                    if (auto succ = dynamic_cast<BasicBlock *>(term->get_operand(i)))
                        for (auto v : liveIn[succ]) newOut.insert(v);
            for (auto v : phiOut[bb]) newOut.insert(v);
            auto &info = bbInfo[bb];
            for (auto v : info.use) newIn.insert(v);
            for (auto v : newOut)
                if (!info.def.count(v)) newIn.insert(v);
            if (newIn != liveIn[bb] || newOut != liveOut[bb]) changed = true;
            liveIn[bb] = std::move(newIn);
            liveOut[bb] = std::move(newOut);
        }
    } while (changed);

    std::set<Value *> crossesCallValues;
    for (auto bb : blocksOrder) {
        std::set<Value *> live = liveOut[bb];
        for (auto it = bb->instr_list_.rbegin(); it != bb->instr_list_.rend(); ++it) {
            Instruction *inst = *it;
            if (inst->is_call()) {
                std::set<Value *> liveAfterCall = live;
                if (canAssignRegister(inst)) liveAfterCall.erase(inst);
                for (auto v : liveAfterCall)
                    if (canAssignRegister(v)) crossesCallValues.insert(v);
            }
            if (canAssignRegister(inst)) live.erase(inst);
            if (auto phi = dynamic_cast<PhiInst *>(inst)) {
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    auto val = phi->get_operand(i);
                    if (canAssignRegister(val)) live.insert(val);
                }
                continue;
            }
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val)) live.insert(val);
            }
        }
    }

    // ---- 4. 活跃区间 ----
    for (auto &entry : defPos) {
        Value *v = entry.first;
        if (v->use_list_.empty()) continue;
        int start = entry.second;
        int end = lastUse[v];
        for (auto bb : blocksOrder)
            if (liveOut[bb].count(v)) end = std::max(end, blockEnd[bb]);
        if (start == 0 && end == 0 && dynamic_cast<Argument *>(v)) continue;
        if (end >= start) {
            intervals.push_back({v, start, end, isFloatTy(v->type_),
                                 crossesCallValues.count(v) > 0});
        }
    }

    // ---- 5. 支配集（迭代求解，用于循环深度/溢出代价）----
    BasicBlock *entry = func_->basic_blocks_[0];
    std::map<BasicBlock *, std::set<BasicBlock *>> doms;
    for (auto bb : blocksOrder) {
        if (bb == entry) doms[bb] = {entry};
        else for (auto b : blocksOrder) doms[bb].insert(b);
    }
    bool domChanged;
    do {
        domChanged = false;
        for (auto bb : blocksOrder) {
            if (bb == entry) continue;
            std::set<BasicBlock *> inter;
            bool firstPred = true;
            for (auto pred : preds[bb]) {
                if (firstPred) { inter = doms[pred]; firstPred = false; }
                else {
                    std::set<BasicBlock *> temp;
                    for (auto b : inter)
                        if (doms[pred].count(b)) temp.insert(b);
                    inter = std::move(temp);
                }
            }
            inter.insert(bb);
            if (inter != doms[bb]) { doms[bb] = std::move(inter); domChanged = true; }
        }
    } while (domChanged);

    std::map<BasicBlock *, int> loopDepth;
    for (auto bb : blocksOrder) loopDepth[bb] = 0;
    for (auto bb : blocksOrder) {
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            auto succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
            if (!succ || !doms[bb].count(succ)) continue;
            std::set<BasicBlock *> loopBlocks;
            std::function<void(BasicBlock *)> collect = [&](BasicBlock *b) {
                if (!loopBlocks.insert(b).second) return;
                if (b == succ) return;
                for (auto pred : preds[b]) collect(pred);
            };
            collect(bb);
            loopBlocks.insert(succ);
            for (auto b : loopBlocks) loopDepth[b]++;
        }
    }

    // ---- 6. phi 亲和 ----
    std::map<Value *, std::set<Value *>> phiAffinity;
    for (auto bb : blocksOrder)
        for (auto inst : bb->instr_list_) {
            auto phi = dynamic_cast<PhiInst *>(inst);
            if (!phi || !canAssignRegister(phi)) continue;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto val = phi->get_operand(i);
                if (canAssignRegister(val)) {
                    phiAffinity[phi].insert(val);
                    phiAffinity[val].insert(phi);
                }
            }
        }

    // ---- 7. 溢出代价：每次使用按 20^loopDepth 累加 ----
    std::map<Value *, double> spillCost;
    for (auto &iv : intervals) {
        double c = 0;
        for (auto &use : iv.value->use_list_) {
            auto inst = dynamic_cast<Instruction *>(use.val_);
            if (!inst) continue;
            c += std::pow(20.0, loopDepth[inst->parent_]);
        }
        if (dynamic_cast<Argument *>(iv.value)) c /= 2.0;
        if (c < 1.0) c = 1.0;
        spillCost[iv.value] = c;
    }
    for (auto &[v, partners] : phiAffinity) {
        double maxCost = spillCost.count(v) ? spillCost[v] : 1.0;
        for (auto *partner : partners)
            if (spillCost.count(partner)) maxCost = std::max(maxCost, spillCost[partner]);
        if (spillCost.count(v)) spillCost[v] = maxCost;
        for (auto *partner : partners)
            if (spillCost.count(partner)) spillCost[partner] = maxCost;
    }

    // Values involved in ABI entry/call parallel moves stay in callee-saved
    // colors. This avoids assigning an incoming argument or call operand to
    // a*/fa* and then overwriting another source while moves are emitted.
    std::set<Value *> requiresCalleeSaved = crossesCallValues;
    for (auto *arg : func_->arguments_)
        if (canAssignRegister(arg)) requiresCalleeSaved.insert(arg);
    for (auto *bb : blocksOrder) {
        for (auto *inst : bb->instr_list_) {
            if (!inst->is_call()) continue;
            for (unsigned i = 0; i + 1 < inst->num_ops_; ++i) {
                Value *arg = inst->get_operand(i);
                if (canAssignRegister(arg)) requiresCalleeSaved.insert(arg);
            }
        }
    }

    // ---- 8. 分类 ----
    std::vector<Interval> intPool, floatPool;
    for (auto &iv : intervals) {
        if (iv.isFloat) floatPool.push_back(iv);
        else intPool.push_back(iv);
    }

    // ---- 9. 精确 SSA 干涉判定（仅用于 affinity 合并）----
    auto liveAtDefsCache = std::make_shared<std::map<Value *, std::set<Value *>>>();
    auto liveAtDefs = [this, liveAtDefsCache, &liveIn, &liveOut, &preds](Value *v)
        -> const std::set<Value *> & {
        auto it = liveAtDefsCache->find(v);
        if (it != liveAtDefsCache->end()) return it->second;
        std::set<Value *> live;
        if (auto *phi = dynamic_cast<PhiInst *>(v)) {
            BasicBlock *bb = phi->parent_;
            live = liveIn[bb];
            for (auto *inst : bb->instr_list_) {
                auto *p2 = dynamic_cast<PhiInst *>(inst);
                if (p2 && p2 != phi && canAssignRegister(p2)) live.insert(p2);
            }
            for (auto *pred : preds[bb]) {
                Value *incoming = nullptr;
                for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2)
                    if (phi->get_operand(i + 1) == pred) { incoming = phi->get_operand(i); break; }
                for (auto *x : liveOut[pred])
                    if (x != incoming) live.insert(x);
            }
        } else if (auto *inst = dynamic_cast<Instruction *>(v)) {
            BasicBlock *bb = inst->parent_;
            live = liveOut[bb];
            for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend(); ++rit) {
                Instruction *cur = *rit;
                if (cur == inst) break;
                if (canAssignRegister(cur)) live.erase(cur);
                if (dynamic_cast<PhiInst *>(cur)) continue;
                for (unsigned i = 0; i < cur->num_ops_; ++i) {
                    auto *op = cur->get_operand(i);
                    if (canAssignRegister(op)) live.insert(op);
                }
            }
        } else {
            BasicBlock *entryBB = func_->basic_blocks_[0];
            live = liveIn[entryBB];
            for (auto *arg : func_->arguments_)
                if (arg != v && canAssignRegister(arg)) live.insert(arg);
        }
        live.erase(v);
        auto &slot = (*liveAtDefsCache)[v];
        slot = std::move(live);
        return slot;
    };
    std::function<bool(Value *, Value *)> trulyInterferes =
        [liveAtDefs](Value *a, Value *b) -> bool {
            return liveAtDefs(a).count(b) > 0 || liveAtDefs(b).count(a) > 0;
        };

    // ---- 10. 着色 ----
    colorPool(intPool, intColorToReg, /*callerSavedColors=*/8,
              requiresCalleeSaved, spillCost, phiAffinity, trulyInterferes);
    colorPool(floatPool, floatColorToReg, /*callerSavedColors=*/9,
              requiresCalleeSaved, spillCost, phiAffinity, trulyInterferes);
}
