#include "../../include/mid/opt/earlyCSE.hpp"
#include "../../include/mid/opt/cse_common.hpp"
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

// ---------- Natural-loop analysis ----------
// Builds map: loop-header → set of base pointers stored to in that loop.
// nullptr in the set means "call present — all addresses potentially modified".
static std::map<BasicBlock*, std::set<Value*>> analyze_loops(Function *func) {
    std::map<BasicBlock*, std::set<Value*>> loop_stores;
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
                        loop_stores[header].insert(getBase(inst->get_operand(1)));
                    if (inst->is_call())
                        loop_stores[header].insert(nullptr); // "all" sentinel
                }
            }
        }
    }
    return loop_stores;
}

// ---------- Sentinel for "loop-varying" defining store ----------
// Any unique non-null pointer works; we only compare by pointer equality in signatures.
static int loop_sentinel_storage;
static Value *const LOOP_SENTINEL = reinterpret_cast<Value*>(&loop_sentinel_storage);

// ---------- Core DFS ----------
static const std::unordered_map<Value*, Value*> empty_vn_map;

static void early_cse_dfs(BasicBlock *bb,
                          std::unordered_map<ExprSignature, Value*> &expr_map,
                          const std::map<BasicBlock*, std::vector<BasicBlock*>> &dom_children,
                          const std::map<BasicBlock*, std::set<Value*>> &loop_stores,
                          std::unordered_map<Value*, Value*> &last_store) // base → defining store
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

    // ── Track first-saved values for scoped restoration ──
    std::vector<std::pair<Value*, Value*>> saved_stores; // {base, old_value}
    std::set<Value*> saved_bases; // bases already saved (avoid double-save)

    auto save_store = [&](Value *base) {
        if (saved_bases.count(base)) return;
        auto it = last_store.find(base);
        saved_stores.push_back({base, it != last_store.end() ? it->second : nullptr});
        saved_bases.insert(base);
    };

    // ── Loop-header: invalidate defining stores for addresses modified in loop ──
    auto ls_it = loop_stores.find(bb);
    if (ls_it != loop_stores.end()) {
        bool all = ls_it->second.count(nullptr);
        // Invalidate existing entries
        for (auto &[base, def] : last_store) {
            if (all || ls_it->second.count(base)) {
                save_store(base);
                def = LOOP_SENTINEL;
            }
        }
        // Install sentinel for loop-modified addresses not yet in last_store
        if (!all) {
            for (Value *base : ls_it->second) {
                if (!base) continue; // nullptr = "all" sentinel
                if (last_store.count(base)) continue; // already handled above
                save_store(base);
                last_store[base] = LOOP_SENTINEL;
            }
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

            // Update defining store for this base
            save_store(base);
            last_store[base] = inst;

            // Store may alias any load — clear Load entries (scoped)
            for (auto si = expr_map.begin(); si != expr_map.end(); ) {
                if (si->first.op_id == Instruction::Load) {
                    removed_sigs.push_back({si->first, si->second});
                    si = expr_map.erase(si);
                } else ++si;
            }
            continue;
        }

        // ── Load ──
        if (inst->is_load()) {
            Value *ptr  = inst->get_operand(0);
            Value *base = getBase(ptr);

            // Store-load forwarding (within BB).
            // The load uses the SSA value directly, not through memory,
            // so do NOT mark has_been_read — the store may become dead.
            auto mem_it = local_mem.find(ptr);
            if (mem_it != local_mem.end()) {
                inst->replace_all_use_with(mem_it->second.stored_val);
                to_delete.push_back(inst);
                continue;
            }

            // Determine defining store for versioned CSE
            Value *def_store = nullptr;
            auto ds_it = last_store.find(base);
            if (ds_it != last_store.end()) {
                def_store = ds_it->second; // may be Store*, LOOP_SENTINEL, or nullptr
            } else if (ls_it != loop_stores.end()) {
                // base is in loop's modified set but not yet in last_store.
                // Install sentinel so loads inside loop won't match loads outside.
                bool all = ls_it->second.count(nullptr);
                if (all || ls_it->second.count(base)) {
                    save_store(base);
                    last_store[base] = LOOP_SENTINEL;
                    def_store = LOOP_SENTINEL;
                }
            }

            // Build signature: {Load, ty, 0, [canonical(ptr), defining_store]}
            ExprSignature sig;
            sig.op_id    = Instruction::Load;
            sig.ty       = inst->type_;
            sig.extra_op = 0;
            sig.ops      = {get_canonical_constant(ptr), def_store};

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

        // ── Call ──
        if (inst->is_call()) {
            local_mem.clear();
            for (auto si = expr_map.begin(); si != expr_map.end(); ) {
                if (si->first.op_id == Instruction::Load) {
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
            early_cse_dfs(child, expr_map, dom_children, loop_stores, last_store);

    // ── Restore scope ──
    for (auto &sig : added_sigs)
        expr_map.erase(sig);
    for (auto &[sig, val] : removed_sigs) {
        auto *inst_val = dynamic_cast<Instruction*>(val);
        if (!inst_val || inst_val->parent_ != bb)
            expr_map[sig] = val;
    }
    for (auto &[base, old_val] : saved_stores) {
        if (old_val == nullptr)
            last_store.erase(base);
        else
            last_store[base] = old_val;
    }
}

// ---------- Entry point ----------
static void early_cse_on_function(Function *func) {
    if (func->basic_blocks_.empty()) return;

    auto &dom_children = func->getDominatorInfo().domChildren;
    BasicBlock *entry = func->basic_blocks_.front();
    auto loop_stores = analyze_loops(func);

    std::unordered_map<ExprSignature, Value*> expr_map;
    std::unordered_map<Value*, Value*> last_store; // base → defining store

    early_cse_dfs(entry, expr_map, dom_children, loop_stores, last_store);
}

void EarlyCSE::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration())
            early_cse_on_function(func);
    }
    for (auto &p : canonical_constants) delete p.second;
    canonical_constants.clear();
}
