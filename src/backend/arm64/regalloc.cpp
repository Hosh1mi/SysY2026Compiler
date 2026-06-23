#include "../../include/backend/arm64/regalloc.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

Arm64RegAlloc::Arm64RegAlloc(Function *f) : func_(f) {}

const std::map<Value*, std::string> &Arm64RegAlloc::assignedRegs() const {
    return assignedRegs_;
}

const std::map<int, std::string> &Arm64RegAlloc::promotedConsts() const {
    return promotedConsts_;
}

bool Arm64RegAlloc::hasAssignedReg(Value *v) const {
    return assignedRegs_.count(v) > 0;
}

std::string Arm64RegAlloc::assignedReg(Value *v, bool asAddress) const {
    auto it = assignedRegs_.find(v);
    if (it == assignedRegs_.end()) return "";
    if (asAddress && !it->second.empty() && it->second[0] == 'w') {
        return "x" + it->second.substr(1);
    }
    return it->second;
}

bool Arm64RegAlloc::canAssignRegister(Value *v) const {
    if (!v || dynamic_cast<Constant*>(v) || dynamic_cast<GlobalVariable*>(v)) return false;
    if (auto *inst = dynamic_cast<Instruction*>(v)) {
        if (inst->is_void() || inst->is_alloca()) {
            return false;
        }
    }
    return isAllocatableIntValue(v->type_) || isAllocatableFloatValue(v->type_) || isAllocatablePtrValue(v->type_) || isAllocatableNEONValue(v->type_);
}

void Arm64RegAlloc::colorPool(const std::vector<Interval> &pool,
                               const std::vector<int> &colorToReg, bool isFloat,
                               const std::set<int> &callerSavedRegs,
                               const std::map<Value*, double> &spillCost,
                               const std::map<Value*, std::set<Value*>> &phiAffinity,
                               const std::function<bool(Value*, Value*)> &trulyInterferes) {
    if (pool.empty()) return;
    int K = (int)colorToReg.size();

    std::vector<Interval> sorted = pool;
    std::sort(sorted.begin(), sorted.end(),
              [](const Interval &a, const Interval &b) { return a.start < b.start; });
    std::map<Value*, Interval> intervalForValue;
    for (auto &iv : sorted) intervalForValue[iv.value] = iv;

    // Build interference graph
    std::map<Value*, std::set<Value*>> adj;
    for (auto &iv : sorted) adj[iv.value];
    for (size_t i = 0; i < sorted.size(); i++) {
        for (size_t j = i + 1; j < sorted.size() && sorted[j].start < sorted[i].end; j++) {
            adj[sorted[i].value].insert(sorted[j].value);
            adj[sorted[j].value].insert(sorted[i].value);
        }
    }

    // ---- Phi coalescing（Briggs 保守准则）----
    // 把 phi 相连且不冲突的活跃区间在着色前合并成一个节点，使 phi 拷贝
    // 退化为自搬移（由 peephole 删除）。Briggs 准则保证合并后的节点
    // 高度数邻居 < K，不会让图变得更难着色，因此不会引入新的 spill。
    // 冲突判定：凸包不重叠（adj 无边）⇒ 必然安全；凸包重叠时再用
    // trulyInterferes 做基于真实活跃性的精确判定——循环 phi 的凸包
    // 横跨整个循环，与 backedge incoming 必然凸包重叠，但真实活跃
    // 区间通常不冲突（incoming 死于并行拷贝处），精确判定能合并它们。
    // 着色阶段仍使用凸包邻接（合并节点取成员邻接之并），保守安全。
    // phi 拷贝发射端（emitPhiCopies）具备并行拷贝语义，撞号/成环均能处理。
    std::map<Value*, Value*> ufParent;
    std::function<Value*(Value*)> ufFind = [&](Value *v) -> Value* {
        auto it = ufParent.find(v);
        if (it == ufParent.end()) return v;
        Value *root = ufFind(it->second);
        ufParent[v] = root;
        return root;
    };

    std::map<Value*, double> cost;
    for (auto &iv : sorted) {
        auto it = spillCost.find(iv.value);
        cost[iv.value] = (it != spillCost.end()) ? it->second : 1.0;
    }

    std::vector<std::pair<Value*, Value*>> affEdges;
    for (auto &iv : sorted) {
        auto it = phiAffinity.find(iv.value);
        if (it == phiAffinity.end()) continue;
        for (auto *p : it->second) {
            if (iv.value < p && intervalForValue.count(p))
                affEdges.push_back({iv.value, p});
        }
    }

    std::map<Value*, std::vector<Value*>> members;
    for (auto &iv : sorted) members[iv.value] = {iv.value};

    bool mergedAny = true;
    while (mergedAny) {
        mergedAny = false;
        for (auto &e : affEdges) {
            Value *a = ufFind(e.first);
            Value *b = ufFind(e.second);
            if (a == b) continue;
            if (adj[a].count(b)) {
                // 凸包重叠 ≠ 真干涉：对两个类的成员做精确两两检测
                bool conflict = false;
                for (auto *x : members[a]) {
                    for (auto *y : members[b]) {
                        if (trulyInterferes(x, y)) { conflict = true; break; }
                    }
                    if (conflict) break;
                }
                if (conflict) continue;
            }

            // Briggs：合并节点的"高度数（>=K）邻居"个数必须 < K
            std::set<Value*> nbrs = adj[a];
            nbrs.insert(adj[b].begin(), adj[b].end());
            nbrs.erase(a);
            nbrs.erase(b);
            int highDegree = 0;
            for (auto *n : nbrs) {
                int d = (int)adj[n].size();
                if (adj[n].count(a) && adj[n].count(b)) d--;  // 合并后只算一个邻居
                if (d >= K) highDegree++;
            }
            if (highDegree >= K) continue;

            // 把 b 并入 a
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
            auto &ma = members[a];
            auto &mb = members[b];
            ma.insert(ma.end(), mb.begin(), mb.end());
            members.erase(b);
            mergedAny = true;
        }
    }

    std::set<Value*> worklist;
    for (auto &kv : adj) worklist.insert(kv.first);
    std::vector<Value*> stack;
    std::set<Value*> potentialSpills;

    // Simplify phase
    std::vector<Value*> nodes(worklist.begin(), worklist.end());
    int N = (int)nodes.size();
    std::unordered_map<Value*, int> idOf;
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
            if (inWL[i] && deg[i] < K) { pick = i; break; }
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

    // Select phase
    std::map<Value*, int> colors;
    while (!stack.empty()) {
        Value *v = stack.back();
        stack.pop_back();

        std::set<int> neighborColors;
        for (auto n : adj[v]) {
            auto it = colors.find(n);
            if (it != colors.end()) neighborColors.insert(it->second);
        }

        const Interval &iv = intervalForValue[v];
        auto colorAllowed = [&](int c) {
            if (c < 0 || c >= K) return false;
            int regNo = colorToReg[c];
            return !iv.crossesCall || !callerSavedRegs.count(regNo);
        };

        int color = -1;
        // Biased: prefer color of already-colored phi partners
        // （没能合并的 affinity 对仍可通过同色偏好消除拷贝）
        auto affIt = phiAffinity.find(v);
        if (affIt != phiAffinity.end()) {
            for (auto partner : affIt->second) {
                auto pc = colors.find(ufFind(partner));
                if (pc != colors.end() &&
                    colorAllowed(pc->second) &&
                    !neighborColors.count(pc->second)) {
                    color = pc->second;
                    break;
                }
            }
        }
        if (color < 0) {
            for (int c = 0; c < K; c++) {
                if (colorAllowed(c) && !neighborColors.count(c)) { color = c; break; }
            }
        }
        if (color >= 0) {
            colors[v] = color;
            potentialSpills.erase(v);
        }
    }

    // Record assignments：每个原始值取其合并代表元的颜色，
    // 寄存器名按值自身的类型决定（w/x 同号同寄存器）
    for (auto &iv : sorted) {
        Value *v = iv.value;
        auto it = colors.find(ufFind(v));
        if (it == colors.end()) continue;
        int regNo = colorToReg[it->second];
        if (isFloat) {
            assignedRegs_[v] = "s" + std::to_string(regNo);
        } else if (isAllocatableNEONValue(v->type_)) {
            assignedRegs_[v] = "v" + std::to_string(regNo);
        } else if (isAllocatablePtrValue(v->type_)) {
            assignedRegs_[v] = "x" + std::to_string(regNo);
        } else {
            assignedRegs_[v] = "w" + std::to_string(regNo);
        }
    }
}

std::vector<BasicBlock*> Arm64RegAlloc::computeBlockOrder(
    std::map<BasicBlock*, std::vector<BasicBlock*>> &preds) const {
    std::vector<BasicBlock*> blocksOrder;

    {
        std::set<BasicBlock*> visited;
        std::function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
            visited.insert(bb);
            auto term = bb->get_terminator();
            if (term) {
                for (unsigned i = 0; i < term->num_ops_; ++i) {
                    if (auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                        if (!visited.count(succ))
                            dfs(succ);
                    }
                }
            }
            blocksOrder.push_back(bb);
        };

        if (!func_->basic_blocks_.empty())
            dfs(func_->basic_blocks_[0]);

        for (auto bb : func_->basic_blocks_) {
            if (!visited.count(bb))
                dfs(bb);
        }

        std::reverse(blocksOrder.begin(), blocksOrder.end());
    }

    for (auto bb : blocksOrder) {
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            if (auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                preds[succ].push_back(bb);
            }
        }
    }

    return blocksOrder;
}

std::map<BasicBlock*, int> Arm64RegAlloc::computeLoopDepth(
    const std::vector<BasicBlock*> &blocksOrder,
    std::map<BasicBlock*, std::vector<BasicBlock*>> &preds) const {
    // ---- Compute dominators (iterative algorithm) ----
    BasicBlock *entry = func_->basic_blocks_[0];
    std::map<BasicBlock*, std::set<BasicBlock*>> doms;
    for (auto bb : blocksOrder) {
        if (bb == entry)
            doms[bb] = {entry};
        else {
            for (auto b : blocksOrder)
                doms[bb].insert(b);
        }
    }

    bool domChanged;
    do {
        domChanged = false;
        for (auto bb : blocksOrder) {
            if (bb == entry) continue;
            std::set<BasicBlock*> inter;
            bool firstPred = true;
            for (auto pred : preds[bb]) {
                if (firstPred) {
                    inter = doms[pred];
                    firstPred = false;
                } else {
                    std::set<BasicBlock*> temp;
                    for (auto b : inter)
                        if (doms[pred].count(b))
                            temp.insert(b);
                    inter = std::move(temp);
                }
            }
            inter.insert(bb);
            if (inter != doms[bb]) {
                doms[bb] = std::move(inter);
                domChanged = true;
            }
        }
    } while (domChanged);

    // ---- Loop depth based on dominators ----
    std::map<BasicBlock*, int> loopDepth;
    for (auto bb : blocksOrder) loopDepth[bb] = 0;

    for (auto bb : blocksOrder) {
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i));
            if (!succ) continue;
            if (doms[bb].count(succ)) {
                std::set<BasicBlock*> loopBlocks;
                std::function<void(BasicBlock*)> collect = [&](BasicBlock *b) {
                    if (!loopBlocks.insert(b).second) return;
                    if (b == succ) return;
                    for (auto pred : preds[b])
                        collect(pred);
                };
                collect(bb);
                loopBlocks.insert(succ);
                for (auto b : loopBlocks) loopDepth[b]++;
            }
        }
    }

    return loopDepth;
}

std::map<Value*, double> Arm64RegAlloc::computeSpillCost(
    const std::vector<Interval> &intervals,
    std::map<BasicBlock*, int> &loopDepth,
    const std::map<Value*, std::set<Value*>> &phiAffinity) const {
    // spill 代价 = def 处 store + 每次 use 处 load，均按 20^(循环深度) 加权。
    // 参数已在调用方栈上，无 store 代价，整体减半；下限 1.0。
    std::map<Value*, double> spillCost;
    for (auto &iv : intervals) {
        double cost = 0;
        // def-site store cost
        if (auto *defInst = dynamic_cast<Instruction*>(iv.value)) {
            auto dit = loopDepth.find(defInst->parent_);
            int depth = (dit != loopDepth.end()) ? dit->second : 0;
            cost += std::pow(20.0, depth);
        }
        // use-site load costs
        for (auto &use : iv.value->use_list_) {
            auto *inst = dynamic_cast<Instruction*>(use.val_);
            if (!inst) continue;
            auto dit = loopDepth.find(inst->parent_);
            int depth = (dit != loopDepth.end()) ? dit->second : 0;
            cost += std::pow(20.0, depth);
        }
        if (dynamic_cast<Argument*>(iv.value))
            cost /= 2.0;
        if (cost < 1.0) cost = 1.0;
        spillCost[iv.value] = cost;
    }

    // Propagate phi affinity costs: ensure phi partners have similar spill cost
    // so the whole group gets registers or spills together.
    for (auto &[v, partners] : phiAffinity) {
        double maxCost = spillCost[v];
        for (auto *partner : partners)
            if (spillCost.count(partner))
                maxCost = std::max(maxCost, spillCost[partner]);
        spillCost[v] = maxCost;
        for (auto *partner : partners)
            if (spillCost.count(partner))
                spillCost[partner] = maxCost;
    }

    return spillCost;
}

void Arm64RegAlloc::computeInstructionNumbers(
    const std::vector<BasicBlock*> &blocksOrder,
    std::map<Value*, int> &defPos,
    std::map<Value*, int> &lastUse,
    std::map<BasicBlock*, int> &blockStart,
    std::map<BasicBlock*, int> &blockEnd) const {
    int idx = 0;
    for (auto arg : func_->arguments_) {
        if (canAssignRegister(arg) && !hasAssignedReg(arg)) {
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
                bool skipForSelect = false;
                if ((inst->op_id_ == Instruction::ICmp || inst->op_id_ == Instruction::FCmp) &&
                    inst->use_list_.size() == 1) {
                    auto *user = dynamic_cast<SelectInst*>((*inst->use_list_.begin()).val_);
                    if (user) skipForSelect = true;
                }
                if (!skipForSelect &&
                    inst->op_id_ == Instruction::Select &&
                    inst->use_list_.size() == 1 &&
                    (isInt(inst->type_) || isPtr(inst->type_) || isFloat(inst->type_))) {
                    auto *user = dynamic_cast<ReturnInst*>((*inst->use_list_.begin()).val_);
                    if (user) skipForSelect = true;
                }
                if (!skipForSelect) {
                    defPos[inst] = idx;
                    lastUse[inst] = idx;
                }
            }

            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val))
                    lastUse[val] = std::max(lastUse[val], idx);
                // Select emits its own cmp/fcmp using the compare operands, so
                // extend those operands' live ranges to this Select's position.
                if (inst->op_id_ == Instruction::Select) {
                    if (auto *icmp = dynamic_cast<ICmpInst*>(val)) {
                        for (unsigned j = 0; j < icmp->num_ops_; ++j) {
                            auto icmpOp = icmp->get_operand(j);
                            if (canAssignRegister(icmpOp))
                                lastUse[icmpOp] = std::max(lastUse[icmpOp], idx);
                        }
                    } else if (auto *fcmp = dynamic_cast<FCmpInst*>(val)) {
                        for (unsigned j = 0; j < fcmp->num_ops_; ++j) {
                            auto fcmpOp = fcmp->get_operand(j);
                            if (canAssignRegister(fcmpOp))
                                lastUse[fcmpOp] = std::max(lastUse[fcmpOp], idx);
                        }
                    }
                }
            }
        }
        blockEnd[bb] = idx;
    }
}

void Arm64RegAlloc::computeLiveness(
    const std::vector<BasicBlock*> &blocksOrder,
    std::map<BasicBlock*, std::set<Value*>> &liveIn,
    std::map<BasicBlock*, std::set<Value*>> &liveOut,
    std::set<Value*> &crossesCallValues) const {
    struct BBInfo { std::set<Value*> def, use; };

    std::map<BasicBlock*, std::set<Value*>> phiOut;
    for (auto bb : blocksOrder) {
        for (auto inst : bb->instr_list_) {
            auto *phi = dynamic_cast<PhiInst*>(inst);
            if (!phi) continue;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto val = phi->get_operand(i);
                auto *pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                if (canAssignRegister(val))
                    phiOut[pred].insert(val);
            }
        }
    }

    std::map<BasicBlock*, BBInfo> bbInfo;
    for (auto bb : blocksOrder) {
        BBInfo info;
        for (auto inst : bb->instr_list_) {
            if (auto *phi = dynamic_cast<PhiInst*>(inst)) {
                if (canAssignRegister(phi)) info.def.insert(phi);
                continue;
            }
            if (canAssignRegister(inst)) info.def.insert(inst);
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val) && !info.def.count(val))
                    info.use.insert(val);
            }
        }
        auto it = bbInfo.find(bb);
        if (it != bbInfo.end()) {
            for (auto v : it->second.use) info.use.insert(v);
        }
        bbInfo[bb] = info;
    }

    bool changed;
    do {
        changed = false;
        for (auto bb : blocksOrder) {
            std::set<Value*> newIn, newOut;
            auto term = bb->get_terminator();
            if (term) {
                for (unsigned i = 0; i < term->num_ops_; ++i) {
                    if (auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                        for (auto v : liveIn[succ]) newOut.insert(v);
                    }
                }
            }
            for (auto v : phiOut[bb]) newOut.insert(v);

            auto &info = bbInfo[bb];
            for (auto v : info.use) newIn.insert(v);
            for (auto v : newOut) {
                if (!info.def.count(v)) newIn.insert(v);
            }

            if (newIn != liveIn[bb] || newOut != liveOut[bb]) changed = true;
            liveIn[bb] = std::move(newIn);
            liveOut[bb] = std::move(newOut);
        }
    } while (changed);

    for (auto bb : blocksOrder) {
        std::set<Value*> live = liveOut[bb];
        for (auto it = bb->instr_list_.rbegin(); it != bb->instr_list_.rend(); ++it) {
            Instruction *inst = *it;

            if (inst->is_call()) {
                std::set<Value*> liveAfterCall = live;
                if (canAssignRegister(inst))
                    liveAfterCall.erase(inst);
                for (auto v : liveAfterCall)
                    if (canAssignRegister(v))
                        crossesCallValues.insert(v);
            }

            if (canAssignRegister(inst))
                live.erase(inst);

            if (auto *phi = dynamic_cast<PhiInst*>(inst)) {
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    auto val = phi->get_operand(i);
                    if (canAssignRegister(val))
                        live.insert(val);
                }
                continue;
            }

            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val))
                    live.insert(val);
            }
        }
    }
}

std::vector<Arm64RegAlloc::Interval> Arm64RegAlloc::buildIntervals(
    const std::vector<BasicBlock*> &blocksOrder,
    const std::map<Value*, int> &defPos,
    const std::map<Value*, int> &lastUse,
    const std::map<BasicBlock*, int> &blockEnd,
    const std::map<BasicBlock*, std::set<Value*>> &liveOut,
    const std::set<Value*> &crossesCallValues) const {
    std::vector<Interval> intervals;
    for (auto &entry : defPos) {
        Value *v = entry.first;
        int start = entry.second;
        int end = lastUse.at(v);

        for (auto bb : blocksOrder) {
            auto it = liveOut.find(bb);
            if (it != liveOut.end() && it->second.count(v)) {
                auto eit = blockEnd.find(bb);
                if (eit != blockEnd.end())
                    end = std::max(end, eit->second);
            }
        }

        if (start == 0 && end == 0 && dynamic_cast<Argument*>(v))
            continue;

        if (end >= start) {
            bool crossesCall = crossesCallValues.count(v) > 0;
            intervals.push_back({v, start, end,
                                 isAllocatableFloatValue(v->type_),
                                 isAllocatablePtrValue(v->type_),
                                 isAllocatableNEONValue(v->type_),
                                 crossesCall});
        }
    }
    return intervals;
}

std::map<Value*, std::set<Value*>> Arm64RegAlloc::buildPhiAffinity(
    const std::vector<BasicBlock*> &blocksOrder) const {
    std::map<Value*, std::set<Value*>> phiAffinity;
    for (auto bb : blocksOrder) {
        for (auto inst : bb->instr_list_) {
            auto *phi = dynamic_cast<PhiInst*>(inst);
            if (!phi || !canAssignRegister(phi)) continue;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto val = phi->get_operand(i);
                if (canAssignRegister(val)) {
                    phiAffinity[phi].insert(val);
                    phiAffinity[val].insert(phi);
                }
            }
        }
    }
    return phiAffinity;
}

void Arm64RegAlloc::promoteLoopConstants(
    const std::vector<BasicBlock*> &blocksOrder,
    const std::map<BasicBlock*, int> &loopDepth,
    bool isLeaf) {
    auto needsMovk = [](int v) {
        return (((uint32_t)v >> 16) & 0xFFFF) != 0;
    };
    std::map<int, double> weight;
    auto addCandidate = [&](int v, double w) {
        if (needsMovk(v)) weight[v] += w;
    };
    for (auto bb : blocksOrder) {
        auto dit = loopDepth.find(bb);
        int depth = (dit != loopDepth.end()) ? dit->second : 0;
        if (depth <= 0) continue;
        double w = std::pow(20.0, depth);
        for (auto inst : bb->instr_list_) {
            if (inst->op_id_ == Instruction::SRem) {
                auto *ci = dynamic_cast<ConstantInt*>(inst->get_operand(1));
                Magic::SignedDivisorInfo info = ci
                    ? Magic::analyzeDivisor(ci->value_)
                    : Magic::SignedDivisorInfo{};
                if (info.usesMagic()) {
                    addCandidate(Magic::getMagic(info.magnitude).multiplier, w);
                    addCandidate(static_cast<int>(info.magnitude), w);
                }
            } else if (inst->op_id_ == Instruction::SDiv) {
                auto *ci = dynamic_cast<ConstantInt*>(inst->get_operand(1));
                Magic::SignedDivisorInfo info = ci
                    ? Magic::analyzeDivisor(ci->value_)
                    : Magic::SignedDivisorInfo{};
                if (info.usesMagic())
                    addCandidate(Magic::getMagic(ci->value_).multiplier, w);
            } else if (inst->op_id_ == Instruction::Add ||
                       inst->op_id_ == Instruction::Sub ||
                       inst->op_id_ == Instruction::ICmp) {
                for (unsigned i = 0; i < inst->num_ops_; ++i)
                    if (auto *ci = dynamic_cast<ConstantInt*>(inst->get_operand(i)))
                        addCandidate(ci->value_, w);
            }
        }
    }
    if (weight.empty()) return;

    std::set<int> usedRegs;
    for (auto &kv : assignedRegs_) {
        const std::string &r = kv.second;
        if (!r.empty() && (r[0] == 'w' || r[0] == 'x'))
            usedRegs.insert(std::stoi(r.substr(1)));
    }
    std::vector<int> freeRegs;
    if (isLeaf) {
        for (int r = 28; r >= 19; --r)  // 从高号开始，远离着色常用的低号区
            if (!usedRegs.count(r)) freeRegs.push_back(r);
    } else {
        // 非叶函数优先用 caller-saved 临时寄存器（call 后按需重物化，
        // 无需保存/恢复）。caller-saved 不足以覆盖循环里的全部不变大常量
        // 时，再借用空闲的 callee-saved：它们入口物化一次、不被 call 冲掉，
        // 对"嵌套在含调用外层循环里、但自身无调用的内层循环常量"尤其划算。
        // 保存/恢复由 mergePromotedConstRegs 统一接管。
        for (int r = 10; r <= 15; ++r)
            if (!usedRegs.count(r)) freeRegs.push_back(r);
        for (int r = 28; r >= 19; --r)
            if (!usedRegs.count(r)) freeRegs.push_back(r);
    }
    std::vector<std::pair<double, int>> ranked;  // (-权重, 常量值)，排序确定
    for (auto &kv : weight) ranked.push_back({-kv.second, kv.first});
    std::sort(ranked.begin(), ranked.end());
    size_t n = std::min({freeRegs.size(), ranked.size(), (size_t)8});
    for (size_t i = 0; i < n; ++i)
        promotedConsts_[ranked[i].second] = "w" + std::to_string(freeRegs[i]);
}

void Arm64RegAlloc::allocate() {
    std::map<Value*, int> defPos;
    std::map<Value*, int> lastUse;
    std::vector<Interval> intervals;

    // ---- 1. RPO block order & predecessor map ----
    std::map<BasicBlock*, std::vector<BasicBlock*>> preds;
    std::vector<BasicBlock*> blocksOrder = computeBlockOrder(preds);

    // ---- 2. Per-block instruction index ranges (filled during dataflow below) ----
    std::map<BasicBlock*, int> blockStart, blockEnd;

    // ---- 0. Leaf-function argument pre-coloring ----
    bool isLeaf = true;
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_call()) { isLeaf = false; break; }
        }
        if (!isLeaf) break;
    }
    if (isLeaf) {
        int intArgIdx = 0, floatArgIdx = 0;
        for (auto arg : func_->arguments_) {
            if (!canAssignRegister(arg)) continue;
            if (isAllocatableFloatValue(arg->type_)) {
                if (floatArgIdx < 8)
                    assignedRegs_[arg] = "s" + std::to_string(floatArgIdx++);
            } else {
                if (intArgIdx < 8) {
                    bool isPtr = isAllocatablePtrValue(arg->type_);
                    assignedRegs_[arg] = (isPtr ? "x" : "w") + std::to_string(intArgIdx++);
                }
            }
        }
    }

    // Build color→physical-register mapping.
    // Values that live through calls must stay in callee-saved registers.
    // Short-lived values should prefer caller-saved registers so they do not
    // force save/restore slots in leaf functions or for call-local temporaries.
    std::vector<int> intColorToReg;
    std::vector<int> floatColorToReg;
    std::vector<int> neonColorToReg;
    std::set<int> callerSavedIntRegs;
    std::set<int> callerSavedFloatRegs;
    std::set<int> callerSavedNEONRegs;
    {
        std::set<int> precoloredIntRegs;
        for (auto &kv : assignedRegs_) {
            const std::string &reg = kv.second;
            if (!reg.empty() && (reg[0] == 'w' || reg[0] == 'x'))
                precoloredIntRegs.insert(std::stoi(reg.substr(1)));
        }
        if (isLeaf) {
            const int callerIntRegs[] = {0,1,2,3,4,5,6,7, 9};
            for (int r : callerIntRegs) {
                if (!precoloredIntRegs.count(r))
                    intColorToReg.push_back(r);
                callerSavedIntRegs.insert(r);
            }
            for (int r = 19; r <= 28; ++r) {
                if (!precoloredIntRegs.count(r))
                    intColorToReg.push_back(r);
            }
        } else {
            const int callerIntRegs[] = {9,0,1,2,3,4,5,6,7};
            for (int r : callerIntRegs) {
                if (!precoloredIntRegs.count(r))
                    intColorToReg.push_back(r);
                callerSavedIntRegs.insert(r);
            }
            callerSavedIntRegs.insert(9);
            for (int r = 19; r <= 28; ++r) {
                if (!precoloredIntRegs.count(r))
                    intColorToReg.push_back(r);
            }
        }
        std::set<int> precoloredFloatRegs;
        for (auto &kv : assignedRegs_) {
            const std::string &reg = kv.second;
            if (!reg.empty() && reg[0] == 's')
                precoloredFloatRegs.insert(std::stoi(reg.substr(1)));
        }
        if (isLeaf) {
            const int callerFloatRegs[] = {0,1,2,3,4,5,6,7, 16};
            for (int r : callerFloatRegs) {
                if (!precoloredFloatRegs.count(r))
                    floatColorToReg.push_back(r);
                callerSavedFloatRegs.insert(r);
            }
            for (int r = 8; r <= 15; ++r) {
                if (!precoloredFloatRegs.count(r))
                    floatColorToReg.push_back(r);
            }
        } else {
            for (int r = 0; r <= 7; ++r) {
                if (!precoloredFloatRegs.count(r))
                    floatColorToReg.push_back(r);
                callerSavedFloatRegs.insert(r);
            }
            if (!precoloredFloatRegs.count(16))
                floatColorToReg.push_back(16);
            callerSavedFloatRegs.insert(16);
            for (int r = 8; r <= 15; ++r) {
                if (!precoloredFloatRegs.count(r))
                    floatColorToReg.push_back(r);
            }
        }
        for (int r = 8; r <= 15; ++r)
            neonColorToReg.push_back(r);
    }

    computeInstructionNumbers(blocksOrder, defPos, lastUse, blockStart, blockEnd);

    // ---- 3. Data-flow analysis: LiveIn / LiveOut ----
    std::map<BasicBlock*, std::set<Value*>> liveIn, liveOut;
    std::set<Value*> crossesCallValues;
    computeLiveness(blocksOrder, liveIn, liveOut, crossesCallValues);

    // ---- 4. Build live intervals ----
    intervals = buildIntervals(blocksOrder, defPos, lastUse, blockEnd, liveOut, crossesCallValues);

    // ---- 5/6. Dominators & loop depth ----
    std::map<BasicBlock*, int> loopDepth = computeLoopDepth(blocksOrder, preds);

    // ---- 7. Phi coalesce affinity ----
    std::map<Value*, std::set<Value*>> phiAffinity = buildPhiAffinity(blocksOrder);

    // ---- 8. Spill cost ----
    std::map<Value*, double> spillCost =
        computeSpillCost(intervals, loopDepth, phiAffinity);

    // ---- 9. Separate into register-class pools ----
    std::vector<Interval> intPool, floatPool, neonPool;
    for (auto &iv : intervals) {
        if (iv.isNEON)
            neonPool.push_back(iv);
        else if (iv.isFloat)
            floatPool.push_back(iv);
        else
            intPool.push_back(iv);
    }

    // ---- 10. Precise SSA interference oracle（仅用于 affinity 合并判定）----
    // liveAtDefs(v)：v 定义写入后仍活跃的值集合。两值真干涉 ⇔ 一方在另一
    // 方定义点处活跃（SSA 性质）。phi 的物理定义点是各前驱末尾的并行拷贝：
    // 任何在某前驱出口活跃的值都与 phi 冲突，唯一豁免是 phi 自己来自该
    // 前驱的 incoming（并行拷贝读先于写）；同块其余 phi 视为同时定义，
    // 相互冲突（规避 swap 问题）。
    auto liveAtDefsCache = std::make_shared<std::map<Value*, std::set<Value*>>>();
    auto liveAtDefs = [this, liveAtDefsCache, &liveIn, &liveOut, &preds]
                      (Value *v) -> const std::set<Value*>& {
        auto it = liveAtDefsCache->find(v);
        if (it != liveAtDefsCache->end()) return it->second;

        std::set<Value*> live;
        if (auto *phi = dynamic_cast<PhiInst*>(v)) {
            BasicBlock *bb = phi->parent_;
            live = liveIn[bb];
            for (auto *inst : bb->instr_list_) {
                auto *p2 = dynamic_cast<PhiInst*>(inst);
                if (p2 && p2 != phi && canAssignRegister(p2)) live.insert(p2);
            }
            for (auto *pred : preds[bb]) {
                Value *incoming = nullptr;
                for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
                    if (phi->get_operand(i + 1) == pred) {
                        incoming = phi->get_operand(i);
                        break;
                    }
                }
                for (auto *x : liveOut[pred])
                    if (x != incoming) live.insert(x);
            }
        } else if (auto *inst = dynamic_cast<Instruction*>(v)) {
            BasicBlock *bb = inst->parent_;
            live = liveOut[bb];
            for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend(); ++rit) {
                Instruction *cur = *rit;
                if (cur == inst) break;  // live == inst 定义后的活跃集合
                if (canAssignRegister(cur)) live.erase(cur);
                if (dynamic_cast<PhiInst*>(cur)) continue;  // phi 的读发生在前驱
                for (unsigned i = 0; i < cur->num_ops_; ++i) {
                    auto *op = cur->get_operand(i);
                    if (canAssignRegister(op)) live.insert(op);
                }
            }
        } else {
            // Argument：在入口块起点定义，与其余参数及入口 liveIn 同时活跃
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
    std::function<bool(Value*, Value*)> trulyInterferes =
        [liveAtDefs](Value *a, Value *b) -> bool {
            return liveAtDefs(a).count(b) > 0 || liveAtDefs(b).count(a) > 0;
        };

    // ---- 11. Graph coloring (Chaitin-Briggs) ----
    colorPool(intPool, intColorToReg, false, callerSavedIntRegs, spillCost, phiAffinity, trulyInterferes);
    colorPool(floatPool, floatColorToReg, true, callerSavedFloatRegs, spillCost, phiAffinity, trulyInterferes);
    colorPool(neonPool, neonColorToReg, false, callerSavedNEONRegs, spillCost, phiAffinity, trulyInterferes);

    // ---- 12. 循环内多指令常量提升 ----
    // srem/sdiv 常数除法的魔数乘数/除数、以及 Add/Sub/ICmp 的大常量操作数
    // 在循环里每次迭代都要 movz+movk 重物化。把权重最高的几个 32 位常量
    // 提升到整个函数都未被占用的寄存器（与已有着色零干涉），入口物化一次。
    // 叶函数使用 callee-saved，非叶函数优先使用 caller-saved；后端会在 call
    // 后重新物化 caller-saved promoted constants，避免额外保存/恢复 x19-x28。
    // 按常量值而非 Value* 记录，魔数这类 ISel 派生常量也能命中。
    promoteLoopConstants(blocksOrder, loopDepth, isLeaf);
}
