#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/regalloc.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

// ---- Arm64FuncContext ----

Arm64FuncContext::Arm64FuncContext(Function *f, MachineEmitter &emitter, bool enableRegAlloc)
    : func_(f), machineEmitter_(emitter), enableRegAlloc_(enableRegAlloc) {}

void Arm64FuncContext::emitMachineLine(const std::string &line) {
    machineEmitter_.emitLine(line);
}

void Arm64FuncContext::emitMachineInstr(MachineInstr inst) {
    machineEmitter_.emit(std::move(inst));
}

void Arm64FuncContext::emitMachineInstrLine(const std::string &line, MOpcode opcode,
                                            std::initializer_list<std::string> defs,
                                            std::initializer_list<std::string> uses,
                                            int latency,
                                            bool setsFlags,
                                            bool usesFlags,
                                            bool isBarrier) {
    MachineInstr inst = MachineInstr::make(line, opcode, defs, uses, latency);
    inst.setsFlags = setsFlags;
    inst.usesFlags = usesFlags;
    inst.isBarrier = isBarrier && opcode != MOpcode::Cmp && opcode != MOpcode::FlagUse;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitMoveMachine(const std::string &dst, const std::string &src,
                                       const std::string &opcode) {
    emitMachineInstrLine("\t" + opcode + " " + dst + ", " + src,
                         MOpcode::Mov, {dst}, {src});
}

void Arm64FuncContext::emitUnaryMachine(const std::string &opcode, const std::string &dst,
                                        const std::string &src, MOpcode mop,
                                        int latency) {
    emitMachineInstrLine("\t" + opcode + " " + dst + ", " + src,
                         mop, {dst}, {src}, latency);
}

void Arm64FuncContext::emitBinaryMachine(const std::string &opcode, const std::string &dst,
                                         const std::string &lhs, const std::string &rhs,
                                         MOpcode mop, int latency) {
    emitMachineInstrLine("\t" + opcode + " " + dst + ", " + lhs + ", " + rhs,
                         mop, {dst}, {lhs, rhs}, latency);
}

void Arm64FuncContext::emitRawAluMachine(const std::string &line, const std::string &dst,
                                         std::initializer_list<std::string> uses,
                                         MOpcode mop, int latency) {
    emitMachineInstrLine(line, mop, {dst}, uses, latency);
}

void Arm64FuncContext::emitBranchMachine(const std::string &line,
                                         std::initializer_list<std::string> uses,
                                         bool usesFlags) {
    emitMachineInstrLine(line, MOpcode::Branch, {}, uses, 1,
                         false, usesFlags, true);
}

void Arm64FuncContext::emitCallMachine(const std::string &callee) {
    MachineInstr inst = MachineInstr::make("\tbl " + callee, MOpcode::Call);
    inst.isCall = true;
    inst.isBarrier = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitRetMachine() {
    emitMachineInstrLine("\tret", MOpcode::Ret, {}, {}, 1,
                         false, false, true);
}

void Arm64FuncContext::emitStackAdjustMachine(const std::string &opcode, int bytes) {
    emitMachineInstrLine("\t" + opcode + " sp, sp, #" + std::to_string(bytes),
                         MOpcode::Alu, {"sp"}, {"sp"}, 1,
                         false, false, true);
}

void Arm64FuncContext::emitStackAdjustMachine(const std::string &opcode, const std::string &reg) {
    emitMachineInstrLine("\t" + opcode + " sp, sp, " + reg,
                         MOpcode::Alu, {"sp"}, {"sp", reg}, 1,
                         false, false, true);
}

void Arm64FuncContext::emitFramePushMachine() {
    MachineInstr inst = MachineInstr::make("\tstp x29, x30, [sp, #-16]!",
                                           MOpcode::PairStore, {"sp"}, {"x29", "x30", "sp"});
    inst.mayStore = true;
    inst.isBarrier = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::emitFramePopMachine() {
    MachineInstr inst = MachineInstr::make("\tldp x29, x30, [sp], #16",
                                           MOpcode::PairLoad, {"x29", "x30", "sp"}, {"sp"}, 4);
    inst.mayLoad = true;
    inst.isBarrier = true;
    emitMachineInstr(std::move(inst));
}

void Arm64FuncContext::generate() {
    func_->set_instr_name();

    // find epilogue BB (the one with ret)
    for (auto bb : func_->basic_blocks_) {
        auto term = bb->get_terminator();
        if (term && term->is_ret()) {
            epilogueBB_ = bb;
            break;
        }
    }

    if (enableRegAlloc_) {
        Arm64RegAlloc regAlloc(func_);
        regAlloc.allocate();
        assignedRegs_ = regAlloc.assignedRegs();
    } else {
        assignedRegs_.clear();
    }
    reservedIntRegs_.clear();
    reservedFloatRegs_.clear();
    reservedNEONRegs_.clear();
    for (const auto &kv : assignedRegs_) {
        const std::string &reg = kv.second;
        if (reg.size() < 2) continue;
        int regNo = std::stoi(reg.substr(1));
        if (reg[0] == 'w' || reg[0] == 'x')
            reservedIntRegs_.insert(regNo);
        else if (reg[0] == 's' || reg[0] == 'd')
            reservedFloatRegs_.insert(regNo);
        else if (reg[0] == 'v')
            reservedNEONRegs_.insert(regNo);
    }

    preparePhi();
    blockSkipped_.clear();
    cselHandled_.clear();

    emitPrologue();
    reorderBlocks();
    for (auto bb : func_->basic_blocks_) {
        emitBlock(bb);
    }
    emitEpilogue();
}

void Arm64FuncContext::reorderBlocks() {
    // Determine preferred fallthrough successor for each block.
    // For conditional branches the generated code is:
    //   b.cond true_label  (or cbz/cbnz)
    //   [optional phi copies]
    //   b fallthrough_target
    // If fallthrough_target is placed immediately after this block, the 'b'
    // instruction becomes redundant.
    std::map<BasicBlock*, BasicBlock*> preferred;

    for (auto bb : func_->basic_blocks_) {
        auto term = bb->get_terminator();
        if (!term || !term->is_br()) continue;

        if (term->num_ops_ == 1) {
            // Unconditional branch: prefer the single target
            preferred[bb] = static_cast<BasicBlock*>(term->get_operand(0));
        } else if (term->num_ops_ == 3) {
            auto trueBB  = static_cast<BasicBlock*>(term->get_operand(1));
            auto falseBB = static_cast<BasicBlock*>(term->get_operand(2));

            // Check which edges carry phi copies. The codegen (emitFusedCmpBranch /
            // emitInstruction::Br) emits the unconditional 'b' to:
            //   - trueBB  when hasTrue && !hasFalse
            //   - falseBB otherwise
            bool hasTrue = false, hasFalse = false;
            for (const auto &pc : phiCopies_) {
                if (pc.pred != bb) continue;
                if (pc.succ == trueBB)  hasTrue  = true;
                if (pc.succ == falseBB) hasFalse = true;
            }
            preferred[bb] = (hasTrue && !hasFalse) ? trueBB : falseBB;
        }
    }

    // Greedy chain layout: start from the entry block (always first) and
    // repeatedly place the preferred fallthrough successor, falling back
    // to any unplaced block when the chain ends.
    std::vector<BasicBlock*> order;
    std::set<BasicBlock*> placed;

    BasicBlock *entry = func_->basic_blocks_[0];
    BasicBlock *current = entry;
    order.push_back(current);
    placed.insert(current);

    while (order.size() < func_->basic_blocks_.size()) {
        // Try the preferred fallthrough successor first
        auto it = preferred.find(current);
        if (it != preferred.end() && !placed.count(it->second)) {
            current = it->second;
        } else {
            // Then try any unplaced successor
            BasicBlock *next = nullptr;
            auto term = current->get_terminator();
            if (term && term->is_br()) {
                for (unsigned i = 0; i < term->num_ops_; ++i) {
                    // operand 0 of cond br is i1 (not a BB), skipped by dynamic_cast
                    if (auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                        if (!placed.count(succ)) { next = succ; break; }
                    }
                }
            }
            if (next) {
                current = next;
            } else {
                // Fall back: pick the first unplaced block in original order
                for (auto bb : func_->basic_blocks_) {
                    if (!placed.count(bb)) { current = bb; break; }
                }
            }
        }
        order.push_back(current);
        placed.insert(current);
    }

    func_->basic_blocks_ = order;
}

void Arm64FuncContext::emitPrologue() {
    emitMachineLine("\t.global " + func_->name_);
    emitMachineLine("\t.p2align 2");
    emitMachineLine(func_->name_ + ":");

    // Allocate slots for arguments that actually need them.
    // Pre-colored args (leaf functions, assigned to their incoming register)
    // stay in w0-w7/s0-s7 and never need a stack slot.
    {
        int intArgIdx = 0, floatArgIdx = 0;
        for (auto arg : func_->arguments_) {
            if (isFloat(arg->type_)) {
                if (floatArgIdx < 8) {
                    std::string src = "s" + std::to_string(floatArgIdx++);
                    if (hasAssignedReg(arg) && assignedReg(arg) == src)
                        continue; // pre-colored — no slot needed
                }
            } else {
                if (intArgIdx < 8) {
                    bool isPtr = (arg->type_->tid_ == Type::PointerTyID ||
                                arg->type_->tid_ == Type::ArrayTyID);
                    std::string reg = (isPtr ? "x" : "w") + std::to_string(intArgIdx++);
                    if (hasAssignedReg(arg)) {
                        std::string dst = assignedReg(arg, isPtr);
                        if (dst == reg) continue; // pre-colored — no slot needed
                    }
                }
            }
            getSlot(arg);
        }
    }

    // pre-scan all instructions to allocate slots
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_alloca()) {
                getSlot(inst);
            } else if (inst->type_->tid_ != Type::VoidTyID &&
                       !dynamic_cast<Constant*>(inst) &&
                       !inst->is_store() && !inst->is_br() && !inst->is_ret() &&
                       !hasAssignedReg(inst)) {
                // Skip ICmp whose only user is a Select —
                // the Select emits its own cmp, the ICmp is never stored.
                if (inst->op_id_ == Instruction::ICmp && inst->use_list_.size() == 1) {
                    auto *user = dynamic_cast<SelectInst*>((*inst->use_list_.begin()).val_);
                    if (user) continue;
                }
                // Skip Select whose only user is a Ret —
                // the csel writes directly to w0, no stack slot needed.
                if (inst->op_id_ == Instruction::Select &&
                    inst->use_list_.size() == 1 &&
                    !isFloat(inst->type_)) {
                    auto *user = dynamic_cast<ReturnInst*>((*inst->use_list_.begin()).val_);
                    if (user) continue;
                }
                getSlot(inst);
            }
        }
    }

    // Snapshot frameSize_ after pre-scan but before emitBlock.
    // Pattern A in LoopVectorize creates new InsertElementInst during
    // emitBlock which call getSlot() and would increase frameSize_,
    // causing the epilogue to compute a different frame layout.
    prologueFrameSize_ = frameSize_;

    auto savedIntRegs = collectAssignedIntRegs(assignedRegs_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    auto savedNEONRegs = collectAssignedNEONRegs(assignedRegs_);
    // NEON regs are 16 bytes each → 2 × 8-byte slots
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size() + savedNEONRegs.size() * 2) * 8;
    int localSize = align16(prologueFrameSize_ + savedRegBytes);
    int saveOffset = -prologueFrameSize_;

    // Determine whether the function has any call instructions.
    bool hasCalls = false;
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_call()) { hasCalls = true; break; }
        }
        if (hasCalls) break;
    }
    needsFrame_ = (localSize > 0) || hasCalls;

    if (needsFrame_) {
        // stp supports only -512..504 range; use minimal stp + sub for large frames
        emitFramePushMachine();
        emitMoveMachine("x29", "sp");
        if (localSize <= 4095) {
            emitStackAdjustMachine("sub", localSize);
        } else {
            emitIntConst(localSize, "x17");
            emitStackAdjustMachine("sub", "x17");
        }
    }

    for (size_t i = 0; i < savedIntRegs.size(); i += 2) {
        if (i + 1 < savedIntRegs.size()) {
            saveOffset -= 16;
            emitStorePairMachine("x" + std::to_string(savedIntRegs[i+1]),
                                 "x" + std::to_string(savedIntRegs[i]), saveOffset);
        } else {
            saveOffset -= 8;
            emitStoreRegMachine("x" + std::to_string(savedIntRegs[i]), saveOffset);
        }
    }
    for (size_t i = 0; i < savedFloatRegs.size(); i += 2) {
        if (i + 1 < savedFloatRegs.size()) {
            saveOffset -= 16;
            emitStorePairMachine("d" + std::to_string(savedFloatRegs[i+1]),
                                 "d" + std::to_string(savedFloatRegs[i]), saveOffset);
        } else {
            saveOffset -= 8;
            emitStoreRegMachine("d" + std::to_string(savedFloatRegs[i]), saveOffset);
        }
    }
    // Save callee-saved NEON registers (v8-v15), 16 bytes each.
    // str qN only supports unsigned offset; use sub+str for negative offsets.
    for (size_t i = 0; i < savedNEONRegs.size(); i++) {
        saveOffset -= 16;
        int r = savedNEONRegs[i];
        if (saveOffset >= 0) {
            emitStoreMemMachine("q" + std::to_string(r),
                                "[x29, #" + std::to_string(saveOffset) + "]",
                                {"q" + std::to_string(r), "x29"}, MOpcode::Store);
        } else {
            int absOff = -saveOffset;
            if (absOff <= 4095)
                emitRawAluMachine("\tsub x16, x29, #" + std::to_string(absOff),
                                  "x16", {"x29"});
            else {
                emitIntConst(absOff, "x16");
                emitBinaryMachine("sub", "x16", "x29", "x16");
            }
            emitStoreMemMachine("q" + std::to_string(r), "[x16]",
                                {"q" + std::to_string(r), "x16"}, MOpcode::Store);
        }
    }

   // ----- load arguments (including register arguments and stack arguments) -----
   int intRegIdx = 0;     // x0-x7 / w0-w7
   int floatRegIdx = 0;   // s0-s7
   int stackOffset = 0;   // stack argument offset (relative to x29+16)

   for (auto arg : func_->arguments_) {
        if (isFloat(arg->type_)) {
            if (floatRegIdx < 8) {
                std::string src = "s" + std::to_string(floatRegIdx++);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg);
                    if (dst == src) {
                        // Pre-colored to incoming register — no spill needed
                    } else {
                        int slot = getSlot(arg);
                        emitStoreRegMachine(src, slot);
                        emitMoveMachine(dst, src, "fmov");
                    }
                } else {
                    int slot = getSlot(arg);
                    emitStoreRegMachine(src, slot);
                }
            } else {
                int off = 16 + stackOffset;
                if (off <= 4095) {
                    emitLoadMemMachine("s17", "[x29, #" + std::to_string(off) + "]", {"x29"});
                } else {
                    emitIntConst(off, "x17");
                    emitLoadMemMachine("s17", "[x29, x17]", {"x29", "x17"});
                }
                int slot = getSlot(arg);
                emitStoreRegMachine("s17", slot);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg);
                    emitMoveMachine(dst, "s17", "fmov");
                }
                stackOffset += 8;
            }
        } else {
            // int or ptr
            if (intRegIdx < 8) {
                bool isPtr = (arg->type_->tid_ == Type::PointerTyID ||
                            arg->type_->tid_ == Type::ArrayTyID);
                std::string reg = isPtr ? "x" : "w";
                reg += std::to_string(intRegIdx++);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg, isPtr);
                    if (dst == reg) {
                        // Pre-colored to incoming register — no spill needed
                    } else {
                        int slot = getSlot(arg);
                        emitStoreRegMachine(reg, slot);
                        emitMoveMachine(dst, reg);
                    }
                } else {
                    int slot = getSlot(arg);
                    emitStoreRegMachine(reg, slot);
                }
            } else {
                int off = 16 + stackOffset;
                if (off <= 4095) {
                    emitLoadMemMachine("x17", "[x29, #" + std::to_string(off) + "]", {"x29"});
                } else {
                    emitIntConst(off, "x17");
                    emitLoadMemMachine("x17", "[x29, x17]", {"x29", "x17"});
                }
                int slot = getSlot(arg);
                emitStoreRegMachine("x17", slot);
                if (hasAssignedReg(arg)) {
                    bool isPtr = (arg->type_->tid_ == Type::PointerTyID ||
                                arg->type_->tid_ == Type::ArrayTyID);
                    std::string dst = assignedReg(arg, isPtr);
                    if (isPtr) {
                        if (dst != "x17") emitMoveMachine(dst, "x17");
                    } else {
                        // dst 形如 "w24"，从 x17 取出低 32 位
                        emitMoveMachine(dst, "w17");
                    }
                }
                stackOffset += 8;
            }
        }
    }
}

void Arm64FuncContext::emitEpilogue() {
    if (!epilogueBB_) return;

    // When no frame is needed, Ret emits 'ret' directly — no epilogue at all.
    if (!needsFrame_) return;

    emitMachineLine(".L" + func_->name_ + "_epilogue:");

    auto savedIntRegs = collectAssignedIntRegs(assignedRegs_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    auto savedNEONRegs = collectAssignedNEONRegs(assignedRegs_);
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size() + savedNEONRegs.size() * 2) * 8;
    int localSize = align16(prologueFrameSize_ + savedRegBytes);
    int restoreOffset = -prologueFrameSize_;

    for (size_t i = 0; i < savedIntRegs.size(); i += 2) {
        if (i + 1 < savedIntRegs.size()) {
            restoreOffset -= 16;
            emitLoadPairMachine("x" + std::to_string(savedIntRegs[i+1]),
                                "x" + std::to_string(savedIntRegs[i]), restoreOffset);
        } else {
            restoreOffset -= 8;
            emitLoadRegMachine("x" + std::to_string(savedIntRegs[i]), restoreOffset);
        }
    }
    for (size_t i = 0; i < savedFloatRegs.size(); i += 2) {
        if (i + 1 < savedFloatRegs.size()) {
            restoreOffset -= 16;
            emitLoadPairMachine("d" + std::to_string(savedFloatRegs[i+1]),
                                "d" + std::to_string(savedFloatRegs[i]), restoreOffset);
        } else {
            restoreOffset -= 8;
            emitLoadRegMachine("d" + std::to_string(savedFloatRegs[i]), restoreOffset);
        }
    }
    // Restore callee-saved NEON registers (v8-v15).
    // ldr qN only supports unsigned offset; use sub+ldr for negative offsets.
    for (size_t i = 0; i < savedNEONRegs.size(); i++) {
        restoreOffset -= 16;
        int r = savedNEONRegs[i];
        if (restoreOffset >= 0) {
            emitLoadMemMachine("q" + std::to_string(r),
                               "[x29, #" + std::to_string(restoreOffset) + "]",
                               {"x29"}, MOpcode::Load, 4);
        } else {
            int absOff = -restoreOffset;
            if (absOff <= 4095)
                emitRawAluMachine("\tsub x16, x29, #" + std::to_string(absOff),
                                  "x16", {"x29"});
            else {
                emitIntConst(absOff, "x16");
                emitBinaryMachine("sub", "x16", "x29", "x16");
            }
            emitLoadMemMachine("q" + std::to_string(r), "[x16]",
                               {"x16"}, MOpcode::Load, 4);
        }
    }

    if (localSize > 0) {
        if (localSize <= 4095) {
            emitStackAdjustMachine("add", localSize);
        } else {
            emitIntConst(localSize, "x17");
            emitStackAdjustMachine("add", "x17");
        }
    }
    emitFramePopMachine();
    emitRetMachine();
}


// ---- graph-coloring register allocation result accessors ----

bool Arm64FuncContext::hasAssignedReg(Value *v) const {
    return assignedRegs_.count(v) > 0;
}

std::string Arm64FuncContext::assignedReg(Value *v, bool asAddress) const {
    auto it = assignedRegs_.find(v);
    if (it == assignedRegs_.end()) return "";
    if (asAddress && !it->second.empty() && it->second[0] == 'w') {
        return "x" + it->second.substr(1);
    }
    return it->second;
}


// ---- slot management ----

int Arm64FuncContext::getSlot(Value *v) {
    auto it = slots_.find(v);
    if (it != slots_.end()) return it->second;

    int size;
    if (auto alloca = dynamic_cast<AllocaInst*>(v)) {
        size = typeSize(alloca->alloca_ty_);
    } else if (isVector(v->type_)) {
        size = 16; // 128-bit vector value
    } else {
        size = 8; // keep SSA/temp slots naturally aligned
    }
    // Align to slot size to prevent any overlap
    frameSize_ = align16(frameSize_);

    frameSize_ += size;
    int offset = -frameSize_;
    slots_[v] = offset;
    return offset;
}

bool Arm64FuncContext::hasSlot(Value *v) const {
    return slots_.count(v) > 0;
}
