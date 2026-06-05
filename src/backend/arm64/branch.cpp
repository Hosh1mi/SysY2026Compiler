#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

// ---- PHI resolution ----

void Arm64FuncContext::preparePhi() {
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (!inst->is_phi()) continue;
            auto phi = static_cast<PhiInst*>(inst);
            int phiSlot = getSlot(phi);
            for (int i = 0; i < phi->num_ops_ / 2; i++) {
                auto val = phi->get_operand(2 * i);
                auto predBB = static_cast<BasicBlock*>(phi->get_operand(2 * i + 1));
                phiCopies_.push_back({predBB, bb, val, phiSlot, phi});
            }
        }
    }
}

void Arm64FuncContext::emitPhiCopies(BasicBlock *pred, BasicBlock *succ) {
    std::vector<PhiCopy> copies;
    for (const auto &pc : phiCopies_) {
        if (pc.pred == pred && pc.succ == succ) {
            // Skip phi copies already handled by csel
            if (cselHandled_.count({pred, pc.phi})) continue;
            copies.push_back(pc);
        }
    }
    if (copies.empty()) return;

    resetRegs();

    // Process phi copies in batches to preserve parallel copy semantics
    // (all sources read before any destination written, within a batch)
    // while never exceeding the scratch register pool capacity (r9–r16 = 8).
    // Float copies use a separate pool (s16–s31 = 16) so they are fine.
    static const int MAX_BATCH = 7;  // leave one register for address base
    size_t idx = 0;
    while (idx < copies.size()) {
        // ---- Phase 1: load up to MAX_BATCH source values ----
        struct Entry { Value *phi; std::string tmpReg; int dstSlot; bool isFloat; };
        std::vector<Entry> entries;
        for (size_t end = std::min(idx + MAX_BATCH, copies.size()); idx < end; ++idx) {
            const auto &cp = copies[idx];
            Value *val = cp.src;

            // Vector phi: the phi destination is always in a colored register,
            // but the source may be a ConstantVector (no register) or in a
            // stack slot. Materialize into a scratch reg and copy to phi.
            if (isVector(val->type_)) {
                if (cp.phi && hasAssignedReg(cp.phi)) {
                    std::string dstReg = assignedReg(cp.phi);
                    std::string srcReg;
                    if (hasAssignedReg(val)) {
                        srcReg = assignedReg(val);
                    } else if (dynamic_cast<Constant*>(val)) {
                        srcReg = loadVector(val);
                    } else {
                        srcReg = allocNEONReg();
                        int off = getSlot(val);
                        // Load 16 bytes from stack: use sub+ldr for negative off
                        if (off >= 0 && off <= 65520 && off % 16 == 0)
                            os_ << "\tldr q" << srcReg.substr(1) << ", [x29, #" << off << "]\n";
                        else {
                            int pos = -off;
                            if (pos <= 4095) os_ << "\tsub x16, x29, #" << pos << "\n";
                            else { os_ << "\tmovz x16, #" << (pos & 0xFFFF) << "\n";
                                   os_ << "\tmovk x16, #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
                                   os_ << "\tsub x16, x29, x16\n"; }
                            os_ << "\tldr q" << srcReg.substr(1) << ", [x16]\n";
                        }
                    }
                    if (srcReg != dstReg)
                        os_ << "\tmov " << dstReg << ".16b, " << srcReg << ".16b\n";
                }
                continue;
            }

            if (hasAssignedReg(val) && cp.phi && hasAssignedReg(cp.phi)) {
                bool asPtr = isPtr(val->type_);
                if (assignedReg(val, asPtr) == assignedReg(cp.phi, asPtr))
                    continue;
            }

            std::string tmpReg;
            if (isFloat(val->type_)) {
                if (hasAssignedReg(val)) {
                    std::string srcReg = assignedReg(val);
                    tmpReg = allocFloatReg();
                    if (tmpReg != srcReg) os_ << "\tfmov " << tmpReg << ", " << srcReg << "\n";
                } else if (auto cf = dynamic_cast<ConstantFloat*>(val)) {
                    tmpReg = allocFloatReg();
                    emitFloatConst(cf->value_, tmpReg);
                } else {
                    tmpReg = allocFloatReg();
                    emitLoadReg(os_, tmpReg, getSlot(val));
                }
            } else if (isPtr(val->type_)) {
                if (hasAssignedReg(val)) {
                    std::string srcReg = assignedReg(val, true);
                    tmpReg = allocAddrReg();
                    if (tmpReg != srcReg) os_ << "\tmov " << tmpReg << ", " << srcReg << "\n";
                } else if (auto gv = dynamic_cast<GlobalVariable*>(val)) {
                    tmpReg = allocAddrReg();
                    emitGlobalAddr(gv, tmpReg);
                } else {
                    tmpReg = allocAddrReg();
                    emitLoadReg(os_, tmpReg, getSlot(val));
                }
            } else { // int
                if (hasAssignedReg(val)) {
                    std::string srcReg = assignedReg(val);
                    tmpReg = allocIntReg();
                    if (tmpReg != srcReg) os_ << "\tmov " << tmpReg << ", " << srcReg << "\n";
                } else if (auto ci = dynamic_cast<ConstantInt*>(val)) {
                    tmpReg = allocIntReg();
                    emitIntConst(ci->value_, tmpReg);
                } else {
                    tmpReg = allocIntReg();
                    emitLoadReg(os_, tmpReg, getSlot(val));
                }
            }
            entries.push_back({cp.phi, tmpReg, cp.dstSlot, isFloat(val->type_)});
        }

        // ---- Phase 2: write all destinations in this batch, then free ----
        for (const auto &e : entries) {
            if (e.phi && hasAssignedReg(e.phi)) {
                bool isPhiPtr = e.phi->type_->tid_ == Type::PointerTyID;
                std::string dstReg = assignedReg(e.phi, isPhiPtr);
                if (e.isFloat) {
                    if (e.tmpReg != dstReg) os_ << "\tfmov " << dstReg << ", " << e.tmpReg << "\n";
                } else {
                    if (e.tmpReg != dstReg) os_ << "\tmov " << dstReg << ", " << e.tmpReg << "\n";
                }
            } else {
                emitStoreReg(os_, e.tmpReg, e.dstSlot);
            }
        }
        for (const auto &e : entries) {
            if (e.isFloat) continue;
            if (e.tmpReg[0] == 'x') freeAddrReg(e.tmpReg);
            else if (e.tmpReg[0] == 'w') freeIntReg(e.tmpReg);
        }
        entries.clear();
    }
}

// ---- ICmp + Br fusion ----

static const char *invertCond(const char *cond) {
    if (strcmp(cond, "eq") == 0) return "ne";
    if (strcmp(cond, "ne") == 0) return "eq";
    if (strcmp(cond, "gt") == 0) return "le";
    if (strcmp(cond, "ge") == 0) return "lt";
    if (strcmp(cond, "lt") == 0) return "ge";
    if (strcmp(cond, "le") == 0) return "gt";
    if (strcmp(cond, "hi") == 0) return "ls";
    if (strcmp(cond, "hs") == 0) return "lo";
    if (strcmp(cond, "lo") == 0) return "hs";
    if (strcmp(cond, "ls") == 0) return "hi";
    return cond;
}

// Try to convert an icmp+br diamond into cmp+csel.
// Pattern A (if-else):  icmp → br trueBB / falseBB
//   trueBB:  1 inst + br mergeBB
//   falseBB: 1 inst + br mergeBB
//   mergeBB: phi (trueVal, falseVal)
// Pattern B (if-then):  icmp → br trueBB / mergeBB
//   trueBB:  1 inst + br mergeBB
//   falseBB == mergeBB
//   mergeBB: phi (trueVal, oldVal-from-cond-block)
// Replaces with: cmp; csel phi, trueVal, falseVal, cond; b merge
bool Arm64FuncContext::tryEmitCSel(ICmpInst *icmp, BranchInst *br) {
    auto *trueBB  = static_cast<BasicBlock*>(br->get_operand(1));
    auto *falseBB = static_cast<BasicBlock*>(br->get_operand(2));
    if (trueBB == falseBB) return false;

    // Get the single non-phi non-terminator instruction in a BB
    auto getSingleInst = [](BasicBlock *bb) -> Instruction* {
        Instruction *found = nullptr;
        for (auto *i : bb->instr_list_) {
            if (i->is_phi()) continue;
            if (i->isTerminator()) break;
            if (found) return nullptr;
            found = i;
        }
        return found;
    };

    Instruction *trueInst = getSingleInst(trueBB);
    if (!trueInst) return false;
    // Reject unsafe instructions: calls clobber flags+regs, loads/stores
    // have side effects, div/rem may trap.
    if (trueInst->is_call() || trueInst->is_load() || trueInst->is_store() ||
        trueInst->is_div() || trueInst->is_rem()) return false;

    auto *trueTerm = trueBB->get_terminator();
    if (!trueTerm || !trueTerm->is_br() || trueTerm->num_ops_ != 1) return false;
    auto *mergeBB = static_cast<BasicBlock*>(trueTerm->get_operand(0));

    Instruction *falseInst = nullptr;
    Value *falsePhiVal = nullptr;

    if (falseBB == mergeBB) {
        // Pattern B: if-then (false path goes directly to merge)
        // falsePhiVal is whatever the cond block provides to the phi
    } else {
        // Pattern A: if-else (both paths have a single assignment)
        falseInst = getSingleInst(falseBB);
        if (!falseInst) return false;
        auto *falseTerm = falseBB->get_terminator();
        if (!falseTerm || !falseTerm->is_br() || falseTerm->num_ops_ != 1) return false;
        if (static_cast<BasicBlock*>(falseTerm->get_operand(0)) != mergeBB) return false;
        if (falseInst->is_call() || falseInst->is_load() ||
            falseInst->is_store() || falseInst->is_div() || falseInst->is_rem())
            return false;
    }

    // Find the phi in mergeBB that takes trueInst from trueBB
    // and (falseInst or an old value) from the cond block / falseBB
    PhiInst *phi = nullptr;
    Value *phiOld = nullptr;
    for (auto *i : mergeBB->instr_list_) {
        if (!i->is_phi()) break;
        auto *p = static_cast<PhiInst*>(i);
        Value *vT = nullptr, *vF = nullptr;
        for (unsigned k = 0; k < p->num_ops_; k += 2) {
            auto *pred = static_cast<BasicBlock*>(p->get_operand(k + 1));
            if (pred == trueBB)  vT = p->get_operand(k);
            if (!falseInst && pred == br->parent_) { vF = p->get_operand(k); phiOld = vF; }
            if (falseInst && pred == falseBB) vF = p->get_operand(k);
        }
        if (vT == trueInst && vF) {
            phi = p;
            if (!falseInst) falsePhiVal = vF;
            if (falseInst) falsePhiVal = falseInst;
            break;
        }
    }
    if (!phi || !falsePhiVal) return false;

    // Emit the true instruction inline so its register holds the right
    // value before the csel reads it.  (Calls are already rejected above.)
    emitInstruction(trueInst);

    // Emit the false instruction inline as well.
    // Both trueInst and falseInst must be computed before the csel because
    // the original CFG branches to them, but csel bypasses both blocks.
    // Also, the regalloc may give them the same register (their live ranges
    // don't overlap in the original CFG), so we must save trueInst's result
    // before falseInst potentially clobbers it.
    std::string trueSaveReg;
    std::string trueAssigned = hasAssignedReg(trueInst) ? assignedReg(trueInst) : "";
    if (falseInst && hasAssignedReg(falseInst) &&
        assignedReg(falseInst) == trueAssigned && !trueAssigned.empty()) {
        trueSaveReg = allocIntReg();
        os_ << "\tmov " << trueSaveReg << ", " << trueAssigned << "\n";
    }
    if (falseInst)
        emitInstruction(falseInst);

    // Emit cmp to set flags for csel.
    auto *v1 = icmp->get_operand(0);
    auto *v2 = icmp->get_operand(1);
    std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
    const char *cond = icmpCond(icmp->icmp_op_);

    auto emitCmp = [&]() {
        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            int val = ci->value_;
            if (val >= 0 && val <= 4095)
                os_ << "\tcmp " << r1 << ", #" << val << "\n";
            else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                os_ << "\tcmp " << r1 << ", " << r2 << "\n"; }
        } else {
            std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
            os_ << "\tcmp " << r1 << ", " << r2 << "\n";
        }
    };
    emitCmp();

    // Write csel result directly to the phi's assigned register.
    // For Pattern A, trueInst was already emitted; if we saved its result
    // (to avoid clobbering by falseInst), use the saved register.
    std::string dstReg   = hasAssignedReg(phi)       ? assignedReg(phi)       : allocIntReg();
    std::string trueReg  = !trueSaveReg.empty()      ? trueSaveReg
                         : hasAssignedReg(trueInst)  ? assignedReg(trueInst)  : loadInt(trueInst);
    // For Pattern A, falseInst was emitted above; its value is now in its assigned reg or slot.
    // For Pattern B, falsePhiVal comes from the cond block (already live).
    std::string falseReg = hasAssignedReg(falsePhiVal)? assignedReg(falsePhiVal): loadInt(falsePhiVal);
    os_ << "\tcsel " << dstReg << ", " << trueReg << ", " << falseReg << ", " << cond << "\n";
    if (!hasAssignedReg(phi)) storeInt(phi, dstReg);

    // Mark this phi copy as handled — don't emit it again
    cselHandled_.insert({br->parent_, phi});

    // Emit other phi copies and the branch
    emitPhiCopies(br->parent_, mergeBB);
    os_ << "\tb " << bbLabel(func_, mergeBB) << "\n";

    // Don't skip trueBB/falseBB — their phi copies are still needed
    // for proper value flow to the merge block.
    return true;
}

// Detect nested short-circuit AND:  icmp1 → br checkBB / merge
//   checkBB: icmp2 → br trueBB / merge
//   trueBB:  single inst → br merge
// Emits:  cmp; ccmp; csel  (combined condition)
bool Arm64FuncContext::tryEmitCCmpCSel(ICmpInst *icmp1, BranchInst *br1) {
    auto *trueBB1  = static_cast<BasicBlock*>(br1->get_operand(1));
    auto *falseBB1 = static_cast<BasicBlock*>(br1->get_operand(2));
    auto *bb = br1->parent_;

    // trueBB1 must end with icmp2 + conditional br
    if (trueBB1->instr_list_.empty()) return false;
    auto *term2 = trueBB1->get_terminator();
    if (!term2 || !term2->is_br() || term2->num_ops_ != 3) return false;
    auto *icmp2 = dynamic_cast<ICmpInst*>(term2->get_operand(0));
    if (!icmp2 || icmp2->parent_ != trueBB1) return false;
    // icmp2 must be right before term2
    auto it2 = std::find(trueBB1->instr_list_.rbegin(), trueBB1->instr_list_.rend(), term2);
    if (it2 == trueBB1->instr_list_.rend() || *++it2 != icmp2) return false;
    if (icmp2->use_list_.size() != 1) return false;

    auto *trueBB2  = static_cast<BasicBlock*>(term2->get_operand(1));
    auto *falseBB2 = static_cast<BasicBlock*>(term2->get_operand(2));

    // Both false paths must go to the SAME merge (short-circuit AND)
    if (falseBB1 != falseBB2) return false;
    auto *mergeBB = falseBB1;

    // trueBB2: single non-phi instruction + unconditional br to merge
    auto getSingle = [](BasicBlock *b) -> Instruction* {
        Instruction *f = nullptr;
        for (auto *i : b->instr_list_) {
            if (i->is_phi()) continue;
            if (i->isTerminator()) break;
            if (f) return nullptr;
            f = i;
        }
        return f;
    };
    Instruction *trueInst = getSingle(trueBB2);
    if (!trueInst) return false;
    auto *trueTerm2 = trueBB2->get_terminator();
    if (!trueTerm2 || !trueTerm2->is_br() || trueTerm2->num_ops_ != 1) return false;
    if (static_cast<BasicBlock*>(trueTerm2->get_operand(0)) != mergeBB) return false;

    // trueBB1 must ONLY contain icmp2 + branch (no other instructions)
    // (otherwise operand liveness is uncertain)
    for (auto *i : trueBB1->instr_list_) {
        if (i->is_phi()) continue;
        if (i == icmp2 || i == term2) continue;
        return false; // extra instruction in check block
    }

    // Find phi in mergeBB that takes trueInst from trueBB2
    PhiInst *phi = nullptr;
    Value *falseVal = nullptr;
    for (auto *i : mergeBB->instr_list_) {
        if (!i->is_phi()) break;
        auto *p = static_cast<PhiInst*>(i);
        Value *vT = nullptr, *vF = nullptr;
        for (unsigned k = 0; k < p->num_ops_; k += 2) {
            if (static_cast<BasicBlock*>(p->get_operand(k + 1)) == trueBB2)
                vT = p->get_operand(k);
            if (static_cast<BasicBlock*>(p->get_operand(k + 1)) == bb)
                vF = p->get_operand(k);
        }
        if (vT == trueInst && vF) { phi = p; falseVal = vF; break; }
    }
    if (!phi || !falseVal) return false;

    // --- Emit: cmp; ccmp; trueInst_inline; csel ---
    auto emitCmp = [&](ICmpInst *icmp) {
        auto *v1 = icmp->get_operand(0);
        auto *v2 = icmp->get_operand(1);
        std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            int val = ci->value_;
            if (val >= 0 && val <= 4095)
                os_ << "\tcmp " << r1 << ", #" << val << "\n";
            else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                os_ << "\tcmp " << r1 << ", " << r2 << "\n"; }
        } else { std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
            os_ << "\tcmp " << r1 << ", " << r2 << "\n"; }
    };

    // cmp for OUTER icmp (sets initial flags)
    emitCmp(icmp1);

    // ccmp for INNER icmp, conditioned on outer icmp's flags
    {
        auto *v1 = icmp2->get_operand(0);
        auto *v2 = icmp2->get_operand(1);
        std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
        const char *outerCond = icmpCond(icmp1->icmp_op_);
        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            int val = ci->value_;
            os_ << "\tccmp " << r1 << ", #" << val << ", #0, " << outerCond << "\n";
        } else { std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
            os_ << "\tccmp " << r1 << ", " << r2 << ", #0, " << outerCond << "\n"; }
    }

    // Emit the true instruction inline, then re-establish flags for csel.
    // Save icmp operand regs BEFORE trueInst (which may reuse those regs).
    auto *io1_v1 = icmp1->get_operand(0), *io1_v2 = icmp1->get_operand(1);
    auto *io2_v1 = icmp2->get_operand(0), *io2_v2 = icmp2->get_operand(1);
    std::string io1_r1 = isPtr(io1_v1->type_) ? loadAddr(io1_v1) : loadInt(io1_v1);
    std::string io2_r1 = isPtr(io2_v1->type_) ? loadAddr(io2_v1) : loadInt(io2_v1);
    bool io1_isImm = dynamic_cast<ConstantInt*>(io1_v2) != nullptr;
    bool io2_isImm = dynamic_cast<ConstantInt*>(io2_v2) != nullptr;
    int io1_imm = io1_isImm ? static_cast<ConstantInt*>(io1_v2)->value_ : 0;
    int io2_imm = io2_isImm ? static_cast<ConstantInt*>(io2_v2)->value_ : 0;
    std::string io1_r2 = io1_isImm ? "" : (isPtr(io1_v2->type_) ? loadAddr(io1_v2) : loadInt(io1_v2));
    std::string io2_r2 = io2_isImm ? "" : (isPtr(io2_v2->type_) ? loadAddr(io2_v2) : loadInt(io2_v2));

    emitInstruction(trueInst);

    // Re-emit cmp (outer) + ccmp (inner) using saved operand regs
    if (io1_isImm && io1_imm >= 0 && io1_imm <= 4095)
        os_ << "\tcmp " << io1_r1 << ", #" << io1_imm << "\n";
    else
        os_ << "\tcmp " << io1_r1 << ", " << io1_r2 << "\n";

    {
        const char *outerCond = icmpCond(icmp1->icmp_op_);
        if (io2_isImm)
            os_ << "\tccmp " << io2_r1 << ", #" << io2_imm << ", #0, " << outerCond << "\n";
        else
            os_ << "\tccmp " << io2_r1 << ", " << io2_r2 << ", #0, " << outerCond << "\n";
    }

    const char *outerCond = icmpCond(icmp1->icmp_op_);
    std::string dstReg   = hasAssignedReg(phi)       ? assignedReg(phi)       : allocIntReg();
    std::string trueReg  = hasAssignedReg(trueInst)  ? assignedReg(trueInst)  : loadInt(trueInst);
    std::string falseReg = hasAssignedReg(falseVal)  ? assignedReg(falseVal)  : loadInt(falseVal);
    os_ << "\tcsel " << dstReg << ", " << trueReg << ", " << falseReg << ", " << outerCond << "\n";
    if (!hasAssignedReg(phi)) storeInt(phi, dstReg);

    cselHandled_.insert({bb, phi});
    emitPhiCopies(bb, mergeBB);
    os_ << "\tb " << bbLabel(func_, mergeBB) << "\n";
    return true;
}

void Arm64FuncContext::emitFusedCmpBranch(ICmpInst *icmp, BranchInst *br) {
    auto v1 = icmp->get_operand(0);
    auto v2 = icmp->get_operand(1);

    auto trueBB  = static_cast<BasicBlock*>(br->get_operand(1));
    auto falseBB = static_cast<BasicBlock*>(br->get_operand(2));
    BasicBlock *parentBB = br->parent_;

    const char *cond = icmpCond(icmp->icmp_op_);

    // Check for phi copies on edges
    bool hasTrue = false, hasFalse = false;
    for (const auto &pc : phiCopies_) {
        if (pc.pred != parentBB) continue;
        if (pc.succ == trueBB)  hasTrue  = true;
        if (pc.succ == falseBB) hasFalse = true;
    }

    // cmp r, #0; b.eq/b.ne → cbz/cbnz (single instruction)
    if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
        if (ci->value_ == 0 && (strcmp(cond, "eq") == 0 || strcmp(cond, "ne") == 0)) {
            bool useCbz = (strcmp(cond, "eq") == 0);
            std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
            const char *op = useCbz ? "cbz" : "cbnz";

            if (!hasTrue && !hasFalse) {
                os_ << "\t" << op << " " << r1 << ", " << bbLabel(func_, trueBB) << "\n";
                os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
            } else if (hasTrue && !hasFalse) {
                // Invert: if eq→cbz goes to trueBB, then ne case goes to falseBB via cbnz
                const char *invOp = useCbz ? "cbnz" : "cbz";
                os_ << "\t" << invOp << " " << r1 << ", " << bbLabel(func_, falseBB) << "\n";
                emitPhiCopies(parentBB, trueBB);
                os_ << "\tb " << bbLabel(func_, trueBB) << "\n";
            } else if (!hasTrue && hasFalse) {
                os_ << "\t" << op << " " << r1 << ", " << bbLabel(func_, trueBB) << "\n";
                emitPhiCopies(parentBB, falseBB);
                os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
            } else {
                std::string edgeLbl = ".L" + func_->name_ + "_edge_" + std::to_string(edgeCounter_++);
                const char *invOp = useCbz ? "cbnz" : "cbz";
                os_ << "\t" << invOp << " " << r1 << ", " << edgeLbl << "\n";
                emitPhiCopies(parentBB, trueBB);
                os_ << "\tb " << bbLabel(func_, trueBB) << "\n";
                os_ << edgeLbl << ":\n";
                emitPhiCopies(parentBB, falseBB);
                os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
            }
            return;
        }
    }

    std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);

    // Emit cmp with immediate if possible (ARM64 cmp imm is 12-bit: 0-4095)
    if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
        int val = ci->value_;
        if (val >= 0 && val <= 4095) {
            os_ << "\tcmp " << r1 << ", #" << val << "\n";
        } else {
            std::string r2 = allocIntReg();
            emitIntConst(val, r2);
            os_ << "\tcmp " << r1 << ", " << r2 << "\n";
        }
    } else {
        std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
        os_ << "\tcmp " << r1 << ", " << r2 << "\n";
    }

    if (!hasTrue && !hasFalse) {
        os_ << "\tb." << cond << " " << bbLabel(func_, trueBB) << "\n";
        os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
    } else if (hasTrue && !hasFalse) {
        os_ << "\tb." << invertCond(cond) << " " << bbLabel(func_, falseBB) << "\n";
        emitPhiCopies(parentBB, trueBB);
        os_ << "\tb " << bbLabel(func_, trueBB) << "\n";
    } else if (!hasTrue && hasFalse) {
        os_ << "\tb." << cond << " " << bbLabel(func_, trueBB) << "\n";
        emitPhiCopies(parentBB, falseBB);
        os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
    } else {
        std::string edgeLbl = ".L" + func_->name_ + "_edge_" + std::to_string(edgeCounter_++);
        os_ << "\tb." << invertCond(cond) << " " << edgeLbl << "\n";
        emitPhiCopies(parentBB, trueBB);
        os_ << "\tb " << bbLabel(func_, trueBB) << "\n";
        os_ << edgeLbl << ":\n";
        emitPhiCopies(parentBB, falseBB);
        os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
    }
}

// ---- condition code mapping ----

const char *Arm64FuncContext::icmpCond(ICmpInst::ICmpOp op) {
    switch (op) {
    case ICmpInst::ICMP_EQ:  return "eq";
    case ICmpInst::ICMP_NE:  return "ne";
    case ICmpInst::ICMP_SGT: return "gt";
    case ICmpInst::ICMP_SGE: return "ge";
    case ICmpInst::ICMP_SLT: return "lt";
    case ICmpInst::ICMP_SLE: return "le";
    default: return "eq";
    }
}

const char *Arm64FuncContext::fcmpCond(FCmpInst::FCmpOp op) {
    switch (op) {
    case FCmpInst::FCMP_UEQ: return "eq";
    case FCmpInst::FCMP_UNE: return "ne";
    case FCmpInst::FCMP_UGT: return "hi";
    case FCmpInst::FCMP_UGE: return "hs";
    case FCmpInst::FCMP_ULT: return "lo";
    case FCmpInst::FCMP_ULE: return "ls";
    default: return "eq";
    }
}
