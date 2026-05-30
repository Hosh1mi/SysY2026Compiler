#include "../../include/mid/opt/earlyCSE.hpp"
#include "../../include/mid/opt/cse_common.hpp"

// ---------- EarlyCSE: per-block local CSE (no dominator tree) ----------
static void early_cse_on_function(Function *func) {
    if (func->basic_blocks_.empty()) return;

    // Function-wide value-number map: ensures operands get consistent VNs
    // across blocks. The elimination scope (local_map) is per-BB.
    std::unordered_map<Value*, Value*> vn_map;

    for (auto *bb : func->basic_blocks_) {
        std::unordered_map<ExprSignature, Value*> local_map;
        std::vector<Instruction*> to_delete;

        for (auto it = bb->instr_list_.begin(); it != bb->instr_list_.end(); ) {
            Instruction *inst = *it;
            ++it;

            if (!is_safe_to_eliminate(inst)) {
                vn_map[inst] = inst;
                if (inst->is_store() || inst->is_call()) {
                    // Intra-BB load invalidation: a store/call may alias any load
                    for (auto si = local_map.begin(); si != local_map.end(); ) {
                        if (si->first.op_id == Instruction::Load)
                            si = local_map.erase(si);
                        else
                            ++si;
                    }
                }
                continue;
            }

            ExprSignature sig = compute_signature(inst, vn_map);
            auto exist = local_map.find(sig);
            if (exist != local_map.end()) {
                Value *repl = exist->second;
                vn_map[inst] = repl;
                inst->replace_all_use_with(repl);
                to_delete.push_back(inst);
            } else {
                vn_map[inst] = inst;
                local_map[sig] = inst;
            }
        }

        // Delete eliminated instructions
        for (auto *inst : to_delete) {
            bb->remove_instr(inst);
        }
    }
}

void EarlyCSE::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (!func->is_declaration()) {
            early_cse_on_function(func);
        }
    }
    // Clean up canonical constants
    for (auto &p : canonical_constants) delete p.second;
    canonical_constants.clear();
}
