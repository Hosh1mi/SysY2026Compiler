#include "../../include/mid/opt/tailDuplication.hpp"
#include "../../include/mid/ir/ir.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <unordered_map>
#include <vector>

namespace {

static const int MAX_DUP_INSTS = 3;

// Check if an instruction is safe to clone (duplicate into predecessors).
static bool isClonableInstruction(Instruction *inst) {
    // Never clone terminators, phis, or memory operations.
    if (inst->is_br())   return false;
    if (inst->is_ret())  return false;
    if (inst->is_phi())  return false;
    if (inst->is_load()) return false;
    if (inst->is_store())return false;
    if (inst->is_call()) return false;
    if (inst->is_alloca())return false;
    if (inst->is_div())  return false;
    if (inst->is_rem())  return false;
    return true;
}

// Clone instruction into destBB, remapping operands via vmap.
// Supports all pure computation instruction types.
static Instruction *cloneInst(Instruction *orig, BasicBlock *destBB,
                              const std::unordered_map<Value *, Value *> &vmap) {
    auto remap = [&](Value *v) -> Value * {
        auto it = vmap.find(v);
        return it != vmap.end() ? it->second : v;
    };

    if (auto *bi = dynamic_cast<BinaryInst *>(orig))
        return new BinaryInst(bi->type_, bi->op_id_,
                              remap(bi->get_operand(0)),
                              remap(bi->get_operand(1)), destBB);

    if (auto *ui = dynamic_cast<UnaryInst *>(orig))
        return new UnaryInst(ui->type_, ui->op_id_,
                             remap(ui->get_operand(0)), destBB);

    if (auto *ci = dynamic_cast<ICmpInst *>(orig))
        return new ICmpInst(ci->icmp_op_,
                            remap(ci->get_operand(0)),
                            remap(ci->get_operand(1)), destBB);

    if (auto *fi = dynamic_cast<FCmpInst *>(orig))
        return new FCmpInst(fi->fcmp_op_,
                            remap(fi->get_operand(0)),
                            remap(fi->get_operand(1)), destBB);

    if (auto *gi = dynamic_cast<GetElementPtrInst *>(orig)) {
        std::vector<Value *> idxs;
        for (unsigned i = 1; i < gi->num_ops_; i++)
            idxs.push_back(remap(gi->get_operand(i)));
        return new GetElementPtrInst(remap(gi->get_operand(0)), idxs, destBB);
    }

    if (auto *zi = dynamic_cast<ZextInst *>(orig))
        return new ZextInst(zi->op_id_, remap(zi->get_operand(0)), zi->dest_ty_, destBB);

    if (auto *fp = dynamic_cast<FpToSiInst *>(orig))
        return new FpToSiInst(fp->op_id_, remap(fp->get_operand(0)), fp->dest_ty_, destBB);

    if (auto *sf = dynamic_cast<SiToFpInst *>(orig))
        return new SiToFpInst(sf->op_id_, remap(sf->get_operand(0)), sf->dest_ty_, destBB);

    if (auto *bc = dynamic_cast<Bitcast *>(orig))
        return new Bitcast(bc->op_id_, remap(bc->get_operand(0)), bc->dest_ty_, destBB);

    if (auto *sel = dynamic_cast<SelectInst *>(orig))
        return new SelectInst(remap(sel->get_operand(0)),
                              remap(sel->get_operand(1)),
                              remap(sel->get_operand(2)),
                              destBB);

    return nullptr;
}

// Collect non-phi, non-terminator instructions that are candidates for duplication.
static std::vector<Instruction *> getClonableInstructions(BasicBlock *bb) {
    std::vector<Instruction *> result;
    for (auto *inst : bb->instr_list_) {
        if (inst->is_phi()) continue;
        if (inst->isTerminator()) break;
        result.push_back(inst);
    }
    return result;
}

// Check that all instructions in instrs are safe to clone, and count
// non-clonable ones.
static bool allClonable(const std::vector<Instruction *> &instrs) {
    for (auto *inst : instrs) {
        if (!isClonableInstruction(inst)) return false;
    }
    return true;
}

} // namespace

bool TailDuplication::runOnFunction(Function *func) {
    bool changed = false;

    // Iterate over a snapshot of blocks; the collection may be modified.
    auto bbs = func->basic_blocks_;
    for (auto *BB : bbs) {
        if (BB->parent_ != func) continue;
        if (BB == func->basic_blocks_.front()) continue; // skip entry
        if (BB->pre_bbs_.size() < 2) continue;           // need multiple preds

        auto *term = BB->get_terminator();
        if (!term->is_br() && !term->is_ret()) continue;
        auto *br = dynamic_cast<BranchInst *>(term);
        if (br && br->num_ops_ != 1) continue; // only uncond branches

        auto instrs = getClonableInstructions(BB);
        if ((int)instrs.size() > MAX_DUP_INSTS) continue;

        // For now, only handle phi+ret dissolution (no instructions to clone).
        if (!term->is_ret()) continue;
        if (!instrs.empty()) continue;

        // Build phi remapping table for each predecessor.
        // For a phi %p = [vA, A], [vB, B], when duplicating into A,
        // we replace %p with vA.
        std::unordered_map<BasicBlock *, std::unordered_map<Value *, Value *>> phiRemap;
        for (auto *inst : BB->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(inst);
            for (unsigned i = 0; i + 1 < phi->num_ops_; i += 2) {
                auto *pred = static_cast<BasicBlock *>(phi->get_operand(i + 1));
                phiRemap[pred][phi] = phi->get_operand(i);
            }
        }

        // Only dissolve if ALL predecessors have unconditional branches.
        bool allUncond = true;
        for (auto *P : BB->pre_bbs_) {
            auto *pBr = dynamic_cast<BranchInst *>(P->get_terminator());
            if (pBr && pBr->num_ops_ == 3) { allUncond = false; break; }
        }
        if (!allUncond) continue;

        // Duplicate into each predecessor.
        auto preds = BB->pre_bbs_; // snapshot
        bool anyDone = false;
        for (auto *P : preds) {
            // Skip if P no longer references BB.
            bool stillPred = false;
            for (auto *s : P->succ_bbs_) {
                if (s == BB) { stillPred = true; break; }
            }
            if (!stillPred) continue;

            // Build remap for this predecessor: phi → incoming value.
            std::unordered_map<Value *, Value *> vmap;
            auto it = phiRemap.find(P);
            if (it != phiRemap.end()) {
                for (auto &[phi, val] : it->second)
                    vmap[phi] = val;
            }

            // Clone instructions into P if any, then redirect P's terminator.
            for (auto *inst : instrs) {
                auto *newInst = cloneInst(inst, P, vmap);
                if (!newInst) goto next_bb;
                P->add_instruction_before_inst(newInst, P->get_terminator());
                vmap[inst] = newInst;
            }

            // Compute the return value (remapped if it was a phi or cloned inst).
            Value *retVal = nullptr;
            if (term->is_ret() && term->num_ops_ > 0) {
                retVal = term->get_operand(0);
                auto mapIt = vmap.find(retVal);
                if (mapIt != vmap.end())
                    retVal = mapIt->second;
            }

            // Replace P's terminator, removing the edge P → BB.
            auto *pTermInst = P->get_terminator();
            P->remove_succ_basic_block(BB);
            BB->remove_pre_basic_block(P);
            P->delete_instr(pTermInst);

            if (term->is_ret()) {
                if (retVal)
                    new ReturnInst(retVal, P);
                else
                    new ReturnInst(P);
            } else {
                auto *target = static_cast<BasicBlock *>(
                    static_cast<BranchInst *>(term)->get_operand(0));
                new BranchInst(target, P);
            }
            anyDone = true;
            changed = true;
        }
        next_bb:;
        if (!anyDone) continue;

        // Clean up phi entries in BB for predecessors that were dissolved.
        for (auto *inst : BB->instr_list_) {
            if (!inst->is_phi()) break;
            auto *phi = static_cast<PhiInst *>(inst);

            // Collect block indices to remove (must do this before modifying).
            std::vector<int> toRemove;
            for (unsigned idx = 0; idx + 1 < phi->num_ops_; idx += 2) {
                auto *pred = dynamic_cast<BasicBlock *>(phi->get_operand(idx + 1));
                if (!pred) continue;
                bool predStillConnected = false;
                for (auto *s : pred->succ_bbs_) {
                    if (s == BB) { predStillConnected = true; break; }
                }
                if (!predStillConnected)
                    toRemove.push_back(idx + 1); // block operand index
            }

            // Remove from back to front to preserve earlier indices.
            for (auto rit = toRemove.rbegin(); rit != toRemove.rend(); ++rit) {
                int blockIdx = *rit;
                phi->remove_operands(blockIdx - 1, blockIdx);
            }
        }

        // Remove BB if all predecessors were dissolved.
        if (BB->pre_bbs_.empty()) {
            std::vector<Instruction *> dead(BB->instr_list_.begin(), BB->instr_list_.end());
            for (auto *inst : dead) BB->delete_instr(inst);
            func->remove_bb(BB);
        }
    }

    return changed;
}

void TailDuplication::execute(Module *module) {
    for (auto *func : module->function_list_) {
        if (func->is_declaration()) continue;
        runOnFunction(func);
    }
}
