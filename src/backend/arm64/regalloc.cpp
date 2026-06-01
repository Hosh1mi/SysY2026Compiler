#include "../../include/backend/arm64/regalloc.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <vector>

Arm64RegAlloc::Arm64RegAlloc(Function *f) : func_(f) {}

const std::map<Value*, std::string> &Arm64RegAlloc::assignedRegs() const {
    return assignedRegs_;
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
    if (auto inst = dynamic_cast<Instruction*>(v)) {
        if (inst->is_void() || inst->is_alloca()) {
            return false;
        }
    }
    return isAllocatableIntValue(v->type_) || isAllocatableFloatValue(v->type_) || isAllocatablePtrValue(v->type_);
}

void Arm64RegAlloc::colorPool(const std::vector<Interval> &pool,
                               const std::vector<int> &colorToReg, bool isFloat,
                               const std::map<Value*, double> &spillCost,
                               const std::map<Value*, std::set<Value*>> &phiAffinity) {
    if (pool.empty()) return;
    int K = (int)colorToReg.size();

    std::vector<Interval> sorted = pool;
    std::sort(sorted.begin(), sorted.end(),
              [](const Interval &a, const Interval &b) { return a.start < b.start; });

    // Build interference graph
    std::map<Value*, std::set<Value*>> adj;
    for (auto &iv : sorted) adj[iv.value];
    for (size_t i = 0; i < sorted.size(); i++) {
        for (size_t j = i + 1; j < sorted.size() && sorted[j].start < sorted[i].end; j++) {
            adj[sorted[i].value].insert(sorted[j].value);
            adj[sorted[j].value].insert(sorted[i].value);
        }
    }

    std::set<Value*> worklist;
    for (auto &iv : sorted) worklist.insert(iv.value);
    std::vector<Value*> stack;
    std::set<Value*> potentialSpills;

    // Simplify phase
    while (!worklist.empty()) {
        bool found = false;
        for (auto it = worklist.begin(); it != worklist.end(); ++it) {
            Value *v = *it;
            int degree = 0;
            for (auto n : adj[v])
                if (worklist.count(n)) degree++;
            if (degree < K) {
                stack.push_back(v);
                worklist.erase(it);
                found = true;
                break;
            }
        }
        if (!found) {
            double bestCost = 1e100;
            Value *best = nullptr;
            for (auto v : worklist) {
                int degree = 0;
                for (auto n : adj[v])
                    if (worklist.count(n)) degree++;
                auto sc = spillCost.find(v);
                double cost = (sc != spillCost.end() ? sc->second : 1.0) / (degree + 1);
                if (cost < bestCost) { bestCost = cost; best = v; }
            }
            stack.push_back(best);
            worklist.erase(best);
            potentialSpills.insert(best);
        }
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

        int color = -1;
        // Biased: prefer color of already-colored phi partners
        auto affIt = phiAffinity.find(v);
        if (affIt != phiAffinity.end()) {
            for (auto partner : affIt->second) {
                auto pc = colors.find(partner);
                if (pc != colors.end() && !neighborColors.count(pc->second)) {
                    color = pc->second;
                    break;
                }
            }
        }
        if (color < 0) {
            for (int c = 0; c < K; c++) {
                if (!neighborColors.count(c)) { color = c; break; }
            }
        }
        if (color >= 0) {
            colors[v] = color;
            potentialSpills.erase(v);
        }
    }

    // Record assignments
    for (auto &kv : colors) {
        int regNo = colorToReg[kv.second];
        if (isFloat) {
            assignedRegs_[kv.first] = "s" + std::to_string(regNo);
        } else if (isAllocatablePtrValue(kv.first->type_)) {
            assignedRegs_[kv.first] = "x" + std::to_string(regNo);
        } else {
            assignedRegs_[kv.first] = "w" + std::to_string(regNo);
        }
    }
}

void Arm64RegAlloc::allocate() {
    std::map<Value*, int> defPos;
    std::map<Value*, int> lastUse;
    std::vector<Interval> intervals;

    // ---- 1. RPO block order & predecessor map ----
    std::map<BasicBlock*, std::vector<BasicBlock*>> preds;
    std::vector<BasicBlock*> blocksOrder;

    {
        std::set<BasicBlock*> visited;
        std::function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
            visited.insert(bb);
            auto term = bb->get_terminator();
            if (term) {
                for (unsigned i = 0; i < term->num_ops_; ++i) {
                    if (auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
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
            if (auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                preds[succ].push_back(bb);
            }
        }
    }

    // ---- 2. Instruction numbering ----
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

    // Build color→physical-register mapping
    std::vector<int> intColorToReg;
    std::vector<int> floatColorToReg;
    {
        std::set<int> precoloredIntRegs;
        for (auto &kv : assignedRegs_) {
            const std::string &reg = kv.second;
            if (!reg.empty() && (reg[0] == 'w' || reg[0] == 'x'))
                precoloredIntRegs.insert(std::stoi(reg.substr(1)));
        }
        if (isLeaf) {
            for (int r : {0,1,2,3,4,5,6,7, 19,20,21,22,23,24,25,26,27,28}) {
                if (!precoloredIntRegs.count(r))
                    intColorToReg.push_back(r);
            }
        } else {
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
        for (int r = 8; r <= 15; ++r) {
            if (!precoloredFloatRegs.count(r))
                floatColorToReg.push_back(r);
        }
    }

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
                if (inst->op_id_ == Instruction::ICmp && inst->use_list_.size() == 1) {
                    auto *user = dynamic_cast<SelectInst*>((*inst->use_list_.begin()).val_);
                    if (user) skipForSelect = true;
                }
                if (!skipForSelect &&
                    inst->op_id_ == Instruction::Select &&
                    inst->use_list_.size() == 1 &&
                    !isFloat(inst->type_)) {
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
                if (canAssignRegister(val)) {
                    lastUse[val] = std::max(lastUse[val], idx);
                }
                // Select emits its own cmp using the ICmp's operands, so
                // extend those operands' live ranges to this Select's position.
                if (inst->op_id_ == Instruction::Select) {
                    if (auto *icmp = dynamic_cast<ICmpInst*>(val)) {
                        for (unsigned j = 0; j < icmp->num_ops_; ++j) {
                            auto icmpOp = icmp->get_operand(j);
                            if (canAssignRegister(icmpOp)) {
                                lastUse[icmpOp] = std::max(lastUse[icmpOp], idx);
                            }
                        }
                    }
                }
            }
        }
        blockEnd[bb] = idx;
    }

    // ---- 3. Data-flow analysis: LiveIn / LiveOut ----
    std::map<BasicBlock*, std::set<Value*>> phiOut;
    for (auto bb : blocksOrder) {
        for (auto inst : bb->instr_list_) {
            auto phi = dynamic_cast<PhiInst*>(inst);
            if (!phi) continue;
            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                auto val = phi->get_operand(i);
                auto pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                if (canAssignRegister(val)) {
                    phiOut[pred].insert(val);
                }
            }
        }
    }

    struct BBInfo { std::set<Value*> def, use; };
    std::map<BasicBlock*, BBInfo> bbInfo;

    for (auto bb : blocksOrder) {
        BBInfo info;
        for (auto inst : bb->instr_list_) {
            if (auto phi = dynamic_cast<PhiInst*>(inst)) {
                if (canAssignRegister(phi)) info.def.insert(phi);
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    auto val = phi->get_operand(i);
                    auto pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                    bbInfo[pred].use.insert(val);
                }
                continue;
            }

            if (canAssignRegister(inst)) info.def.insert(inst);
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val) && !info.def.count(val)) {
                    info.use.insert(val);
                }
            }
        }
        auto it = bbInfo.find(bb);
        if (it != bbInfo.end()) {
            for (auto v : it->second.use) info.use.insert(v);
        }
        bbInfo[bb] = info;
    }

    bool changed;
    std::map<BasicBlock*, std::set<Value*>> liveIn, liveOut;
    do {
        changed = false;
        for (auto bb : blocksOrder) {
            std::set<Value*> newIn, newOut;
            auto term = bb->get_terminator();
            if (term) {
                for (unsigned i = 0; i < term->num_ops_; ++i) {
                    if (auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
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

    // ---- 4. Build live intervals ----
    for (auto &entry : defPos) {
        Value *v = entry.first;
        int start = entry.second;
        int end = lastUse[v];

        for (auto bb : blocksOrder) {
            if (liveOut[bb].count(v)) {
                end = std::max(end, blockEnd[bb]);
            }
        }

        if (start == 0 && end == 0 && dynamic_cast<Argument*>(v))
            continue;

        if (end >= start) {
            intervals.push_back({v, start, end,
                                 isAllocatableFloatValue(v->type_),
                                 isAllocatablePtrValue(v->type_)});
        }
    }

    // ---- 5. Compute dominators (iterative algorithm) ----
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

    // ---- 6. Loop depth based on dominators ----
    std::map<BasicBlock*, int> loopDepth;
    for (auto bb : blocksOrder) loopDepth[bb] = 0;

    for (auto bb : blocksOrder) {
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i));
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

    // ---- 7. Phi coalesce affinity ----
    std::map<Value*, std::set<Value*>> phiAffinity;
    for (auto bb : blocksOrder) {
        for (auto inst : bb->instr_list_) {
            auto phi = dynamic_cast<PhiInst*>(inst);
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

    // ---- 8. Spill cost: sum of (10 ^ loopDepth) per use ----
    std::map<Value*, double> spillCost;
    for (auto &iv : intervals) {
        double cost = 0;
        for (auto &use : iv.value->use_list_) {
            auto inst = dynamic_cast<Instruction*>(use.val_);
            if (!inst) continue;
            int depth = loopDepth[inst->parent_];
            cost += std::pow(10.0, depth);
        }
        if (dynamic_cast<Argument*>(iv.value))
            cost /= 2.0;
        if (cost < 1.0) cost = 1.0;
        spillCost[iv.value] = cost;
    }

    // ---- 9. Separate into pools ----
    std::vector<Interval> intPool, floatPool;
    for (auto &iv : intervals) {
        if (iv.isFloat)
            floatPool.push_back(iv);
        else
            intPool.push_back(iv);
    }

    // ---- 10. Graph coloring (Chaitin-Briggs) ----
    colorPool(intPool, intColorToReg, false, spillCost, phiAffinity);
    colorPool(floatPool, floatColorToReg, true, spillCost, phiAffinity);
}
