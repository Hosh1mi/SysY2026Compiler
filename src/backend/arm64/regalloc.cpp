#include "../../include/backend/arm64/regalloc.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

Value *transparentAddressRoot(Value *value) {
    Value *current = value;
    while (auto *bitcast = dynamic_cast<Bitcast *>(current)) {
        Value *source = bitcast->get_operand(0);
        if (!bitcast->type_ || !source->type_ ||
            bitcast->type_->tid_ != Type::PointerTyID ||
            source->type_->tid_ != Type::PointerTyID)
            break;
        current = source;
    }
    return current;
}

} // namespace

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

size_t Arm64RegAlloc::stableIndex(Value *v) const {
    auto it = stableOrder_.find(v);
    // Prefer later definitions when graph decisions otherwise tie.  The
    // simplify stack is LIFO, so this leaves earlier values to be selected
    // first while keeping the choice independent of allocation addresses.
    return it == stableOrder_.end() ? stableOrder_.size()
                                    : stableOrder_.size() - 1 - it->second;
}

int Arm64RegAlloc::preferredArgumentReg(Value *v) const {
    if (!preferArgumentRegs_ || !dynamic_cast<Argument*>(v)) return -1;

    int intArg = 0;
    int floatArg = 0;
    for (auto *arg : func_->arguments_) {
        if (isAllocatableFloatValue(arg->type_)) {
            int reg = floatArg++;
            if (arg == v) return reg < 8 ? reg : -1;
        } else if (isAllocatableIntValue(arg->type_) ||
                   isAllocatablePtrValue(arg->type_)) {
            int reg = intArg++;
            if (arg == v) return reg < 8 ? reg : -1;
        } else if (arg == v) {
            return -1;
        }
    }
    return -1;
}

void Arm64RegAlloc::colorPool(const std::vector<Interval> &pool,
                               const std::vector<int> &colorToReg, bool isFloat,
                               const std::set<int> &callerSavedRegs,
                               const std::map<Value*, double> &spillCost,
                               const std::map<Value*, std::set<Value*>> &affinity,
                               const std::function<bool(Value*, Value*)> &trulyInterferes) {
    if (pool.empty()) return;
    int K = (int)colorToReg.size();

    std::vector<Interval> sorted = pool;
    std::sort(sorted.begin(), sorted.end(),
              [this](const Interval &a, const Interval &b) {
                  if (a.start != b.start) return a.start < b.start;
                  return stableIndex(a.value) < stableIndex(b.value);
              });
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

    // ---- Affinity 合并（Briggs 保守准则 + George 准则）----
    // 把亲和值且不真正冲突的活跃区间合并成一个节点，消除 phi copy 或
    // destructive-operand copy。
    // 冲突判定先用凸包重叠（adj 有边）做快速过滤，凸包重叠时再调用
    // trulyInterferes 做精确的活跃性判定（循环 phi 与 backedge incoming
    // 虽凸包重叠但真实区间通常不冲突，精确判定能合并它们）。
    // 合并守则：Briggs 准则（合并节点高度数邻居 < K）；Briggs 不满足时
    // 尝试 George 准则（一方的高度数邻居均已是另一方的邻居，约束不恶化）。
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
        auto it = affinity.find(iv.value);
        if (it == affinity.end()) continue;
        std::vector<Value*> partners(it->second.begin(), it->second.end());
        std::sort(partners.begin(), partners.end(),
                  [this](Value *a, Value *b) {
                      return stableIndex(a) < stableIndex(b);
                  });
        for (auto *p : partners) {
            if (stableIndex(iv.value) < stableIndex(p) && intervalForValue.count(p))
                affEdges.push_back({iv.value, p});
        }
    }

    std::map<Value*, std::vector<Value*>> members;
    for (auto &iv : sorted) members[iv.value] = {iv.value};

    constexpr size_t kCoalescingBlockLimit = 80;
    bool mergedAny = func_->basic_blocks_.size() <= kCoalescingBlockLimit;
    while (mergedAny) {
        mergedAny = false;
        for (auto &e : affEdges) {
            Value *a = ufFind(e.first);
            Value *b = ufFind(e.second);
            if (a == b) continue;
            if (adj[a].count(b)) {
                bool conflict = false;
                for (auto *x : members[a]) {
                    for (auto *y : members[b]) {
                        if (trulyInterferes(x, y)) {
                            conflict = true;
                            break;
                        }
                    }
                    if (conflict) break;
                }
                if (conflict) continue;
            }

            // Briggs 准则：合并后节点的高度数（≥K）邻居个数 < K
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
            if (highDegree >= K) {
                // George 准则（Briggs 不满足时的 fallback）：
                // a 的所有高度数邻居均已是 b 的邻居（或反之），则合并后
                // 高度数邻居集合不扩大，着色约束不恶化，可以安全合并。
                bool georgeAB = true;
                for (auto *n : adj[a]) {
                    if (n == b) continue;
                    if ((int)adj[n].size() >= K && !adj[b].count(n))
                        { georgeAB = false; break; }
                }
                if (!georgeAB) {
                    bool georgeBA = true;
                    for (auto *n : adj[b]) {
                        if (n == a) continue;
                        if ((int)adj[n].size() >= K && !adj[a].count(n))
                            { georgeBA = false; break; }
                    }
                    if (!georgeBA) continue;
                }
            }

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

    std::vector<Value*> stack;
    std::set<Value*> potentialSpills;

    // Simplify phase
    std::vector<Value*> nodes;
    nodes.reserve(adj.size());
    std::set<Value*> addedNodes;
    for (auto &iv : sorted) {
        Value *root = ufFind(iv.value);
        if (adj.count(root) && addedNodes.insert(root).second)
            nodes.push_back(root);
    }
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
            if (!inWL[i] || deg[i] >= K) continue;
            if (pick < 0 || deg[i] < deg[pick]) pick = i;
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
    auto preferredColorForNode = [&](Value *node) {
        auto memberIt = members.find(node);
        if (memberIt == members.end()) return -1;
        for (auto *member : memberIt->second) {
            int preferredReg = preferredArgumentReg(member);
            if (preferredReg < 0) continue;
            auto preferred = std::find(colorToReg.begin(), colorToReg.end(), preferredReg);
            if (preferred != colorToReg.end())
                return static_cast<int>(preferred - colorToReg.begin());
        }
        return -1;
    };
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
        // ABI argument registers are preferences, not permanent colors.  This
        // removes entry copies when the color is genuinely available while
        // allowing the register to be reused after the argument dies.
        int preferredColor = preferredColorForNode(v);
        if (preferredColor >= 0 && colorAllowed(preferredColor) &&
            !neighborColors.count(preferredColor))
            color = preferredColor;
        // Biased: prefer the color of already-colored affinity partners.
        // （没能合并的 affinity 对仍可通过同色偏好消除拷贝）
        auto affIt = affinity.find(v);
        if (color < 0 && affIt != affinity.end()) {
            std::vector<Value*> partners(affIt->second.begin(), affIt->second.end());
            std::sort(partners.begin(), partners.end(),
                      [this](Value *a, Value *b) {
                          return stableIndex(a) < stableIndex(b);
                      });
            for (auto partner : partners) {
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
            // Do not consume an uncolored interfering argument's ABI color if
            // another color is available.  Non-neighbors are intentionally
            // ignored, so the physical register becomes reusable at liveness
            // boundaries instead of being reserved for the whole function.
            std::set<int> pendingArgumentColors;
            for (auto *neighbor : adj[v]) {
                if (colors.count(neighbor)) continue;
                int pending = preferredColorForNode(neighbor);
                if (pending >= 0) pendingArgumentColors.insert(pending);
            }
            for (int c = 0; c < K; c++) {
                if (colorAllowed(c) && !neighborColors.count(c) &&
                    !pendingArgumentColors.count(c)) {
                    color = c;
                    break;
                }
            }
            for (int c = 0; color < 0 && c < K; c++) {
                if (colorAllowed(c) && !neighborColors.count(c)) {
                    color = c;
                    break;
                }
            }
        }
        if (color >= 0) {
            colors[v] = color;
            potentialSpills.erase(v);
        }
    }

    // ---- 写回分配结果 ----
    // 每个原始值取其 UF 代表元的颜色，寄存器名按值自身类型（w/x/s/v）决定。
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

        std::reverse(blocksOrder.begin(), blocksOrder.end());

        // Append unreachable blocks after the RPO so blocksOrder[0] is always
        // the entry; CHK idom seeds idom[0]=0 assuming this invariant.
        for (auto bb : func_->basic_blocks_) {
            if (!visited.count(bb))
                blocksOrder.push_back(bb);
        }
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
    // ---- Cooper-Harvey-Kennedy（CHK）即时支配者算法 ----
    // 用 RPO 编号 + idom 链 intersect 迭代，O(n) 复杂度，替代 O(n²) 全集算法。
    // RPO 编号：blocksOrder[0] 是入口，编号最小。
    int N = (int)blocksOrder.size();
    std::map<BasicBlock*, int> rpoNum;
    for (int i = 0; i < N; ++i) rpoNum[blocksOrder[i]] = i;

    // idom[i]：blocksOrder[i] 的直接支配者下标；入口自支配（idom[0] = 0）。
    std::vector<int> idom(N, -1);
    idom[0] = 0;

    // intersect：沿两条 idom 链向上爬，利用 RPO 大小关系收敛到公共祖先。
    auto intersect = [&](int a, int b) {
        while (a != b) {
            while (a > b) a = idom[a];
            while (b > a) b = idom[b];
        }
        return a;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 1; i < N; ++i) {
            BasicBlock *bb = blocksOrder[i];
            int newIdom = -1;
            for (auto pred : preds[bb]) {
                auto it = rpoNum.find(pred);
                if (it == rpoNum.end()) continue;
                int pi = it->second;
                if (idom[pi] == -1) continue;  // 前驱尚未处理，跳过
                newIdom = (newIdom == -1) ? pi : intersect(pi, newIdom);
            }
            if (newIdom != -1 && newIdom != idom[i]) {
                idom[i] = newIdom;
                changed = true;
            }
        }
    }

    // dominates(succIdx, bbIdx)：沿 idom 链上爬，判断 succ 是否支配 bb。
    auto dominates = [&](int succIdx, int bbIdx) {
        int cur = bbIdx;
        while (cur != succIdx) {
            if (idom[cur] < 0) return false;   // 不可达块，idom 未初始化
            if (idom[cur] == cur) return false; // 已到达入口，未找到 succ
            cur = idom[cur];
        }
        return true;
    };

    // ---- 回边驱动的自然循环深度计算 ----
    std::map<BasicBlock*, int> loopDepth;
    for (auto bb : blocksOrder) loopDepth[bb] = 0;

    for (int i = 0; i < N; ++i) {
        BasicBlock *bb = blocksOrder[i];
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned k = 0; k < term->num_ops_; ++k) {
            auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(k));
            if (!succ) continue;
            auto sit = rpoNum.find(succ);
            if (sit == rpoNum.end()) continue;
            int si = sit->second;
            if (!dominates(si, i)) continue;  // 不是回边

            // 收集自然循环：从回边尾(i)出发，沿前驱反向到达 succ(si)
            std::set<int> loopSet;
            std::vector<int> worklist = {i};
            loopSet.insert(i);
            loopSet.insert(si);
            while (!worklist.empty()) {
                int cur = worklist.back(); worklist.pop_back();
                for (auto pred : preds[blocksOrder[cur]]) {
                    auto pit = rpoNum.find(pred);
                    if (pit == rpoNum.end()) continue;
                    int pi = pit->second;
                    if (!loopSet.count(pi)) {
                        loopSet.insert(pi);
                        worklist.push_back(pi);
                    }
                }
            }
            for (int idx : loopSet) loopDepth[blocksOrder[idx]]++;
        }
    }

    return loopDepth;
}

std::map<Value*, double> Arm64RegAlloc::computeSpillCost(
    const std::vector<Interval> &intervals,
    const std::map<BasicBlock*, int> &loopDepth,
    const std::map<Value*, std::set<Value*>> &affinity) const {
    // spill 代价 = def 处 store + 每次 use 处 load，均按 20^(循环深度) 加权。
    // 参数已在调用方栈上无 store 代价，整体减半；下限 1.0。
    std::map<Value*, double> spillCost;
    for (auto &iv : intervals) {
        double cost = 0;
        // 定义点 store 代价：对称地将写回操作纳入 spill 估算（Chaitin 原始
        // 公式仅统计 use-site；加入 def-site 后高频写变量更倾向留在寄存器）
        if (auto *defInst = dynamic_cast<Instruction*>(iv.value)) {
            auto dit = loopDepth.find(defInst->parent_);
            int depth = (dit != loopDepth.end()) ? dit->second : 0;
            cost += std::pow(20.0, depth);
        }
        // 使用点 load 代价
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

    // Preserve the allocator's one-pass affinity propagation, but visit the
    // values in canonical IR order instead of pointer-map order.
    std::vector<Value*> affinityOrder;
    affinityOrder.reserve(intervals.size());
    for (auto &iv : intervals) affinityOrder.push_back(iv.value);
    std::sort(affinityOrder.begin(), affinityOrder.end(),
              [this](Value *a, Value *b) {
                  return stableIndex(a) < stableIndex(b);
              });
    for (auto *v : affinityOrder) {
        double maxCost = spillCost[v];
        auto it = affinity.find(v);
        if (it != affinity.end())
            for (auto *partner : it->second) {
                auto costIt = spillCost.find(partner);
                if (costIt != spillCost.end())
                    maxCost = std::max(maxCost, costIt->second);
            }
        spillCost[v] = maxCost;
        if (it != affinity.end())
            for (auto *partner : it->second)
                if (spillCost.count(partner))
                    spillCost[partner] = maxCost;
    }

    return spillCost;
}

void Arm64RegAlloc::computeInstructionNumbers(
    const std::vector<BasicBlock*> &blocksOrder,
    std::map<Value*, int> &defPos,
    std::map<Value*, int> &lastUse,
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
            blockEnd[bb] = idx;
            continue;
        }

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
                Value *addressRoot = transparentAddressRoot(val);
                if (addressRoot != val && canAssignRegister(addressRoot))
                    lastUse[addressRoot] = std::max(lastUse[addressRoot], idx);
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
                Value *addressRoot = transparentAddressRoot(val);
                if (addressRoot != val && canAssignRegister(addressRoot))
                    phiOut[pred].insert(addressRoot);
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
                Value *addressRoot = transparentAddressRoot(val);
                if (addressRoot != val && canAssignRegister(addressRoot) &&
                    !info.def.count(addressRoot))
                    info.use.insert(addressRoot);
            }
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
                    Value *addressRoot = transparentAddressRoot(val);
                    if (addressRoot != val && canAssignRegister(addressRoot))
                        live.insert(addressRoot);
                }
                continue;
            }

            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val))
                    live.insert(val);
                Value *addressRoot = transparentAddressRoot(val);
                if (addressRoot != val && canAssignRegister(addressRoot))
                    live.insert(addressRoot);
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

std::map<Value*, std::set<Value*>> Arm64RegAlloc::buildRegisterAffinity(
    const std::vector<BasicBlock*> &blocksOrder) const {
    std::map<Value*, std::set<Value*>> affinity;
    auto addAffinity = [&](Value *a, Value *b) {
        if (!canAssignRegister(a) || !canAssignRegister(b) ||
            a->type_ != b->type_)
            return;
        affinity[a].insert(b);
        affinity[b].insert(a);
    };
    for (auto bb : blocksOrder) {
        for (auto inst : bb->instr_list_) {
            auto *phi = dynamic_cast<PhiInst*>(inst);
            if (phi && canAssignRegister(phi)) {
                for (unsigned i = 0; i < phi->num_ops_; i += 2)
                    addAffinity(phi, phi->get_operand(i));
                continue;
            }

            // AArch64 MLA/MLS destructively updates its accumulator.  Give a
            // vector add/sub fed by a single-use multiply an affinity to that
            // accumulator, so instruction selection does not need register
            // copies around the fused operation.
            auto *root = dynamic_cast<BinaryInst *>(inst);
            if (!root || !dynamic_cast<VectorType *>(root->type_)) continue;
            const bool isAdd = root->is_add() || root->is_fadd();
            const bool isSub = root->is_sub() || root->is_fsub();
            if (!isAdd && !isSub) continue;

            auto matchMul = [&](Value *value) -> BinaryInst * {
                auto *mul = dynamic_cast<BinaryInst *>(value);
                if (!mul || mul->type_ != root->type_ ||
                    mul->use_list_.size() != 1)
                    return nullptr;
                if (root->is_fadd() || root->is_fsub())
                    return mul->is_fmul() ? mul : nullptr;
                return mul->is_mul() ? mul : nullptr;
            };

            if (matchMul(root->get_operand(1))) {
                addAffinity(root, root->get_operand(0));
            } else if (isAdd && matchMul(root->get_operand(0))) {
                addAffinity(root, root->get_operand(1));
            }
        }
    }
    return affinity;
}

Arm64RegAlloc::RegPalette Arm64RegAlloc::buildRegPalette(bool isLeaf) const {
    RegPalette pal;
    if (isLeaf) {
        const int callerInt[] = {0,1,2,3,4,5,6,7,9};
        for (int r : callerInt) {
            pal.intColorToReg.push_back(r);
            pal.callerSavedInt.insert(r);
        }
        for (int r = 19; r <= 28; ++r)
            pal.intColorToReg.push_back(r);
        const int callerFloat[] = {0,1,2,3,4,5,6,7,16};
        for (int r : callerFloat) {
            pal.floatColorToReg.push_back(r);
            pal.callerSavedFloat.insert(r);
        }
        for (int r = 8; r <= 15; ++r)
            pal.floatColorToReg.push_back(r);
    } else {
        const int callerInt[] = {9,0,1,2,3,4,5,6,7};
        for (int r : callerInt) {
            pal.intColorToReg.push_back(r);
            pal.callerSavedInt.insert(r);
        }
        for (int r = 19; r <= 28; ++r)
            pal.intColorToReg.push_back(r);
        for (int r = 0; r <= 7; ++r) {
            pal.floatColorToReg.push_back(r);
            pal.callerSavedFloat.insert(r);
        }
        pal.floatColorToReg.push_back(16);
        pal.callerSavedFloat.insert(16);
        for (int r = 8; r <= 15; ++r)
            pal.floatColorToReg.push_back(r);
    }
    // NEON palette: v8-v15 are all callee-saved on AArch64, so callerSavedNEON stays empty.
    for (int r = 8; r <= 15; ++r) pal.neonColorToReg.push_back(r);
    return pal;
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
                    addCandidate(Magic::getMagic(info.magnitude).multiplier, w);
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
    int maxGepIndices = 0;
    for (auto *bb : blocksOrder) {
        for (auto *inst : bb->instr_list_) {
            if (inst->op_id_ == Instruction::GetElementPtr)
                maxGepIndices = std::max(
                    maxGepIndices, static_cast<int>(inst->num_ops_) - 1);
        }
    }
    if (isLeaf) {
        for (int r = 28; r >= 19; --r)  // 从高号开始，远离着色常用的低号区
            if (!usedRegs.count(r)) freeRegs.push_back(r);
    } else {
        // Multi-dimensional GEP lowering needs one or more simultaneous
        // address temporaries.  Reserve scratch capacity in proportion to the
        // largest address expression instead of letting promoted constants
        // force the selector onto its aliasing fallback register.
        // x16/x17 cover ordinary address formation.  A six-or-more-index GEP
        // can require the whole x10-x17 bank while accumulating scaled terms.
        int scratchReserve = maxGepIndices >= 6 ? 6 : 0;
        for (int r = 10; r <= 15 - scratchReserve; ++r)
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
    stableOrder_.clear();
    size_t stableId = 0;
    for (auto *arg : func_->arguments_)
        stableOrder_[arg] = stableId++;
    for (auto *bb : func_->basic_blocks_)
        for (auto *inst : bb->instr_list_)
            stableOrder_[inst] = stableId++;

    // ---- 1. Leaf detection ----
    bool isLeaf = true;
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_call()) { isLeaf = false; break; }
        }
        if (!isLeaf) break;
    }
    preferArgumentRegs_ = isLeaf;

    // ---- 2. RPO block order & predecessor map ----
    std::map<BasicBlock*, std::vector<BasicBlock*>> preds;
    std::vector<BasicBlock*> blocksOrder = computeBlockOrder(preds);

    // ---- 3. Register palette (color→physical-reg + caller-saved sets) ----
    RegPalette pal = buildRegPalette(isLeaf);

    // Instruction numbers (defPos / lastUse / block ranges)
    std::map<Value*, int> defPos, lastUse;
    std::map<BasicBlock*, int> blockEnd;
    computeInstructionNumbers(blocksOrder, defPos, lastUse, blockEnd);

    // ---- 4. Liveness analysis: LiveIn / LiveOut ----
    std::map<BasicBlock*, std::set<Value*>> liveIn, liveOut;
    std::set<Value*> crossesCallValues;
    computeLiveness(blocksOrder, liveIn, liveOut, crossesCallValues);

    // ---- 5. Build live intervals ----
    std::vector<Interval> intervals =
        buildIntervals(blocksOrder, defPos, lastUse, blockEnd, liveOut, crossesCallValues);

    // ---- 6. Loop depth ----
    std::map<BasicBlock*, int> loopDepth = computeLoopDepth(blocksOrder, preds);

    // ---- 7. Register affinity ----
    std::map<Value*, std::set<Value*>> affinity =
        buildRegisterAffinity(blocksOrder);

    // ---- 8. Spill cost ----
    std::map<Value*, double> spillCost =
        computeSpillCost(intervals, loopDepth, affinity);

    // ---- 9. Separate into register-class pools ----
    std::vector<Interval> intPool, floatPool, neonPool;
    for (auto &iv : intervals) {
        if (iv.isNEON) neonPool.push_back(iv);
        else if (iv.isFloat) floatPool.push_back(iv);
        else intPool.push_back(iv);
    }

    // ---- 10. SSA interference oracle (used only by affinity coalescing) ----
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
                    if (x != incoming &&
                        transparentAddressRoot(x) !=
                            transparentAddressRoot(incoming))
                        live.insert(x);
            }
        } else if (auto *inst = dynamic_cast<Instruction*>(v)) {
            BasicBlock *bb = inst->parent_;
            live = liveOut[bb];
            for (auto rit = bb->instr_list_.rbegin(); rit != bb->instr_list_.rend(); ++rit) {
                Instruction *cur = *rit;
                if (cur == inst) break;
                if (canAssignRegister(cur)) live.erase(cur);
                if (dynamic_cast<PhiInst*>(cur)) continue;  // phi 的读发生在前驱
                for (unsigned i = 0; i < cur->num_ops_; ++i) {
                    auto *op = cur->get_operand(i);
                    if (canAssignRegister(op)) live.insert(op);
                    Value *root = transparentAddressRoot(op);
                    if (root != op && canAssignRegister(root))
                        live.insert(root);
                }
            }
        } else {
            // Argument：与其余参数及入口 liveIn 同时活跃
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
    colorPool(intPool,   pal.intColorToReg,   false, pal.callerSavedInt,   spillCost, affinity, trulyInterferes);
    colorPool(floatPool, pal.floatColorToReg, true,  pal.callerSavedFloat, spillCost, affinity, trulyInterferes);
    colorPool(neonPool,  pal.neonColorToReg,  false, pal.callerSavedNEON,  spillCost, affinity, trulyInterferes);

    // ---- 12. Loop constant promotion ----
    // srem/sdiv 魔数乘数、Add/Sub/ICmp 大常量在循环里每次迭代重物化。
    // 把高权重常量提升到空闲寄存器，入口物化一次。
    promoteLoopConstants(blocksOrder, loopDepth, isLeaf);
}
