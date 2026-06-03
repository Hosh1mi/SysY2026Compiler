#include "../../include/mid/opt/gvn.hpp"
#include "../../include/mid/opt/cse_common.hpp"
#include <functional>
#include <map>
#include <set>

// ---------- GVN: dominator-tree-based global value numbering ----------
static void gvn_on_function(Function *func) {
    if (func->basic_blocks_.empty()) return;

    auto &dom_children = func->getDominatorInfo().domChildren;
    BasicBlock *entry = func->basic_blocks_.front();

    std::unordered_map<Value*, Value*> vn_map;
    std::unordered_map<ExprSignature, Value*> scope_map;

    std::function<void(BasicBlock*)> dfs = [&](BasicBlock *bb) {
        // On entering a non-entry block, conservatively clear cached loads.
        // Cross-block load CSE requires proper alias/store analysis; the
        // dominator-tree scope alone is not sufficient: a sibling block's
        // store may not yet be processed but could incorrectly eliminate a
        // load in another sibling.
        if (bb != entry) {
            for (auto si = scope_map.begin(); si != scope_map.end(); ) {
                if (si->first.op_id == Instruction::Load)
                    si = scope_map.erase(si);
                else
                    ++si;
            }
        }

        std::vector<Instruction*> to_delete;
        std::vector<ExprSignature> added_sigs;   // signatures added in this block

        for (auto it = bb->instr_list_.begin(); it != bb->instr_list_.end(); ) {
            Instruction *inst = *it;
            ++it;

            if (!is_safe_to_eliminate(inst)) {
                vn_map[inst] = inst;
                if (inst->is_store() || inst->is_call()) {
                    for (auto si = scope_map.begin(); si != scope_map.end(); ) {
                        if (si->first.op_id == Instruction::Load)
                            si = scope_map.erase(si);
                        else
                            ++si;
                    }
                }
                continue;
            }

            ExprSignature sig = compute_signature(inst, vn_map);
            auto exist = scope_map.find(sig);
            if (exist != scope_map.end()) {
                Value *repl = exist->second;
                vn_map[inst] = repl;
                inst->replace_all_use_with(repl);
                to_delete.push_back(inst);
            } else {
                vn_map[inst] = inst;
                scope_map[sig] = inst;
                added_sigs.push_back(sig);
            }
        }

        // Delete eliminated instructions
        for (auto *inst : to_delete) {
            bb->remove_instr(inst);
        }

        // Recurse into dominator-tree children
        auto itc = dom_children.find(bb);
        if (itc != dom_children.end()) {
            for (auto *child : itc->second) {
                dfs(child);
            }
        }

        // On exit: remove signatures added in this block
        for (auto &sig : added_sigs) {
            scope_map.erase(sig);
        }
    };

    dfs(entry);
}

void GVN::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) {
            gvn_on_function(func);
        }
    }
    // Clean up canonical constants
    for (auto &p : canonical_constants) delete p.second;
    canonical_constants.clear();
}
