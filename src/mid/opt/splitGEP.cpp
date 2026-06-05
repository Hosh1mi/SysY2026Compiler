#include "../../include/mid/opt/splitGEP.hpp"
#include "../../include/mid/ir/ir.hpp"
#include "../../include/mid/ir/instruction.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

static bool isSplitGEPDebugEnabled() {
    static bool enabled = std::getenv("DEBUG_SPLIT_GEP") != nullptr;
    return enabled;
}

void SplitGEP::execute(Module *module) {
    for (auto *f : module->function_list_) {
        if (f->is_declaration()) continue;
        // Temporary: only process first function
        runOnFunction(f);
    }
}

// ── simple loop detection via back-edges ────────────────────────────

std::vector<SplitGEP::Loop> SplitGEP::findLoops(Function *func) {
    std::vector<Loop> loops;

    for (auto *bb : func->basic_blocks_) {
        if (bb->instr_list_.empty()) continue;
        auto *term = bb->get_terminator();
        if (!term || !term->is_br()) continue;

        // Unconditional back-edge: br label %target where target dominates bb
        if (term->num_ops_ == 1) {
            auto *target = static_cast<BasicBlock*>(term->get_operand(0));
            // Simple heuristic: target comes before bb in function order
            auto itT = std::find(func->basic_blocks_.begin(),
                                 func->basic_blocks_.end(), target);
            auto itB = std::find(func->basic_blocks_.begin(),
                                 func->basic_blocks_.end(), bb);
            if (itT > itB) continue; // not a back-edge

            // Find the header: must be a conditional branch
            if (target->instr_list_.empty()) continue;
            auto *hdrTerm = target->get_terminator();
            if (!hdrTerm || hdrTerm->num_ops_ != 3) continue;

            Loop loop;
            loop.header = target;
            loop.latch  = bb;

            // Collect the natural loop by walking predecessors from the latch
            // back to the header.
            std::vector<BasicBlock*> worklist = {bb};
            std::set<BasicBlock*> visited = {target, bb};
            while (!worklist.empty()) {
                auto *cur = worklist.back(); worklist.pop_back();
                for (auto *pred : cur->pre_bbs_) {
                    if (visited.insert(pred).second)
                        worklist.push_back(pred);
                }
            }
            loop.blocks = visited;
            loops.push_back(loop);
        }
    }
    return loops;
}

// ── invariant / variant analysis ─────────────────────────────────────

bool SplitGEP::isVariant(Value *v, const std::set<BasicBlock*> &loopBlocks,
                          std::set<Value*> &variantCache,
                          std::set<Value*> &invariantCache) {
    if (variantCache.count(v)) return true;
    if (invariantCache.count(v)) return false;

    // Constants and globals are always invariant
    if (dynamic_cast<ConstantInt*>(v) || dynamic_cast<ConstantFloat*>(v) ||
        dynamic_cast<ConstantZero*>(v)   || dynamic_cast<GlobalVariable*>(v) ||
        dynamic_cast<AllocaInst*>(v)     || dynamic_cast<Function*>(v))
    {
        invariantCache.insert(v);
        return false;
    }

    auto *inst = dynamic_cast<Instruction*>(v);
    if (!inst) {
        invariantCache.insert(v);
        return false;
    }

    // Phi nodes at the header → the IV itself is variant
    if (inst->is_phi()) {
        auto *phi = static_cast<PhiInst*>(inst);
        // Check if this phi is in the loop header
        if (loopBlocks.count(phi->parent_) == 0 &&
            std::find(loopBlocks.begin(), loopBlocks.end(), phi->parent_) == loopBlocks.end())
        {
            // Phi not in loop → check if it's the header's phi
            // If the phi's parent is the header and one incoming is from the latch
            BasicBlock *header = nullptr;
            // We need to check if this phi is in the loop header.
            // For now, if the parent is not in loop blocks, it's invariant.
            invariantCache.insert(v);
            return false;
        }
        variantCache.insert(v);
        return true;
    }

    // Instruction in the loop: variant if any operand is variant
    if (loopBlocks.count(inst->parent_)) {
        bool anyVariant = false;
        for (unsigned i = 0; i < inst->num_ops_; i++) {
            Value *op = inst->get_operand(i);
            if (!op) continue;
            if (isVariant(op, loopBlocks, variantCache, invariantCache)) {
                anyVariant = true;
                // Don't break — need to fill caches completely
            }
        }
        if (anyVariant) {
            variantCache.insert(v);
            return true;
        }
        invariantCache.insert(v);
        return false;
    }

    // Instruction defined outside the loop → invariant
    invariantCache.insert(v);
    return false;
}

// ── split GEPs in one loop ──────────────────────────────────────────

bool SplitGEP::runOnLoop(const Loop &loop) {
    bool changed = false;

    // ScalarExpandedInterchange emits se_* loops with address shapes that its
    // later consumers assume stay intact. Splitting those GEPs can expose
    // non-equivalent pointer recurrences after vectorization/IVSR.
    if (loop.header && loop.header->name_.rfind("se_", 0) == 0)
        return false;

    for (auto *bb : loop.blocks) {
        std::vector<Instruction*> insts(bb->instr_list_.begin(),
                                        bb->instr_list_.end());

        for (auto *inst : insts) {
            if (!inst->is_gep()) continue;
            auto *gep = static_cast<GetElementPtrInst*>(inst);
            unsigned numIndices = gep->num_ops_ - 1;
            if (numIndices < 2) continue;

            std::set<Value*> variantCache, invariantCache;
            unsigned splitIdx = 0;
            for (unsigned i = 1; i <= numIndices; i++) {
                Value *idx = gep->get_operand(i);
                if (isVariant(idx, loop.blocks, variantCache, invariantCache))
                    break;
                splitIdx = i;
            }
            if (splitIdx == 0 || splitIdx == numIndices) continue;

            // Build invariant prefix GEP
            std::vector<Value*> prefixIdxs;
            for (unsigned i = 1; i <= splitIdx; i++)
                prefixIdxs.push_back(gep->get_operand(i));
            auto *prefixGEP = new GetElementPtrInst(
                gep->get_operand(0), prefixIdxs, bb, true);
            bool inserted = bb->add_instruction_before_inst(prefixGEP, inst);
            if (!inserted) {
                prefixGEP->remove_use_of_ops();
                delete prefixGEP;
                continue;
            }

            auto *prefixPtrTy = dynamic_cast<PointerType*>(prefixGEP->type_);
            if (!prefixPtrTy || prefixPtrTy->contained_->tid_ != Type::ArrayTyID) {
                bb->delete_instr(prefixGEP);
                continue;
            }

            // Build variant suffix GEP with normal GEP semantics. Since the
            // prefix result still points to an aggregate object, the suffix
            // must start with 0 before continuing with the remaining indices.
            std::vector<Value*> suffixIdxs;
            suffixIdxs.push_back(new ConstantInt(bb->parent_->parent_->int32_ty_, 0));
            for (unsigned i = splitIdx + 1; i <= numIndices; i++)
                suffixIdxs.push_back(gep->get_operand(i));
            auto *suffixGEP = new GetElementPtrInst(prefixGEP, suffixIdxs, bb, true);
            inserted = bb->add_instruction_before_inst(suffixGEP, inst);
            if (!inserted) {
                bb->delete_instr(prefixGEP);
                suffixGEP->remove_use_of_ops();
                delete suffixGEP;
                continue;
            }

            if (isSplitGEPDebugEnabled()) {
                std::cerr << "[SplitGEP] function=" << bb->parent_->name_
                          << " loop_header=" << loop.header->name_
                          << " bb=" << bb->name_
                          << " gep=%" << gep->name_
                          << " splitIdx=" << splitIdx
                          << " numIndices=" << numIndices
                          << " prefix=%" << prefixGEP->name_
                          << " suffix=%" << suffixGEP->name_
                          << "\n";
            }

            // Replace original GEP with suffix GEP
            inst->replace_all_use_with(suffixGEP);
            bb->delete_instr(inst);

            changed = true;
        }
    }

    return changed;
}

// ── entry point per function ─────────────────────────────────────────

void SplitGEP::runOnFunction(Function *func) {
    auto loops = findLoops(func);
    // Process innermost loops first (those with the smallest body)
    // so the invariant prefix of one loop can be further split by an outer loop.
    std::sort(loops.begin(), loops.end(),
        [](const Loop &a, const Loop &b) {
            return a.blocks.size() < b.blocks.size();
        });

    for (auto &loop : loops)
        runOnLoop(loop);
}
