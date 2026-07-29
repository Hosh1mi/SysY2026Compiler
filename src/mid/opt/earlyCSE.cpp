#include "../../include/mid/opt/earlyCSE.hpp"
#include "../../include/mid/analysis/analysisManager.hpp"
#include "../../include/mid/opt/cse_common.hpp"
#include <cstdlib>
#include <iostream>
#include <set>
#include <stack>

// ---------- Helpers ----------
static Value* getBase(Value *ptr) {
    while (true) {
        if (auto *gep = dynamic_cast<GetElementPtrInst*>(ptr)) {
            ptr = gep->get_operand(0);
            continue;
        }
        if (auto *bc = dynamic_cast<Bitcast*>(ptr)) {
            ptr = bc->get_operand(0);
            continue;
        }
        break;
    }
    return ptr;
}

static bool isUnknownBase(Value *base) {
    if (dynamic_cast<AllocaInst*>(base))   return false;
    if (dynamic_cast<GlobalVariable*>(base)) return false;
    return true;
}

// ---------- Per-BB memory entry ----------
struct MemDef {
    Instruction *store_inst;
    Value       *stored_val;
    bool         has_been_read;
};

struct LoopMemoryMod {
    Instruction *inst = nullptr;
};

struct ActiveMemoryMod {
    Instruction *inst = nullptr;
    Value *token = nullptr;
};

// Sentinel for a loop-carried memory definition.  It can participate in load
// versioning but never carries a forwardable stored value.
static int loop_sentinel_storage;
static Value *const LOOP_SENTINEL = reinterpret_cast<Value*>(&loop_sentinel_storage);

static bool isEarlyCSEAADebugEnabled() {
    static bool enabled = std::getenv("DEBUG_EARLY_CSE_AA") != nullptr;
    return enabled;
}

static std::string formatEarlyCSEValue(Value *val) {
    if (!val) return "<null>";
    if (auto *inst = dynamic_cast<Instruction *>(val))
        return "%" + inst->name_;
    if (auto *global = dynamic_cast<GlobalVariable *>(val))
        return "@" + global->name_;
    if (!val->name_.empty())
        return val->name_;
    return "<anon>";
}

static bool mayModPtr(const BasicAliasAnalysis &BAA, Instruction *modInst,
                      Value *ptr) {
    if (!modInst) return true;
    return isModSet(BAA.getModRefInfo(modInst, ptr));
}

static Value *latestModFor(Value *ptr,
                           const std::vector<ActiveMemoryMod> &activeMods,
                           const BasicAliasAnalysis &BAA) {
    for (auto it = activeMods.rbegin(); it != activeMods.rend(); ++it) {
        if (mayModPtr(BAA, it->inst, ptr))
            return it->token;
    }
    return nullptr;
}

static Value *storedValueFromLatestMod(
    Value *ptr, const std::vector<ActiveMemoryMod> &activeMods,
    const BasicAliasAnalysis &BAA, BasicBlock *requiredStoreBlock) {
    for (auto it = activeMods.rbegin(); it != activeMods.rend(); ++it) {
        if (!mayModPtr(BAA, it->inst, ptr))
            continue;
        if (it->token == LOOP_SENTINEL)
            return nullptr;
        auto *store = dynamic_cast<StoreInst *>(it->inst);
        if (!store || store->parent_ != requiredStoreBlock ||
            store->get_operand(1) != ptr)
            return nullptr;
        return store->get_operand(0);
    }
    return nullptr;
}

// ---------- Natural-loop analysis ----------
// Builds map: loop-header → memory operations inside that loop.
// Header sentinels prevent incorrectly reusing a load across a loop that may
// modify the queried address.
static std::map<BasicBlock*, std::vector<LoopMemoryMod>>
analyze_loops(Function *func) {
    std::map<BasicBlock*, std::vector<LoopMemoryMod>> loop_mods;
    auto &dom_info = func->getDominatorInfo();

    for (auto *bb : func->basic_blocks_) {
        for (auto *succ : bb->succ_bbs_) {
            if (!dom_info.dominates(succ, bb)) continue; // bb→succ, succ dom bb = back-edge

            BasicBlock *header = succ;
            BasicBlock *back_src = bb;

            // Collect natural-loop blocks
            std::set<BasicBlock*> loop_blocks;
            std::stack<BasicBlock*> work;
            work.push(back_src);
            loop_blocks.insert(back_src);
            while (!work.empty()) {
                BasicBlock *cur = work.top(); work.pop();
                for (auto *pred : cur->pre_bbs_) {
                    if (loop_blocks.count(pred)) continue;
                    if (!dom_info.dominates(header, pred)) continue;
                    loop_blocks.insert(pred);
                    work.push(pred);
                }
            }
            loop_blocks.insert(header);

            // Scan for stores and calls
            for (auto *lb : loop_blocks) {
                for (auto *inst : lb->instr_list_) {
                    if (inst->is_store())
                        loop_mods[header].push_back({inst});
                    if (inst->is_call())
                        loop_mods[header].push_back({inst});
                }
            }
        }
    }
    return loop_mods;
}

// ---------- Core DFS ----------
static const std::unordered_map<Value*, Value*> empty_vn_map;

static void early_cse_dfs(BasicBlock *bb,
                          std::unordered_map<ExprSignature, Value*> &expr_map,
                          const std::map<BasicBlock*, std::vector<BasicBlock*>> &dom_children,
                          const std::map<BasicBlock*, std::vector<LoopMemoryMod>> &loop_mods,
                          std::vector<ActiveMemoryMod> &activeMods,
                          const BasicAliasAnalysis &BAA)
{
    // ── Join point: clear cached loads to prevent sibling→sibling CSE ──
    // A load defined in one sibling does not dominate another sibling.
    // Without this, a load in sibling B could incorrectly replace a load in
    // sibling A when their defining stores happen to match.
    if (bb->pre_bbs_.size() > 1) {
        for (auto it = expr_map.begin(); it != expr_map.end(); ) {
            if (it->first.op_id == Instruction::Load)
                it = expr_map.erase(it);
            else
                ++it;
        }
    }

    size_t savedModSize = activeMods.size();

    // ── Loop-header: invalidate defining stores for addresses modified in loop ──
    auto ls_it = loop_mods.find(bb);
    if (ls_it != loop_mods.end()) {
        for (auto &mod : ls_it->second) {
            activeMods.push_back({mod.inst, LOOP_SENTINEL});
        }
    }

    // ── Per-BB state ──
    std::unordered_map<Value*, MemDef> local_mem;  // exact ptr → MemDef
    std::vector<ExprSignature> added_sigs;
    std::vector<std::pair<ExprSignature, Value*>> removed_sigs;
    std::vector<Instruction*> to_delete;

    for (auto it = bb->instr_list_.begin(); it != bb->instr_list_.end(); ) {
        Instruction *inst = *it;
        ++it;

        // ── Store ──
        if (inst->is_store()) {
            Value *val  = inst->get_operand(0);
            Value *ptr  = inst->get_operand(1);
            Value *base = getBase(ptr);

            // Dead-store detection (same exact ptr, not read)
            auto mem_it = local_mem.find(ptr);
            if (mem_it != local_mem.end() && !mem_it->second.has_been_read)
                to_delete.push_back(mem_it->second.store_inst);

            if (isUnknownBase(base))
                local_mem.clear();

            local_mem[ptr] = {inst, val, /*has_been_read=*/false};

            activeMods.push_back({inst, inst});

            // Store may alias some cached loads — clear only affected Load entries.
            for (auto si = expr_map.begin(); si != expr_map.end(); ) {
                if (si->first.op_id == Instruction::Load &&
                    mayModPtr(BAA, inst, si->first.ops[0])) {
                    if (isEarlyCSEAADebugEnabled()) {
                        std::cerr << "[EarlyCSE:AA] invalidate load_ptr="
                                  << formatEarlyCSEValue(si->first.ops[0])
                                  << " by=" << formatEarlyCSEValue(inst)
                                  << "\n";
                    }
                    removed_sigs.push_back({si->first, si->second});
                    si = expr_map.erase(si);
                } else {
                    if (isEarlyCSEAADebugEnabled() &&
                        si->first.op_id == Instruction::Load) {
                        std::cerr << "[EarlyCSE:AA] preserve load_ptr="
                                  << formatEarlyCSEValue(si->first.ops[0])
                                  << " across=" << formatEarlyCSEValue(inst)
                                  << "\n";
                    }
                    ++si;
                }
            }
            continue;
        }

        // ── Load ──
        if (inst->is_load()) {
            Value *ptr  = inst->get_operand(0);

            // Store-load forwarding (within BB).
            // The load uses the SSA value directly, not through memory,
            // so do NOT mark has_been_read — the store may become dead.
            auto mem_it = local_mem.find(ptr);
            if (mem_it != local_mem.end()) {
                inst->replace_all_use_with(mem_it->second.stored_val);
                to_delete.push_back(inst);
                continue;
            }

            // Forward across one CFG edge only.  Requiring the defining store
            // in the load block's direct predecessor avoids skipping writes
            // from sibling paths that reconverge at an intermediate join.
            // A loop sentinel, may-alias store, or writable call also blocks
            // the forwarding conservatively.
            if (bb->pre_bbs_.size() <= 1) {
                BasicBlock *pred = bb->pre_bbs_.empty() ? nullptr : bb->pre_bbs_.front();
                if (Value *stored =
                        storedValueFromLatestMod(ptr, activeMods, BAA, pred)) {
                    inst->replace_all_use_with(stored);
                    to_delete.push_back(inst);
                    continue;
                }
            }

            Value *def_store = latestModFor(ptr, activeMods, BAA);

            // Build signature: {Load, ty, 0, [canonical(ptr), defining_store]}
            ExprSignature sig;
            sig.op_id    = Instruction::Load;
            sig.ty       = inst->type_;
            sig.extra_op = 0;
            sig.ops      = {get_canonical_constant(ptr), def_store};

            auto exist = expr_map.find(sig);
            if (exist != expr_map.end()) {
                if (isEarlyCSEAADebugEnabled()) {
                    std::cerr << "[EarlyCSE:AA] cse load="
                              << formatEarlyCSEValue(inst)
                              << " ptr=" << formatEarlyCSEValue(ptr)
                              << " version=" << formatEarlyCSEValue(def_store)
                              << "\n";
                }
                inst->replace_all_use_with(exist->second);
                to_delete.push_back(inst);
            } else {
                expr_map[sig] = inst;
                added_sigs.push_back(sig);
            }
            continue;
        }

        // ── Call ──
        if (inst->is_call()) {
            // Pure call CSE: result depends only on args, no side effects
            {
                auto *call = static_cast<CallInst*>(inst);
                auto *callee = dynamic_cast<Function*>(
                    call->get_operand(call->num_ops_ - 1));
                if (callee && BAA.isPure(callee)) {
                    ExprSignature sig = compute_signature(inst, empty_vn_map);
                    auto exist = expr_map.find(sig);
                    if (exist != expr_map.end()) {
                        inst->replace_all_use_with(exist->second);
                        to_delete.push_back(inst);
                    } else {
                        expr_map[sig] = inst;
                        added_sigs.push_back(sig);
                    }
                    continue;
                }
            }
            for (auto mi = local_mem.begin(); mi != local_mem.end();) {
                ModRefInfo mr = BAA.getModRefInfo(inst, mi->first);
                if (isModSet(mr)) {
                    mi = local_mem.erase(mi);
                    continue;
                }
                if (isRefSet(mr))
                    mi->second.has_been_read = true;
                ++mi;
            }
            activeMods.push_back({inst, inst});
            for (auto si = expr_map.begin(); si != expr_map.end(); ) {
                if (si->first.op_id == Instruction::Load &&
                    mayModPtr(BAA, inst, si->first.ops[0])) {
                    if (isEarlyCSEAADebugEnabled()) {
                        std::cerr << "[EarlyCSE:AA] invalidate load_ptr="
                                  << formatEarlyCSEValue(si->first.ops[0])
                                  << " by=" << formatEarlyCSEValue(inst)
                                  << "\n";
                    }
                    removed_sigs.push_back({si->first, si->second});
                    si = expr_map.erase(si);
                } else ++si;
            }
            continue;
        }

        // ── Expression CSE (non-memory) ──
        if (is_safe_to_eliminate(inst)) {
            ExprSignature sig = compute_signature(inst, empty_vn_map);
            auto exist = expr_map.find(sig);
            if (exist != expr_map.end()) {
                inst->replace_all_use_with(exist->second);
                to_delete.push_back(inst);
            } else {
                expr_map[sig] = inst;
                added_sigs.push_back(sig);
            }
        }
    }

    for (auto *inst : to_delete)
        bb->delete_instr(inst);

    // ── Recurse into dominator children ──
    auto itc = dom_children.find(bb);
    if (itc != dom_children.end())
        for (auto *child : itc->second)
            early_cse_dfs(child, expr_map, dom_children, loop_mods,
                          activeMods, BAA);

    // ── Restore scope ──
    for (auto &sig : added_sigs)
        expr_map.erase(sig);
    for (auto &[sig, val] : removed_sigs) {
        auto *inst_val = dynamic_cast<Instruction*>(val);
        if (!inst_val || inst_val->parent_ != bb)
            expr_map[sig] = val;
    }
    activeMods.resize(savedModSize);
}

// ---------- Entry point ----------
static void early_cse_on_function(Function *func, const BasicAliasAnalysis &BAA) {
    if (func->basic_blocks_.empty()) return;

    auto &dom_children = func->getDominatorInfo().domChildren;
    BasicBlock *entry = func->basic_blocks_.front();
    auto loop_mods = analyze_loops(func);

    std::unordered_map<ExprSignature, Value*> expr_map;
    std::vector<ActiveMemoryMod> activeMods;

    early_cse_dfs(entry, expr_map, dom_children, loop_mods, activeMods, BAA);
}

void EarlyCSE::execute(Module *module) {
    AnalysisManager AM;
    execute(module, AM);
}

PreservedAnalyses EarlyCSE::execute(Module *module, AnalysisManager &AM) {
    BasicAliasAnalysis &BAA = AM.getBasicAA(module);
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            early_cse_on_function(func, BAA);
    }
    for (auto &p : canonical_constants) delete p.second;
    canonical_constants.clear();
    return PreservedAnalyses::none();
}
