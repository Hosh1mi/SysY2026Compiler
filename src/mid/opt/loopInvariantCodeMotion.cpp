#include "../../include/mid/opt/loopInvariantCodeMotion.hpp"
#include <algorithm>
#include <functional>
#include <queue>

void LICM::execute(Module *module) {
    for (auto func : module->function_list_) {
        if (!func->is_declaration())
            runOnFunction(func);
    }
}

// ── CFG traversal ──────────────────────────────────────────────────────────

std::vector<BasicBlock*> LICM::computeRPO(Function *func) {
    std::vector<BasicBlock*> postorder;
    std::set<BasicBlock*> visited;

    std::function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ)) dfs(succ);
        postorder.push_back(bb);
    };

    if (!func->basic_blocks_.empty())
        dfs(func->basic_blocks_[0]);

    std::reverse(postorder.begin(), postorder.end());
    return postorder;
}

void LICM::computeDominators(const std::vector<BasicBlock*>& rpo) {
    idom_.clear();
    rpoIdx_.clear();
    if (rpo.empty()) return;

    BasicBlock *entry = rpo[0];
    for (int i = 0; i < (int)rpo.size(); i++)
        rpoIdx_[rpo[i]] = i;

    idom_[entry] = entry;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : rpo) {
            if (bb == entry) continue;
            BasicBlock *new_idom = nullptr;
            for (auto pred : bb->pre_bbs_) {
                if (!idom_.count(pred)) continue;
                new_idom = new_idom ? intersect(pred, new_idom) : pred;
            }
            if (new_idom && idom_[bb] != new_idom) {
                idom_[bb] = new_idom;
                changed = true;
            }
        }
    }
}

BasicBlock *LICM::intersect(BasicBlock *a, BasicBlock *b) {
    while (a != b) {
        while (rpoIdx_[a] > rpoIdx_[b]) a = idom_[a];
        while (rpoIdx_[b] > rpoIdx_[a]) b = idom_[b];
    }
    return a;
}

bool LICM::dominates(BasicBlock *a, BasicBlock *b) {
    while (b != idom_[b]) {   // walk up until entry (idom[entry]==entry)
        if (b == a) return true;
        b = idom_[b];
    }
    return b == a;            // b is now entry
}

// ── Loop detection ─────────────────────────────────────────────────────────

std::vector<LICM::Loop> LICM::findLoops(Function *func) {
    std::vector<Loop> loops;

    for (auto bb : func->basic_blocks_) {
        for (auto succ : bb->succ_bbs_) {
            // back edge: bb -> succ  and  succ dominates bb
            if (!idom_.count(succ)) continue;
            if (!dominates(succ, bb)) continue;

            Loop loop;
            loop.header = succ;
            loop.latch  = bb;

            // collect loop body by reverse-traversal from latch to header
            loop.blocks.insert(succ);
            std::queue<BasicBlock*> wl;
            wl.push(bb);
            while (!wl.empty()) {
                auto cur = wl.front(); wl.pop();
                if (!loop.blocks.insert(cur).second) continue;
                for (auto pred : cur->pre_bbs_)
                    if (!loop.blocks.count(pred)) wl.push(pred);
            }
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

// Return the unique predecessor of the loop header that is outside the loop,
// or nullptr if there are zero or multiple such predecessors.
BasicBlock *LICM::getPreheader(const Loop &loop) {
    BasicBlock *pre = nullptr;
    for (auto pred : loop.header->pre_bbs_) {
        if (loop.blocks.count(pred)) continue; // inside loop
        if (pre) return nullptr;               // multiple outside preds
        pre = pred;
    }
    return pre; // nullptr if no outside pred (entry is header)
}

// ── Invariance / safety checks ─────────────────────────────────────────────

bool LICM::isInvariant(Value *val, const std::set<BasicBlock*>& loopBlocks,
                        const std::set<Instruction*>& toHoist, const Loop */*loop*/) {
    if (dynamic_cast<Constant*>(val))       return true;
    if (dynamic_cast<GlobalVariable*>(val)) return true;
    if (dynamic_cast<Argument*>(val))       return true;

    auto inst = dynamic_cast<Instruction*>(val);
    if (!inst) return true;
    if (toHoist.count(inst)) return true;        // will be hoisted
    return !loopBlocks.count(inst->parent_);     // defined outside loop
}

// Walk GEP/bitcast chains to the ultimate base Value.
static Value *getBase(Value *ptr) {
    while (true) {
        if (auto gep = dynamic_cast<GetElementPtrInst*>(ptr)) { ptr = gep->get_operand(0); continue; }
        if (auto bc  = dynamic_cast<Bitcast*>(ptr))           { ptr = bc->get_operand(0);  continue; }
        break;
    }
    return ptr;
}

bool LICM::hasStoreToSameBase(const Loop &loop, Value *base) {
    for (auto bb : loop.blocks)
        for (auto inst : bb->instr_list_)
            if (inst->is_store() && getBase(inst->get_operand(1)) == base)
                return true;
    return false;
}

bool LICM::isSafeToHoist(Instruction *inst, const Loop &loop,
                          const std::set<Instruction*>& toHoist, bool loopHasCalls) {
    if (inst->is_phi() || inst->is_br() || inst->is_ret() ||
        inst->is_store() || inst->is_alloca() || inst->is_call())
        return false;

    if (inst->is_load()) {
        // Only hoist loads when no calls exist in the loop (calls may modify globals/args)
        if (loopHasCalls) return false;
        Value *base = getBase(inst->get_operand(0));
        // Only hoist if no store writes to the same base inside the loop.
        // Covers globals, arguments, and allocas (e.g. loop-local counters that are
        // invariant within an inner loop).
        return !hasStoreToSameBase(loop, base);
    }

    // BinaryInst, UnaryInst, GEP, ICmp, FCmp, ZExt, etc. are pure
    return true;
}

// ── Hoisting ──────────────────────────────────────────────────────────────

bool LICM::runOnLoop(const Loop &loop) {
    BasicBlock *preheader = getPreheader(loop);
    if (!preheader) return false;

    // NOTE: eliminateTrivialHeaderPhis was disabled due to miscompilation on h-10
    // (incorrect phi elimination when a loop has multiple predecessors or when
    // non_latch_val resolves to a phi in a different loop context).

    bool changed = false;

    // Precompute once: if any call exists in the loop, loads cannot be hoisted safely
    bool loopHasCalls = false;
    for (auto bb : loop.blocks)
        for (auto inst : bb->instr_list_)
            if (inst->is_call()) { loopHasCalls = true; goto done_call_scan; }
    done_call_scan:;

    // Iterate until no more candidates (handles chains: GEP → Load)
    bool progress = true;
    while (progress) {
        progress = false;

        // Collect candidates in program order across all loop blocks
        std::set<Instruction*> toHoist;
        for (auto bb : loop.blocks) {
            for (auto inst : bb->instr_list_) {
                if (!isSafeToHoist(inst, loop, toHoist, loopHasCalls)) continue;

                bool allInvariant = true;
                for (unsigned i = 0; i < inst->num_ops_; i++) {
                    if (!isInvariant(inst->get_operand(i), loop.blocks, toHoist, &loop)) {
                        allInvariant = false;
                        break;
                    }
                }
                if (allInvariant) toHoist.insert(inst);
            }
        }

        if (toHoist.empty()) break;

        // Move candidates to preheader in a single ordered pass
        for (auto bb : loop.blocks) {
            auto it = bb->instr_list_.begin();
            while (it != bb->instr_list_.end()) {
                Instruction *inst = *it;
                if (!toHoist.count(inst)) { ++it; continue; }

                it = bb->instr_list_.end(); // will be re-assigned
                bb->remove_instr(inst);
                inst->parent_ = preheader;
                preheader->add_instruction_before_terminator(inst);

                // restart iteration for this bb from the beginning
                it = bb->instr_list_.begin();
                progress = true;
                changed  = true;
            }
        }
    }

    return changed;
}

// Remove single-predecessor block phis: %v = phi [x, pred] → replace %v with x everywhere.
// These are semantically equivalent to direct uses and block the invariance chain.
void LICM::eliminateSinglePredPhis(Function *func) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : func->basic_blocks_) {
            if (bb->pre_bbs_.size() != 1) continue;
            auto it = bb->instr_list_.begin();
            while (it != bb->instr_list_.end()) {
                auto phi = dynamic_cast<PhiInst*>(*it);
                if (!phi) break;
                Value *incoming = phi->get_operand(0);
                phi->replace_all_use_with(incoming);
                bb->delete_instr(phi);
                changed = true;
                it = bb->instr_list_.begin();
            }
        }
    }
}

// Remove trivial self-loop header phis: %v = phi [x, preheader], [%v, latch]
// → replace %v with x everywhere.  The loop never changes the value.
void LICM::eliminateTrivialHeaderPhis(const Loop &loop) {
    bool changed = true;
    while (changed) {
        changed = false;
        auto it = loop.header->instr_list_.begin();
        while (it != loop.header->instr_list_.end()) {
            auto phi = dynamic_cast<PhiInst*>(*it);
            if (!phi) break;

            Value *non_latch_val = nullptr;
            bool self_loop = true;   // until proven otherwise
            bool valid = true;

            for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                Value *inc_bb  = phi->get_operand(i + 1);
                Value *inc_val = phi->get_operand(i);
                if (inc_bb == loop.latch) {
                    if (inc_val != phi) self_loop = false;
                } else {
                    if (!non_latch_val) non_latch_val = inc_val;
                    else if (non_latch_val != inc_val) valid = false;
                }
            }

            if (valid && self_loop && non_latch_val) {
                // Safety check: only eliminate if non_latch_val is defined OUTSIDE the loop.
                // If non_latch_val is itself inside the loop (e.g. another loop's phi),
                // elimination would create dangling references when instructions are hoisted.
                auto nv_inst = dynamic_cast<Instruction*>(non_latch_val);
                if (nv_inst && loop.blocks.count(nv_inst->parent_)) {
                    // non_latch_val is inside the loop — skip this phi to avoid miscompilation
                    ++it;
                    continue;
                }
                phi->replace_all_use_with(non_latch_val);
                loop.header->delete_instr(phi);
                changed = true;
                it = loop.header->instr_list_.begin();
                continue;
            }
            ++it;
        }
    }
}

void LICM::runOnFunction(Function *func) {
    if (func->basic_blocks_.empty()) return;

    // Simplify pass-through phis before loop analysis so LICM sees cleaner values.
    eliminateSinglePredPhis(func);

    auto rpo = computeRPO(func);
    computeDominators(rpo);
    auto loops = findLoops(func);

    // Process smaller (inner) loops first
    std::sort(loops.begin(), loops.end(), [](const Loop &a, const Loop &b) {
        return a.blocks.size() < b.blocks.size();
    });

    for (auto &loop : loops)
        runOnLoop(loop);
}
