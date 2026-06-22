#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

// ---- PHI resolution ----

void Arm64FuncContext::preparePhi() {
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (!inst->is_phi()) continue;
            auto phi = static_cast<PhiInst*>(inst);
            int phiSlot = hasAssignedReg(phi) ? 0 : getSlot(phi);
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

    // Fast path for the common case: every edge copy writes to an assigned
    // scalar phi register and the parallel-copy graph is acyclic.  The old
    // fallback below first materializes every source in scratch registers,
    // which is correct but turns simple phi updates into two-hop mov chains.
    struct DirectPhiCopy {
        Value *src;
        std::string dstReg;
        std::string srcReg;
        std::string srcKey;
        std::string dstKey;
        bool hasSrcReg;
        bool isFloat;
        bool isPtr;
    };

    auto regKey = [](const std::string &reg) -> std::string {
        if (reg.size() < 2) return reg;
        if ((reg[0] == 'w' || reg[0] == 'x') && std::isdigit(reg[1]))
            return "r" + reg.substr(1);
        if ((reg[0] == 's' || reg[0] == 'd') && std::isdigit(reg[1]))
            return "f" + reg.substr(1);
        return reg;
    };

    auto emitDirectCopy = [&](const DirectPhiCopy &cp) {
        if (cp.hasSrcReg) {
            if (cp.srcReg != cp.dstReg)
                emitMoveMachine(cp.dstReg, cp.srcReg, cp.isFloat ? "fmov" : "mov");
            return;
        }

        if (cp.isFloat) {
            if (auto *cf = dynamic_cast<ConstantFloat*>(cp.src)) {
                emitFloatConst(cf->value_, cp.dstReg);
            } else {
                emitLoadRegMachine(cp.dstReg, getSlot(cp.src));
            }
        } else if (cp.isPtr) {
            if (auto *gv = dynamic_cast<GlobalVariable*>(cp.src)) {
                emitGlobalAddr(gv, cp.dstReg);
            } else if (auto *ci = dynamic_cast<ConstantInt*>(cp.src)) {
                emitIntConst(ci->value_, cp.dstReg);
            } else {
                emitLoadRegMachine(cp.dstReg, getSlot(cp.src));
            }
        } else {
            if (auto *ci = dynamic_cast<ConstantInt*>(cp.src)) {
                emitIntConst(ci->value_, cp.dstReg);
            } else {
                emitLoadRegMachine(cp.dstReg, getSlot(cp.src));
            }
        }
    };

    std::vector<DirectPhiCopy> directCopies;
    bool canUseDirectCopies = true;
    for (const auto &cp : copies) {
        Value *val = cp.src;
        if (!cp.phi || !hasAssignedReg(cp.phi) || isVector(val->type_)) {
            canUseDirectCopies = false;
            break;
        }

        bool phiIsFloat = isFloat(cp.phi->type_);
        bool phiIsPtr = isPtr(cp.phi->type_);
        bool valIsPtr = isPtr(val->type_);
        std::string dstReg = assignedReg(cp.phi, phiIsPtr);

        DirectPhiCopy entry{val, dstReg, "", "", regKey(dstReg),
                            false, phiIsFloat, phiIsPtr};
        if (hasAssignedReg(val)) {
            entry.srcReg = assignedReg(val, valIsPtr);
            entry.srcKey = regKey(entry.srcReg);
            entry.hasSrcReg = true;
            if (entry.srcReg == entry.dstReg) continue;
        } else if (phiIsFloat) {
            if (!dynamic_cast<ConstantFloat*>(val) && dynamic_cast<Constant*>(val)) {
                canUseDirectCopies = false;
                break;
            }
        } else if (!phiIsPtr) {
            if (!dynamic_cast<ConstantInt*>(val) && dynamic_cast<Constant*>(val)) {
                canUseDirectCopies = false;
                break;
            }
        }
        directCopies.push_back(entry);
    }

    if (canUseDirectCopies) {
        std::vector<size_t> schedule;
        std::vector<bool> done(directCopies.size(), false);

        while (schedule.size() < directCopies.size()) {
            bool progressed = false;
            for (size_t i = 0; i < directCopies.size(); ++i) {
                if (done[i]) continue;

                bool dstStillNeededAsSource = false;
                for (size_t j = 0; j < directCopies.size(); ++j) {
                    if (done[j] || i == j || !directCopies[j].hasSrcReg) continue;
                    if (directCopies[i].dstKey == directCopies[j].srcKey) {
                        dstStillNeededAsSource = true;
                        break;
                    }
                }
                if (dstStillNeededAsSource) continue;

                done[i] = true;
                schedule.push_back(i);
                progressed = true;
                break;
            }

            if (!progressed) {
                canUseDirectCopies = false;
                break;
            }
        }

        if (canUseDirectCopies) {
            for (size_t idx : schedule)
                emitDirectCopy(directCopies[idx]);
            return;
        }
    }

    // Process phi copies in batches to preserve parallel copy semantics
    // (all sources read before any destination written, within a batch)
    // while never exceeding the scratch register pool capacity (r10–r16 = 7).
    // Float copies use a separate pool (s16–s31 = 16) so they are fine.
    static const int MAX_BATCH = 6;  // leave one register for address base
    size_t idx = 0;
    while (idx < copies.size()) {
        // ---- Phase 1: load up to MAX_BATCH source values ----
        struct Entry { Value *phi; std::string tmpReg; int dstSlot; bool isFloat; };
        std::vector<Entry> entries;
        size_t end = std::min(idx + MAX_BATCH, copies.size());

        auto batchSourceReadsSlot = [&](int slot, size_t selfIdx) {
            for (size_t scan = idx; scan < end; ++scan) {
                if (scan == selfIdx) continue;
                Value *src = copies[scan].src;
                if (hasAssignedReg(src) || dynamic_cast<Constant*>(src) ||
                    dynamic_cast<GlobalVariable*>(src) || dynamic_cast<AllocaInst*>(src))
                    continue;
                if (getSlot(src) == slot)
                    return true;
            }
            return false;
        };

        auto batchSourceReadsReg = [&](const std::string &reg, size_t selfIdx) {
            std::string key = regKey(reg);
            for (size_t scan = idx; scan < end; ++scan) {
                if (scan == selfIdx) continue;
                Value *src = copies[scan].src;
                if (!hasAssignedReg(src)) continue;
                if (regKey(assignedReg(src, isPtr(src->type_))) == key)
                    return true;
            }
            return false;
        };

        for (; idx < end; ++idx) {
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
                            emitLoadMemMachine("q" + srcReg.substr(1),
                                               "[x29, #" + std::to_string(off) + "]",
                                               {"x29"}, MOpcode::Load, 4);
                        else {
                            int pos = -off;
                            if (pos <= 4095) {
                                emitRawAluMachine("\tsub x16, x29, #" + std::to_string(pos),
                                                  "x16", {"x29"});
                            } else {
                                emitIntConst(pos, "x16");
                                emitBinaryMachine("sub", "x16", "x29", "x16");
                            }
                            emitLoadMemMachine("q" + srcReg.substr(1), "[x16]",
                                               {"x16"}, MOpcode::Load, 4);
                        }
                    }
                    if (srcReg != dstReg)
                        emitRawAluMachine("\tmov " + dstReg + ".16b, " + srcReg + ".16b",
                                          dstReg, {srcReg}, MOpcode::Neon);
                }
                continue;
            }

            if (hasAssignedReg(val) && cp.phi && hasAssignedReg(cp.phi)) {
                bool asPtr = isPtr(val->type_);
                if (assignedReg(val, asPtr) == assignedReg(cp.phi, asPtr))
                    continue;
            }

            if (cp.phi && !hasAssignedReg(cp.phi) && hasAssignedReg(val) &&
                isPtr(val->type_) &&
                !isVector(val->type_) && !batchSourceReadsSlot(cp.dstSlot, idx)) {
                std::string srcReg = assignedReg(val, isPtr(val->type_));
                emitStoreRegMachine(srcReg, cp.dstSlot);
                continue;
            }

            if (cp.phi && hasAssignedReg(cp.phi) && !isFloat(val->type_) &&
                !isPtr(val->type_)) {
                if (auto *ci = dynamic_cast<ConstantInt*>(val)) {
                    std::string dstReg = assignedReg(cp.phi);
                    if (!batchSourceReadsReg(dstReg, idx)) {
                        if (ci->value_ == 0)
                            emitMoveMachine(dstReg, "wzr");
                        else
                            emitIntConst(ci->value_, dstReg);
                        continue;
                    }
                }
            }

            std::string tmpReg;
            if (isFloat(val->type_)) {
                if (hasAssignedReg(val)) {
                    std::string srcReg = assignedReg(val);
                    usedFloatRegs_.insert(std::stoi(srcReg.substr(1)));
                    tmpReg = allocFloatReg();
                    if (tmpReg != srcReg) emitMoveMachine(tmpReg, srcReg, "fmov");
                } else if (auto *cf = dynamic_cast<ConstantFloat*>(val)) {
                    tmpReg = allocFloatReg();
                    emitFloatConst(cf->value_, tmpReg);
                } else {
                    tmpReg = allocFloatReg();
                    emitLoadRegMachine(tmpReg, getSlot(val));
                }
            } else if (isPtr(val->type_)) {
                if (hasAssignedReg(val)) {
                    std::string srcReg = assignedReg(val, true);
                    usedIntRegs_.insert(std::stoi(srcReg.substr(1)));
                    tmpReg = allocAddrReg();
                    if (tmpReg != srcReg) emitMoveMachine(tmpReg, srcReg);
                } else if (auto *gv = dynamic_cast<GlobalVariable*>(val)) {
                    tmpReg = allocAddrReg();
                    emitGlobalAddr(gv, tmpReg);
                } else {
                    tmpReg = allocAddrReg();
                    emitLoadRegMachine(tmpReg, getSlot(val));
                }
            } else { // int
                if (hasAssignedReg(val)) {
                    std::string srcReg = assignedReg(val);
                    usedIntRegs_.insert(std::stoi(srcReg.substr(1)));
                    tmpReg = allocIntReg();
                    if (tmpReg != srcReg) emitMoveMachine(tmpReg, srcReg);
                } else if (auto *ci = dynamic_cast<ConstantInt*>(val)) {
                    tmpReg = allocIntReg();
                    emitIntConst(ci->value_, tmpReg);
                } else {
                    tmpReg = allocIntReg();
                    emitLoadRegMachine(tmpReg, getSlot(val));
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
                    if (e.tmpReg != dstReg) emitMoveMachine(dstReg, e.tmpReg, "fmov");
                } else {
                    if (e.tmpReg != dstReg) emitMoveMachine(dstReg, e.tmpReg);
                }
            } else {
                emitStoreRegMachine(e.tmpReg, e.dstSlot);
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

    // Pattern B hazard: falsePhiVal flows from the cond block, not from an
    // emitted block.  Emitting trueInst below can clobber the register holding
    // falsePhiVal — e.g. trueInst = `add falsePhiVal, x` where regalloc gave
    // trueInst's dest the same register as falsePhiVal.  That is legal in the
    // original CFG (falsePhiVal is dead after the add on the true path), but
    // the csel still needs falsePhiVal for the false arm.  Without this save
    // the csel degenerates to `csel R, R, R` (both inputs = trueInst's value).
    // Save falsePhiVal into a fresh register before trueInst runs.
    std::string falseSaveReg;
    if (!falseInst && hasAssignedReg(trueInst) && hasAssignedReg(falsePhiVal) &&
        assignedReg(trueInst) == assignedReg(falsePhiVal)) {
        falseSaveReg = allocIntReg();
        emitMoveMachine(falseSaveReg, assignedReg(falsePhiVal));
    }

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
        emitMoveMachine(trueSaveReg, trueAssigned);
    }
    if (falseInst)
        emitInstruction(falseInst);

    // Emit cmp to set flags for csel.
    auto *v1 = icmp->get_operand(0);
    auto *v2 = icmp->get_operand(1);
    std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
    const char *cond = icmpCond(icmp->icmp_op_);

    auto emitCmp = [&]() {
        if (auto *ci = dynamic_cast<ConstantInt*>(v2)) {
            int val = ci->value_;
            if (val >= 0 && val <= 4095)
                emitMachineInstrLine("\tcmp " + r1 + ", #" + std::to_string(val),
                                     MOpcode::Cmp, {}, {r1}, 1, true, false, true);
            else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                     MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true); }
        } else {
            std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
            emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                 MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
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
    std::string falseReg = !falseSaveReg.empty()       ? falseSaveReg
                         : hasAssignedReg(falsePhiVal) ? assignedReg(falsePhiVal)
                         : loadInt(falsePhiVal);
    emitMachineInstrLine("\tcsel " + dstReg + ", " + trueReg + ", " + falseReg + ", " + cond,
                         MOpcode::FlagUse, {dstReg}, {trueReg, falseReg}, 1,
                         false, true, true);
    if (!hasAssignedReg(phi)) storeInt(phi, dstReg);

    // Mark this phi copy as handled — don't emit it again
    cselHandled_.insert({br->parent_, phi});

    // Emit other phi copies and the branch
    emitPhiCopies(br->parent_, mergeBB);
    emitBranchMachine("\tb " + bbLabel(func_, mergeBB));

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
        if (auto *ci = dynamic_cast<ConstantInt*>(v2)) {
            int val = ci->value_;
            if (val >= 0 && val <= 4095)
                emitMachineInstrLine("\tcmp " + r1 + ", #" + std::to_string(val),
                                     MOpcode::Cmp, {}, {r1}, 1, true, false, true);
            else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                     MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true); }
        } else { std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
            emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                 MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true); }
    };

    // cmp for OUTER icmp (sets initial flags)
    emitCmp(icmp1);

    // ccmp for INNER icmp, conditioned on outer icmp's flags
    {
        auto *v1 = icmp2->get_operand(0);
        auto *v2 = icmp2->get_operand(1);
        std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
        const char *outerCond = icmpCond(icmp1->icmp_op_);
        if (auto *ci = dynamic_cast<ConstantInt*>(v2)) {
            int val = ci->value_;
            emitMachineInstrLine("\tccmp " + r1 + ", #" + std::to_string(val) + ", #0, " + outerCond,
                                 MOpcode::FlagUse, {}, {r1}, 1, true, true, true);
        } else { std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
            emitMachineInstrLine("\tccmp " + r1 + ", " + r2 + ", #0, " + outerCond,
                                 MOpcode::FlagUse, {}, {r1, r2}, 1, true, true, true); }
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
        emitMachineInstrLine("\tcmp " + io1_r1 + ", #" + std::to_string(io1_imm),
                             MOpcode::Cmp, {}, {io1_r1}, 1, true, false, true);
    else
        emitMachineInstrLine("\tcmp " + io1_r1 + ", " + io1_r2,
                             MOpcode::Cmp, {}, {io1_r1, io1_r2}, 1, true, false, true);

    {
        const char *outerCond = icmpCond(icmp1->icmp_op_);
        if (io2_isImm)
            emitMachineInstrLine("\tccmp " + io2_r1 + ", #" + std::to_string(io2_imm) + ", #0, " + outerCond,
                                 MOpcode::FlagUse, {}, {io2_r1}, 1, true, true, true);
        else
            emitMachineInstrLine("\tccmp " + io2_r1 + ", " + io2_r2 + ", #0, " + outerCond,
                                 MOpcode::FlagUse, {}, {io2_r1, io2_r2}, 1, true, true, true);
    }

    const char *outerCond = icmpCond(icmp1->icmp_op_);
    std::string dstReg   = hasAssignedReg(phi)       ? assignedReg(phi)       : allocIntReg();
    std::string trueReg  = hasAssignedReg(trueInst)  ? assignedReg(trueInst)  : loadInt(trueInst);
    std::string falseReg = hasAssignedReg(falseVal)  ? assignedReg(falseVal)  : loadInt(falseVal);
    emitMachineInstrLine("\tcsel " + dstReg + ", " + trueReg + ", " + falseReg + ", " + outerCond,
                         MOpcode::FlagUse, {dstReg}, {trueReg, falseReg}, 1,
                         false, true, true);
    if (!hasAssignedReg(phi)) storeInt(phi, dstReg);

    cselHandled_.insert({bb, phi});
    emitPhiCopies(bb, mergeBB);
    emitBranchMachine("\tb " + bbLabel(func_, mergeBB));
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
    if (auto *ci = dynamic_cast<ConstantInt*>(v2)) {
        if (ci->value_ == 0 && (strcmp(cond, "eq") == 0 || strcmp(cond, "ne") == 0)) {
            bool useCbz = (strcmp(cond, "eq") == 0);
            std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
            const char *op = useCbz ? "cbz" : "cbnz";

            if (!hasTrue && !hasFalse) {
                emitBranchMachine("\t" + std::string(op) + " " + r1 + ", " + bbLabel(func_, trueBB), {r1});
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            } else if (hasTrue && !hasFalse) {
                // Invert: if eq→cbz goes to trueBB, then ne case goes to falseBB via cbnz
                const char *invOp = useCbz ? "cbnz" : "cbz";
                emitBranchMachine("\t" + std::string(invOp) + " " + r1 + ", " + bbLabel(func_, falseBB), {r1});
                emitPhiCopies(parentBB, trueBB);
                emitBranchMachine("\tb " + bbLabel(func_, trueBB));
            } else if (!hasTrue && hasFalse) {
                emitBranchMachine("\t" + std::string(op) + " " + r1 + ", " + bbLabel(func_, trueBB), {r1});
                emitPhiCopies(parentBB, falseBB);
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            } else {
                // Both targets have phi copies — count and put trampoline on the path
                // with fewer copies.
                int trueCopies = 0, falseCopies = 0;
                for (const auto &pc : phiCopies_) {
                    if (pc.pred != parentBB) continue;
                    if (pc.succ == trueBB)  trueCopies++;
                    if (pc.succ == falseBB) falseCopies++;
                }
                bool trampolineOnTrue = (trueCopies <= falseCopies);
                std::string edgeLbl = ".L" + func_->name_ + "_edge_" + std::to_string(edgeCounter_++);
                if (trampolineOnTrue) {
                    emitBranchMachine("\t" + std::string(op) + " " + r1 + ", " + edgeLbl, {r1});
                    emitPhiCopies(parentBB, falseBB);
                    emitBranchMachine("\tb " + bbLabel(func_, falseBB));
                    emitMachineLine(edgeLbl + ":");
                    emitPhiCopies(parentBB, trueBB);
                    emitBranchMachine("\tb " + bbLabel(func_, trueBB));
                } else {
                    const char *invOp = useCbz ? "cbnz" : "cbz";
                    emitBranchMachine("\t" + std::string(invOp) + " " + r1 + ", " + edgeLbl, {r1});
                    emitPhiCopies(parentBB, trueBB);
                    emitBranchMachine("\tb " + bbLabel(func_, trueBB));
                    emitMachineLine(edgeLbl + ":");
                    emitPhiCopies(parentBB, falseBB);
                    emitBranchMachine("\tb " + bbLabel(func_, falseBB));
                }
            }
            return;
        }
    }

    std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);

    // Emit cmp with immediate if possible (ARM64 cmp imm is 12-bit: 0-4095)
    if (auto *ci = dynamic_cast<ConstantInt*>(v2)) {
        int val = ci->value_;
        if (val >= 0 && val <= 4095) {
            emitMachineInstrLine("\tcmp " + r1 + ", #" + std::to_string(val),
                                 MOpcode::Cmp, {}, {r1}, 1, true, false, true);
        } else {
            std::string r2 = intConstReg(val);
            emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                 MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        }
    } else {
        std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
        emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                             MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
    }

    if (!hasTrue && !hasFalse) {
        emitBranchMachine("\tb." + std::string(cond) + " " + bbLabel(func_, trueBB), {}, true);
        emitBranchMachine("\tb " + bbLabel(func_, falseBB));
    } else if (hasTrue && !hasFalse) {
        emitBranchMachine("\tb." + std::string(invertCond(cond)) + " " + bbLabel(func_, falseBB), {}, true);
        emitPhiCopies(parentBB, trueBB);
        emitBranchMachine("\tb " + bbLabel(func_, trueBB));
    } else if (!hasTrue && hasFalse) {
        emitBranchMachine("\tb." + std::string(cond) + " " + bbLabel(func_, trueBB), {}, true);
        emitPhiCopies(parentBB, falseBB);
        emitBranchMachine("\tb " + bbLabel(func_, falseBB));
    } else {
        // Both targets have phi copies — put the trampoline on the path
        // with fewer copies so the hotter path stays as fallthrough.
        int trueCopies = 0, falseCopies = 0;
        for (const auto &pc : phiCopies_) {
            if (pc.pred != parentBB) continue;
            if (pc.succ == trueBB)  trueCopies++;
            if (pc.succ == falseBB) falseCopies++;
        }
        bool trampolineOnTrue = (trueCopies <= falseCopies);
        std::string edgeLbl = ".L" + func_->name_ + "_edge_" + std::to_string(edgeCounter_++);
        if (trampolineOnTrue) {
            emitBranchMachine("\tb." + std::string(cond) + " " + edgeLbl, {}, true);
            emitPhiCopies(parentBB, falseBB);
            emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            emitMachineLine(edgeLbl + ":");
            emitPhiCopies(parentBB, trueBB);
            emitBranchMachine("\tb " + bbLabel(func_, trueBB));
        } else {
            emitBranchMachine("\tb." + std::string(invertCond(cond)) + " " + edgeLbl, {}, true);
            emitPhiCopies(parentBB, trueBB);
            emitBranchMachine("\tb " + bbLabel(func_, trueBB));
            emitMachineLine(edgeLbl + ":");
            emitPhiCopies(parentBB, falseBB);
            emitBranchMachine("\tb " + bbLabel(func_, falseBB));
        }
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
    case ICmpInst::ICMP_UGT: return "hi";
    case ICmpInst::ICMP_UGE: return "hs";
    case ICmpInst::ICMP_ULT: return "lo";
    case ICmpInst::ICMP_ULE: return "ls";
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
