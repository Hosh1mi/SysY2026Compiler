#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/regalloc.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>

// ---- Arm64FuncContext ----

Arm64FuncContext::Arm64FuncContext(Function *f, std::ostream &os)
    : func_(f), os_(os) {}

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

    Arm64RegAlloc regAlloc(func_);
    regAlloc.allocate();
    assignedRegs_ = regAlloc.assignedRegs();
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
    os_ << "\t.global " << func_->name_ << "\n";
    os_ << "\t.p2align 2\n";
    os_ << func_->name_ << ":\n";

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
        os_ << "\tstp x29, x30, [sp, #-16]!\n";
        os_ << "\tmov x29, sp\n";
        if (localSize <= 4095) {
            os_ << "\tsub sp, sp, #" << localSize << "\n";
        } else {
            os_ << "\tmovz x17, #" << (localSize & 0xFFFF) << "\n";
            os_ << "\tmovk x17, #" << ((localSize >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tsub sp, sp, x17\n";
        }
    }

    for (size_t i = 0; i < savedIntRegs.size(); i += 2) {
        if (i + 1 < savedIntRegs.size()) {
            saveOffset -= 16;
            emitStorePair(os_, "x" + std::to_string(savedIntRegs[i+1]),
                          "x" + std::to_string(savedIntRegs[i]), saveOffset);
        } else {
            saveOffset -= 8;
            emitStoreReg(os_, "x" + std::to_string(savedIntRegs[i]), saveOffset);
        }
    }
    for (size_t i = 0; i < savedFloatRegs.size(); i += 2) {
        if (i + 1 < savedFloatRegs.size()) {
            saveOffset -= 16;
            emitStorePair(os_, "d" + std::to_string(savedFloatRegs[i+1]),
                          "d" + std::to_string(savedFloatRegs[i]), saveOffset);
        } else {
            saveOffset -= 8;
            emitStoreReg(os_, "d" + std::to_string(savedFloatRegs[i]), saveOffset);
        }
    }
    // Save callee-saved NEON registers (v8-v15), 16 bytes each.
    // str qN only supports unsigned offset; use sub+str for negative offsets.
    for (size_t i = 0; i < savedNEONRegs.size(); i++) {
        saveOffset -= 16;
        int r = savedNEONRegs[i];
        if (saveOffset >= 0) {
            os_ << "\tstr q" << r << ", [x29, #" << saveOffset << "]\n";
        } else {
            int absOff = -saveOffset;
            if (absOff <= 4095)
                os_ << "\tsub x16, x29, #" << absOff << "\n";
            else {
                os_ << "\tmovz x16, #" << (absOff & 0xFFFF) << "\n";
                os_ << "\tmovk x16, #" << ((absOff >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tsub x16, x29, x16\n";
            }
            os_ << "\tstr q" << r << ", [x16]\n";
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
                        emitStoreReg(os_, src, slot);
                        os_ << "\tfmov " << dst << ", " << src << "\n";
                    }
                } else {
                    int slot = getSlot(arg);
                    emitStoreReg(os_, src, slot);
                }
            } else {
                int off = 16 + stackOffset;
                if (off <= 4095) {
                    os_ << "\tldr s17, [x29, #" << off << "]\n";
                } else {
                    os_ << "\tmovz x17, #" << off << "\n";
                    os_ << "\tldr s17, [x29, x17]\n";
                }
                int slot = getSlot(arg);
                emitStoreReg(os_, "s17", slot);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg);
                    os_ << "\tfmov " << dst << ", s17\n";
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
                        emitStoreReg(os_, reg, slot);
                        os_ << "\tmov " << dst << ", " << reg << "\n";
                    }
                } else {
                    int slot = getSlot(arg);
                    emitStoreReg(os_, reg, slot);
                }
            } else {
                int off = 16 + stackOffset;
                if (off <= 4095) {
                    os_ << "\tldr x17, [x29, #" << off << "]\n";
                } else {
                    os_ << "\tmovz x17, #" << off << "\n";
                    os_ << "\tldr x17, [x29, x17]\n";
                }
                int slot = getSlot(arg);
                emitStoreReg(os_, "x17", slot);
                if (hasAssignedReg(arg)) {
                    bool isPtr = (arg->type_->tid_ == Type::PointerTyID ||
                                arg->type_->tid_ == Type::ArrayTyID);
                    std::string dst = assignedReg(arg, isPtr);
                    if (isPtr) {
                        if (dst != "x17") os_ << "\tmov " << dst << ", x17\n";
                    } else {
                        // dst 形如 "w24"，从 x17 取出低 32 位
                        os_ << "\tmov " << dst << ", w17\n";
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

    os_ << ".L" << func_->name_ << "_epilogue:\n";

    auto savedIntRegs = collectAssignedIntRegs(assignedRegs_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    auto savedNEONRegs = collectAssignedNEONRegs(assignedRegs_);
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size() + savedNEONRegs.size() * 2) * 8;
    int localSize = align16(prologueFrameSize_ + savedRegBytes);
    int restoreOffset = -prologueFrameSize_;

    for (size_t i = 0; i < savedIntRegs.size(); i += 2) {
        if (i + 1 < savedIntRegs.size()) {
            restoreOffset -= 16;
            emitLoadPair(os_, "x" + std::to_string(savedIntRegs[i+1]),
                         "x" + std::to_string(savedIntRegs[i]), restoreOffset);
        } else {
            restoreOffset -= 8;
            emitLoadReg(os_, "x" + std::to_string(savedIntRegs[i]), restoreOffset);
        }
    }
    for (size_t i = 0; i < savedFloatRegs.size(); i += 2) {
        if (i + 1 < savedFloatRegs.size()) {
            restoreOffset -= 16;
            emitLoadPair(os_, "d" + std::to_string(savedFloatRegs[i+1]),
                         "d" + std::to_string(savedFloatRegs[i]), restoreOffset);
        } else {
            restoreOffset -= 8;
            emitLoadReg(os_, "d" + std::to_string(savedFloatRegs[i]), restoreOffset);
        }
    }
    // Restore callee-saved NEON registers (v8-v15).
    // ldr qN only supports unsigned offset; use sub+ldr for negative offsets.
    for (size_t i = 0; i < savedNEONRegs.size(); i++) {
        restoreOffset -= 16;
        int r = savedNEONRegs[i];
        if (restoreOffset >= 0) {
            os_ << "\tldr q" << r << ", [x29, #" << restoreOffset << "]\n";
        } else {
            int absOff = -restoreOffset;
            if (absOff <= 4095)
                os_ << "\tsub x16, x29, #" << absOff << "\n";
            else {
                os_ << "\tmovz x16, #" << (absOff & 0xFFFF) << "\n";
                os_ << "\tmovk x16, #" << ((absOff >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tsub x16, x29, x16\n";
            }
            os_ << "\tldr q" << r << ", [x16]\n";
        }
    }

    if (localSize > 0) {
        if (localSize <= 4095) {
            os_ << "\tadd sp, sp, #" << localSize << "\n";
        } else {
            os_ << "\tmovz x17, #" << (localSize & 0xFFFF) << "\n";
            os_ << "\tmovk x17, #" << ((localSize >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tadd sp, sp, x17\n";
        }
    }
    os_ << "\tldp x29, x30, [sp], #16\n";
    os_ << "\tret\n";
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
