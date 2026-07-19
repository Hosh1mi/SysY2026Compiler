#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include "../../include/backend/arm64/regalloc.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <climits>
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
    if (dst == src)
        return;
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

void Arm64FuncContext::emitCallMachine(const std::string &callee, CallInst *call) {
    MachineInstr inst = MachineInstr::make("\tbl " + callee, MOpcode::Call);
    inst.isCall = true;
    inst.isBarrier = true;
    emitMachineInstr(std::move(inst));

    // Caller-saved promoted constants are intentionally not spilled around
    // calls.  Re-materialize only when a real use is reachable before the next
    // clobber; this keeps loop constants hoisted without emitting dead reloads
    // after calls that return directly or call again first.
    if (call)
        emitLivePromotedConstsFrom(call->parent_, call);
}

bool Arm64FuncContext::callClobbersPromotedConst(int val) const {
    auto it = promotedConsts_.find(val);
    if (it == promotedConsts_.end())
        return false;
    int regNo = std::stoi(it->second.substr(1));
    return regNo < 19 || regNo > 28;
}

bool Arm64FuncContext::instructionUsesPromotedConst(Instruction *inst, int val) const {
    if (!inst || inst->is_call())
        return false;

    auto isPromoted = [&]() {
        return promotedConsts_.count(val) > 0;
    };
    if (!isPromoted())
        return false;

    auto usesDirectConstant = [&]() {
        for (unsigned i = 0; i < inst->num_ops_; ++i) {
            auto *ci = dynamic_cast<ConstantInt *>(inst->get_operand(i));
            if (ci && ci->value_ == val)
                return true;
        }
        return false;
    };

    if (inst->op_id_ == Instruction::SRem) {
        auto *ci = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (ci) {
            Magic::SignedDivisorInfo info = Magic::analyzeDivisor(ci->value_);
            if (info.usesMagic()) {
                Magic::MagicNumber mag = Magic::getMagic(info.magnitude);
                if (val == mag.multiplier || val == static_cast<int>(info.magnitude))
                    return true;
            }
        }
    } else if (inst->op_id_ == Instruction::SDiv) {
        auto *ci = dynamic_cast<ConstantInt *>(inst->get_operand(1));
        if (ci) {
            Magic::SignedDivisorInfo info = Magic::analyzeDivisor(ci->value_);
            if (info.usesMagic()) {
                Magic::MagicNumber mag = Magic::getMagic(ci->value_);
                if (val == mag.multiplier)
                    return true;
            }
        }
    }

    return usesDirectConstant();
}

bool Arm64FuncContext::promotedConstReachableBeforeClobber(
    BasicBlock *bb, Instruction *after, int val, std::set<BasicBlock*> &visited) const {
    if (!bb)
        return false;

    auto it = bb->instr_list_.begin();
    if (after) {
        it = std::find(bb->instr_list_.begin(), bb->instr_list_.end(), after);
        if (it == bb->instr_list_.end())
            return false;
        ++it;
    } else if (!visited.insert(bb).second) {
        return false;
    }

    for (; it != bb->instr_list_.end(); ++it) {
        Instruction *inst = *it;
        if (inst->is_call() && callClobbersPromotedConst(val))
            return false;
        if (instructionUsesPromotedConst(inst, val))
            return true;
    }

    auto *term = bb->get_terminator();
    if (!term)
        return false;
    for (unsigned i = 0; i < term->num_ops_; ++i) {
        auto *succ = dynamic_cast<BasicBlock *>(term->get_operand(i));
        if (succ && promotedConstReachableBeforeClobber(succ, nullptr, val, visited))
            return true;
    }
    return false;
}

void Arm64FuncContext::emitLivePromotedConstsFrom(BasicBlock *bb, Instruction *after) {
    for (const auto &kv : promotedConsts_) {
        if (after && !callClobbersPromotedConst(kv.first))
            continue;
        std::set<BasicBlock*> visited;
        if (promotedConstReachableBeforeClobber(bb, after, kv.first, visited))
            emitIntConst(kv.first, kv.second);
    }
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
        promotedConsts_ = regAlloc.promotedConsts();
    } else {
        assignedRegs_.clear();
        promotedConsts_.clear();
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
    for (const auto &kv : promotedConsts_)
        reservedIntRegs_.insert(std::stoi(kv.second.substr(1)));
    prepareGlobalPageBases();
    if (!globalPageBaseForBlock_.empty())
        reservedIntRegs_.insert(8);

    preparePhi();
    blockSkipped_.clear();
    cselHandled_.clear();
    fixedValueRegs_.clear();

    emitPrologue();
    if (!func_->basic_blocks_.empty())
        emitLivePromotedConstsFrom(func_->basic_blocks_[0], nullptr);
    reorderBlocks();
    for (auto bb : func_->basic_blocks_) {
        auto cached = globalPageBaseForBlock_.find(bb);
        currentGlobalPageBase_ = cached == globalPageBaseForBlock_.end()
                                     ? nullptr : cached->second;
        currentGlobalPageMaterialized_ = false;
        emitBlock(bb);
    }
    currentGlobalPageBase_ = nullptr;
    emitEpilogue();
}

void Arm64FuncContext::prepareGlobalPageBases() {
    globalPageBaseForBlock_.clear();
    if (!enableRegAlloc_)
        return;

    for (auto *bb : func_->basic_blocks_)
        for (auto *inst : bb->instr_list_)
            if (inst->is_call())
                return;

    for (auto *bb : func_->basic_blocks_) {
        std::map<GlobalVariable*, int> uses;
        for (auto *inst : bb->instr_list_) {
            Value *address = nullptr;
            if (inst->op_id_ == Instruction::Load && inst->num_ops_ >= 1)
                address = inst->get_operand(0);
            else if (inst->op_id_ == Instruction::Store && inst->num_ops_ >= 2)
                address = inst->get_operand(1);
            else if (inst->op_id_ == Instruction::GetElementPtr && inst->num_ops_ >= 1)
                address = inst->get_operand(0);
            if (auto *global = dynamic_cast<GlobalVariable*>(address))
                ++uses[global];
        }

        GlobalVariable *best = nullptr;
        int bestUses = 1;
        for (const auto &entry : uses) {
            if (entry.second > bestUses ||
                (entry.second == bestUses && best && entry.first->name_ < best->name_)) {
                best = entry.first;
                bestUses = entry.second;
            }
        }
        if (best)
            globalPageBaseForBlock_[bb] = best;
    }
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
            auto *falseBB = static_cast<BasicBlock*>(term->get_operand(2));

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
                    if (auto *succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                        if (!placed.count(succ)) { next = succ; break; }
                    }
                }
            }
            if (next) {
                current = next;
            } else {
                // Fall back: 优先选"链头"——不是任何未放置块的 preferred 目标
                // 的块。否则会先放下合流块本身，使它前驱的无条件跳转永远无法
                // 变成 fallthrough（如循环体小块 → backedge 合流块）。
                BasicBlock *head = nullptr;
                for (auto bb : func_->basic_blocks_) {
                    if (placed.count(bb)) continue;
                    bool isPreferredTarget = false;
                    for (auto &kv : preferred) {
                        if (kv.second == bb && !placed.count(kv.first)) {
                            isPreferredTarget = true;
                            break;
                        }
                    }
                    if (!isPreferredTarget) { head = bb; break; }
                }
                if (!head) {
                    for (auto bb : func_->basic_blocks_) {
                        if (!placed.count(bb)) { head = bb; break; }
                    }
                }
                current = head;
            }
        }
        order.push_back(current);
        placed.insert(current);
    }

    func_->basic_blocks_ = order;
}

// 把提升常量占用的 callee-saved 寄存器并入保存列表（保持升序去重，
// prologue/epilogue 两处必须用同一结果以保证栈布局一致）
static void mergePromotedConstRegs(std::vector<int> &regs,
                                   const std::map<int, std::string> &promoted) {
    std::set<int> s(regs.begin(), regs.end());
    for (const auto &kv : promoted) {
        int regNo = std::stoi(kv.second.substr(1));
        if (regNo >= 19 && regNo <= 28)
            s.insert(regNo);
    }
    regs.assign(s.begin(), s.end());
}

void Arm64FuncContext::emitPrologue() {
    emitMachineLine("\t.global " + func_->name_);
    emitMachineLine("\t.p2align 2");
    emitMachineLine(func_->name_ + ":");

    // Allocate slots only for arguments that remain memory-resident or arrive
    // on the caller's stack.  Register-to-register argument shuffles are
    // resolved as parallel copies below, including cycles, without stack slots.
    {
        int intArgIdx = 0, floatArgIdx = 0;
        for (auto arg : func_->arguments_) {
            if (isFloat(arg->type_)) {
                if (floatArgIdx < 8) {
                    std::string src = "s" + std::to_string(floatArgIdx++);
                    if (hasAssignedReg(arg))
                        continue;
                }
            } else {
                if (intArgIdx < 8) {
                    bool isPtr = (arg->type_->tid_ == Type::PointerTyID ||
                                arg->type_->tid_ == Type::ArrayTyID);
                    std::string reg = (isPtr ? "x" : "w") + std::to_string(intArgIdx++);
                    if (hasAssignedReg(arg))
                        continue;
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
                // Skip compare whose only user is a Select —
                // the Select emits its own cmp/fcmp, the compare is never stored.
                if ((inst->op_id_ == Instruction::ICmp || inst->op_id_ == Instruction::FCmp) &&
                    inst->use_list_.size() == 1) {
                    auto *user = dynamic_cast<SelectInst*>((*inst->use_list_.begin()).val_);
                    if (user) continue;
                }
                // Skip Select whose only user is a Ret —
                // csel/fcsel writes directly to the return register, no stack slot needed.
                if (inst->op_id_ == Instruction::Select &&
                    inst->use_list_.size() == 1 &&
                    (isInt(inst->type_) || isPtr(inst->type_) || isFloat(inst->type_))) {
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
    mergePromotedConstRegs(savedIntRegs, promotedConsts_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    auto savedNEONRegs = collectAssignedNEONRegs(assignedRegs_);
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size() + savedNEONRegs.size()) * 8;

    // Determine whether the function has any call instructions.
    bool hasCalls = false;
    for (auto bb : func_->basic_blocks_) {
        for (auto inst : bb->instr_list_) {
            if (inst->is_call()) { hasCalls = true; break; }
        }
        if (hasCalls) break;
    }

    // Functions with no x29-relative slots and no stack-passed incoming args can
    // save only LR in the local SP frame instead of building a full x29/x30
    // frame record.  Keep x29 whenever existing slot addressing needs it.
    int intArgCount = 0, floatArgCount = 0;
    for (auto arg : func_->arguments_) {
        if (isFloat(arg->type_)) floatArgCount++;
        else intArgCount++;
    }
    bool hasStackArgs = (intArgCount > 8) || (floatArgCount > 8);
    needsFramePtr_ = hasStackArgs || (prologueFrameSize_ > 0);
    needsLinkSave_ = hasCalls && !needsFramePtr_;
    int linkSaveBytes = needsLinkSave_ ? 8 : 0;
    int localSize = align16(prologueFrameSize_ + savedRegBytes + linkSaveBytes);
    needsFrame_ = needsFramePtr_ || (localSize > 0);

    if (needsFramePtr_) {
        // stp supports only -512..504 range; use minimal stp + sub for large frames
        emitFramePushMachine();
        emitMoveMachine("x29", "sp");
    }
    if (localSize > 0) {
        if (localSize <= 4095) {
            emitStackAdjustMachine("sub", localSize);
        } else {
            emitIntConst(localSize, "x17");
            emitStackAdjustMachine("sub", "x17");
        }
    }

    int saveOffset = 0;
    for (size_t i = 0; i < savedIntRegs.size(); i += 2) {
        if (i + 1 < savedIntRegs.size()) {
            emitStorePairSPMachine("x" + std::to_string(savedIntRegs[i+1]),
                                   "x" + std::to_string(savedIntRegs[i]), saveOffset);
            saveOffset += 16;
        } else {
            emitStoreRegSPMachine("x" + std::to_string(savedIntRegs[i]), saveOffset);
            saveOffset += 8;
        }
    }
    for (size_t i = 0; i < savedFloatRegs.size(); i += 2) {
        if (i + 1 < savedFloatRegs.size()) {
            emitStorePairSPMachine("d" + std::to_string(savedFloatRegs[i+1]),
                                   "d" + std::to_string(savedFloatRegs[i]), saveOffset);
            saveOffset += 16;
        } else {
            emitStoreRegSPMachine("d" + std::to_string(savedFloatRegs[i]), saveOffset);
            saveOffset += 8;
        }
    }
    // AAPCS64 only requires callees to preserve the low 64 bits of v8-v15.
    for (size_t i = 0; i < savedNEONRegs.size(); i += 2) {
        if (i + 1 < savedNEONRegs.size()) {
            emitStorePairSPMachine("d" + std::to_string(savedNEONRegs[i]),
                                   "d" + std::to_string(savedNEONRegs[i+1]), saveOffset);
            saveOffset += 16;
        } else {
            emitStoreRegSPMachine("d" + std::to_string(savedNEONRegs[i]), saveOffset);
            saveOffset += 8;
        }
    }
    if (needsLinkSave_) {
        emitStoreRegSPMachine("x30", saveOffset);
        saveOffset += 8;
    }

   // ----- load arguments (including register arguments and stack arguments) -----
   // 入参搬移是一组并行拷贝：某个参数分配到的目标寄存器可能正是后续参数的
   // 入参寄存器（如 arg2 分配到 w3，而 arg3 经 w3 传入），不能边遍历边发射
   // mov。先保存真正需要常驻内存的参数（此时入参寄存器还保有原值），再按
   // "目标不挡未读源"的顺序发射 mov；出现拷贝环时用后端保留的临时寄存器
   // 保存一个源值来打破环。
   int intRegIdx = 0;     // x0-x7 / w0-w7
   int floatRegIdx = 0;   // s0-s7
   int stackOffset = 0;   // stack argument offset (relative to x29+16)

   struct ArgMove {
       std::string dst;
       std::string src;    // 空串表示只能从栈槽重载（栈传参）
       int slot;
       bool isFloat;
   };
   std::vector<ArgMove> argMoves;

   for (auto arg : func_->arguments_) {
        if (isFloat(arg->type_)) {
            if (floatRegIdx < 8) {
                std::string src = "s" + std::to_string(floatRegIdx++);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg);
                    if (dst == src) {
                        // Pre-colored to incoming register — no spill needed
                    } else {
                        argMoves.push_back({dst, src, 0, true});
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
                    argMoves.push_back({dst, "", slot, true});
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
                        argMoves.push_back({dst, reg, 0, false});
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
                    argMoves.push_back({dst, "", slot, false});
                }
                stackOffset += 8;
            }
        }
    }

    // 物理寄存器编号（w3/x3 同号同寄存器；s/d 同理），用于冲突判断
    auto regId = [](const std::string &r) -> std::string {
        if (r.empty()) return "";
        char c = r[0];
        return ((c == 'w' || c == 'x') ? "i" : "f") + r.substr(1);
    };
    while (!argMoves.empty()) {
        bool progress = false;
        for (size_t i = 0; i < argMoves.size(); ++i) {
            std::string dstId = regId(argMoves[i].dst);
            bool blocksPendingSrc = false;
            for (size_t j = 0; j < argMoves.size(); ++j) {
                if (j != i && !argMoves[j].src.empty() &&
                    regId(argMoves[j].src) == dstId) {
                    blocksPendingSrc = true;
                    break;
                }
            }
            if (blocksPendingSrc)
                continue;
            if (argMoves[i].src.empty())
                emitLoadRegMachine(argMoves[i].dst, argMoves[i].slot);
            else if (argMoves[i].isFloat)
                emitMoveMachine(argMoves[i].dst, argMoves[i].src, "fmov");
            else
                emitMoveMachine(argMoves[i].dst, argMoves[i].src);
            argMoves.erase(argMoves.begin() + i);
            progress = true;
            break;
        }
        if (!progress) {
            // Every remaining source is a register, so lack of progress means
            // the parallel-copy graph contains a cycle.  Save one source in a
            // register that is excluded from allocation, then make that edge
            // acyclic.  Preserve the source width for integer arguments.
            ArgMove &move = argMoves.front();
            std::string scratch;
            if (move.isFloat) {
                scratch = "s17";
                emitMoveMachine(scratch, move.src, "fmov");
            } else {
                scratch = (!move.src.empty() && move.src[0] == 'x') ? "x16" : "w16";
                emitMoveMachine(scratch, move.src);
            }
            std::string savedId = regId(move.src);
            for (auto &pending : argMoves) {
                if (regId(pending.src) != savedId)
                    continue;
                if (pending.isFloat)
                    pending.src = "s17";
                else
                    pending.src = (!pending.src.empty() && pending.src[0] == 'x')
                                      ? "x16" : "w16";
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
    mergePromotedConstRegs(savedIntRegs, promotedConsts_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    auto savedNEONRegs = collectAssignedNEONRegs(assignedRegs_);
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size() + savedNEONRegs.size()) * 8;
    int linkSaveBytes = needsLinkSave_ ? 8 : 0;
    int localSize = align16(prologueFrameSize_ + savedRegBytes + linkSaveBytes);

    int restoreOffset = 0;
    for (size_t i = 0; i < savedIntRegs.size(); i += 2) {
        if (i + 1 < savedIntRegs.size()) {
            emitLoadPairSPMachine("x" + std::to_string(savedIntRegs[i+1]),
                                  "x" + std::to_string(savedIntRegs[i]), restoreOffset);
            restoreOffset += 16;
        } else {
            emitLoadRegSPMachine("x" + std::to_string(savedIntRegs[i]), restoreOffset);
            restoreOffset += 8;
        }
    }
    for (size_t i = 0; i < savedFloatRegs.size(); i += 2) {
        if (i + 1 < savedFloatRegs.size()) {
            emitLoadPairSPMachine("d" + std::to_string(savedFloatRegs[i+1]),
                                  "d" + std::to_string(savedFloatRegs[i]), restoreOffset);
            restoreOffset += 16;
        } else {
            emitLoadRegSPMachine("d" + std::to_string(savedFloatRegs[i]), restoreOffset);
            restoreOffset += 8;
        }
    }
    // Match the prologue: restore only the ABI-preserved low 64 bits.
    for (size_t i = 0; i < savedNEONRegs.size(); i += 2) {
        if (i + 1 < savedNEONRegs.size()) {
            emitLoadPairSPMachine("d" + std::to_string(savedNEONRegs[i]),
                                  "d" + std::to_string(savedNEONRegs[i+1]), restoreOffset);
            restoreOffset += 16;
        } else {
            emitLoadRegSPMachine("d" + std::to_string(savedNEONRegs[i]), restoreOffset);
            restoreOffset += 8;
        }
    }
    if (needsLinkSave_) {
        emitLoadRegSPMachine("x30", restoreOffset);
        restoreOffset += 8;
    }

    if (localSize > 0) {
        if (localSize <= 4095) {
            emitStackAdjustMachine("add", localSize);
        } else {
            emitIntConst(localSize, "x17");
            emitStackAdjustMachine("add", "x17");
        }
    }
    if (needsFramePtr_) {
        emitFramePopMachine();
    }
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
    if (auto *alloca = dynamic_cast<AllocaInst*>(v)) {
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
