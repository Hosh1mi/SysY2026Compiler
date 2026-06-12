#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <vector>

void Arm64FuncContext::emitBlock(BasicBlock *bb) {
    if (blockSkipped_.count(bb)) return;

    // Lazily collect branch targets to detect dead entry-block labels.
    if (branchTargets_.empty() && !func_->basic_blocks_.empty()) {
        for (auto b : func_->basic_blocks_) {
            auto term = b->get_terminator();
            if (!term || !term->is_br()) continue;
            for (unsigned i = 0; i < term->num_ops_; ++i) {
                if (auto tgt = dynamic_cast<BasicBlock*>(term->get_operand(i)))
                    branchTargets_.insert(tgt);
            }
        }
    }

    // Emit block label only if this block is a branch target or not the entry block.
    // The entry block is reached via the function name, not its label.
    bool isEntry = (bb == func_->basic_blocks_[0]);
    if (!isEntry || branchTargets_.count(bb))
        emitMachineLine(bbLabel(func_, bb) + ":");

    resetRegs();

    auto &instrs = bb->instr_list_;
    for (auto it = instrs.begin(); it != instrs.end(); ++it) {
        auto inst = *it;
        if (inst->is_phi()) continue;

        // Skip ICmp if its only user is a Select (Select emits its own cmp)
        if (inst->op_id_ == Instruction::ICmp && inst->use_list_.size() == 1) {
            auto *user = dynamic_cast<SelectInst*>((*inst->use_list_.begin()).val_);
            if (user) continue; // Select will emit cmp + csel
        }

        // ICmp + Br fusion: csel / ccmp+csel / cmp + b.cond
        if (inst->op_id_ == Instruction::ICmp) {
            auto icmp = static_cast<ICmpInst*>(inst);
            auto next = std::next(it);
            if (next != instrs.end()) {
                auto br = dynamic_cast<BranchInst*>(*next);
                if (br && br->num_ops_ == 3 && br->get_operand(0) == icmp && icmp->use_list_.size() == 1) {
                    if (tryEmitCSel(icmp, br)) { ++it; continue; }
                    // if (tryEmitCCmpCSel(icmp, br)) { ++it; continue; }
                    emitFusedCmpBranch(icmp, br);
                    ++it; // skip Br
                    continue;
                }
            }
        }

        // Mul + Add/Sub fusion → madd / msub / mneg
        //
        //   mul %m, %a, %b   then  add/sub using %m  →  fused madd/msub/mneg
        //
        // The graph-coloring allocator may reuse the same physical register
        // for %a and the Add's other operand (non-overlapping liveness before
        // fusion).  To avoid clobbering, we load the mul operands and pin
        // them to scratch registers BEFORE emitting any intervening instructions.
        if (inst->op_id_ == Instruction::Mul && inst->use_list_.size() == 1 && !isVector(inst->type_)) {
            auto mulIt = it;
            bool fused = false;
            auto scan = std::next(it);
            int skip = 0;
            for (; scan != instrs.end() && skip < 3; ++scan, ++skip) {
                Instruction *sInst = *scan;
                if (sInst->is_phi()) continue;
                bool usesMul = false;
                for (unsigned i = 0; i < sInst->num_ops_; ++i)
                    if (sInst->get_operand(i) == inst) { usesMul = true; break; }
                if (usesMul && !dynamic_cast<BinaryInst*>(sInst)) break;

                auto addSub = dynamic_cast<BinaryInst*>(sInst);
                if (addSub) {
                    Value *op0 = addSub->get_operand(0);
                    Value *op1 = addSub->get_operand(1);
                    bool canMAdd = (addSub->op_id_ == Instruction::Add && (op0 == inst || op1 == inst));
                    bool canMSub = (addSub->op_id_ == Instruction::Sub && op1 == inst);
                    if (!canMAdd && !canMSub) break; // Add/Sub found but unrelated

                    // --- Fusion confirmed ---
                    BinaryInst *mulInst = static_cast<BinaryInst*>(inst);

                    // 1. Load mul operands and pin to w8 / w16.
                    //    w8 is never returned by allocIntReg (pool is w10-w15).
                    //    w16 is the fallback — we re-mark it after each
                    //    intervening instruction to prevent re-use.
                    resetRegs();
                    std::string rA = loadInt(mulInst->get_operand(0));
                    std::string rB = loadInt(mulInst->get_operand(1));
                    emitMoveMachine("w8", rA);
                    emitMoveMachine("w16", rB);

                    // 2. Emit intervening instructions.  Each calls resetRegs(),
                    //    so we re-pin w16 in usedIntRegs_ to keep it alive.
                    for (auto mid = std::next(mulIt); mid != scan; ++mid) {
                        if ((*mid)->is_phi()) continue;
                        emitInstruction(*mid);
                        if (!usedIntRegs_.count(16))
                            usedIntRegs_.insert(16);
                    }

                    // 3. Emit fused madd / msub / mneg using pinned operands
                    {
                        resetRegs();
                        std::string rA2 = "w8";
                        std::string rB2 = "w16";
                        Value *accOp = (op0 == inst) ? op1 : op0;
                        std::string rAcc = loadInt(accOp);
                        std::string rd = allocIntReg();
                        if (canMAdd) {
                            emitRawAluMachine("\tmadd " + rd + ", " + rA2 + ", " + rB2
                                              + ", " + rAcc,
                                              rd, {rA2, rB2, rAcc}, MOpcode::Mul, 3);
                        } else {
                            Value *minuend = addSub->get_operand(0);
                            if (auto *ci = dynamic_cast<ConstantInt*>(minuend)) {
                                if (ci->value_ == 0) {
                                    emitRawAluMachine("\tmneg " + rd + ", " + rA2
                                                      + ", " + rB2,
                                                      rd, {rA2, rB2}, MOpcode::Mul, 3);
                                    storeInt(addSub, rd);
                                    it = scan; fused = true; break;
                                }
                            }
                            std::string rMin = loadInt(minuend);
                            emitRawAluMachine("\tmsub " + rd + ", " + rA2 + ", " + rB2
                                              + ", " + rMin,
                                              rd, {rA2, rB2, rMin}, MOpcode::Mul, 3);
                        }
                        storeInt(addSub, rd);
                    }

                    it = scan; fused = true; break;
                }
            }
            if (fused) continue;
        }

        emitInstruction(inst);
    }
}

void Arm64FuncContext::emitInstruction(Instruction *inst) {
    resetRegs();
    for (unsigned i = 0; i < inst->num_ops_; ++i) {
        Value *op = inst->get_operand(i);
        if (!hasAssignedReg(op)) continue;

        std::string reg = assignedReg(op, isPtr(op->type_));
        if (reg.size() < 2) continue;

        int regNo = std::stoi(reg.substr(1));
        if (reg[0] == 'w' || reg[0] == 'x') {
            usedIntRegs_.insert(regNo);
        } else if (reg[0] == 's' || reg[0] == 'd') {
            usedFloatRegs_.insert(regNo);
        } else if (reg[0] == 'v') {
            usedNEONRegs_.insert(regNo);
        }
    }

    switch (inst->op_id_) {

    // ---- Alloca ----
    case Instruction::Alloca: {
        // nothing to emit; slot already allocated in prologue
        break;
    }

    // ---- Store ----
    case Instruction::Store: {
        auto val = inst->get_operand(0);
        auto ptr = inst->get_operand(1);
        if (isVector(val->type_)) {
            std::string addr = loadAddr(ptr);
            std::string vs = loadVector(val);
            MachineInstr st = MachineInstr::make("\tst1 {" + vs + ".4s}, [" + addr + "]",
                                                 MOpcode::Store, {}, {vs, addr});
            st.mayStore = true;
            emitMachineInstr(std::move(st));
            break;
        }
        if (auto gv = dynamic_cast<GlobalVariable*>(ptr)) {
            std::string base = allocAddrReg();
            emitMachineInstrLine("\tadrp " + base + ", " + gv->name_,
                                 MOpcode::Adr, {base});
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                emitStoreMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {r, base});
            } else if (isPtr(val->type_)) {
                std::string r = loadAddr(val);
                emitStoreMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {r, base});
            } else {
                std::string r = loadInt(val);
                emitStoreMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {r, base});
            }
        } else {
            std::string addr = loadAddr(ptr);
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                emitStoreMemMachine(r, "[" + addr + "]", {r, addr});
            } else if (isPtr(val->type_)) {
                std::string r = loadAddr(val);
                emitStoreMemMachine(r, "[" + addr + "]", {r, addr});
            } else {
                std::string r = loadInt(val);
                emitStoreMemMachine(r, "[" + addr + "]", {r, addr});
            }
        }
        break;
    }

    // ---- Load ----
    case Instruction::Load: {
        auto ptr = inst->get_operand(0);
        if (isVector(inst->type_)) {
            std::string addr = loadAddr(ptr);
            std::string vd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            MachineInstr ld = MachineInstr::make("\tld1 {" + vd + ".4s}, [" + addr + "]",
                                                MOpcode::Load, {vd}, {addr}, 4);
            ld.mayLoad = true;
            emitMachineInstr(std::move(ld));
            if (!hasAssignedReg(inst)) storeVector(inst, vd);
            break;
        }
        if (auto gv = dynamic_cast<GlobalVariable*>(ptr)) {
            std::string base = allocAddrReg();
            emitMachineInstrLine("\tadrp " + base + ", " + gv->name_,
                                 MOpcode::Adr, {base});
            if (isFloat(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
                emitLoadMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {base});
                storeFloat(inst, r);
            } else if (isPtr(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
                emitLoadMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {base});
                storeAddr(inst, r);
            } else {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitLoadMemMachine(r, "[" + base + ", :lo12:" + gv->name_ + "]", {base});
                storeInt(inst, r);
            }
        } else {
            std::string addr = loadAddr(ptr);
            if (isFloat(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
                emitLoadMemMachine(r, "[" + addr + "]", {addr});
                storeFloat(inst, r);
            } else if (isPtr(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
                emitLoadMemMachine(r, "[" + addr + "]", {addr});
                storeAddr(inst, r);
            } else {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitLoadMemMachine(r, "[" + addr + "]", {addr});
                storeInt(inst, r);
            }
        }
        break;
    }

    // ---- Integer Binary ----
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // Vector path: Add/Sub/Mul on <4 x i32>
        if (isVector(inst->type_)) {
            std::string r1 = loadVector(v1);
            std::string r2 = loadVector(v2);
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            const char *opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::Add: opcode = "add"; break;
                case Instruction::Sub: opcode = "sub"; break;
                case Instruction::Mul: opcode = "mul"; break;
                default: break;
            }
            emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ".4s, " + r1
                                  + ".4s, " + r2 + ".4s",
                              rd, {r1, r2}, MOpcode::Neon,
                              inst->op_id_ == Instruction::Mul ? 3 : 1);
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        bool emitted = false;

        // =====================================================================
        // SDiv 常量强度削减
        // =====================================================================
        if (inst->op_id_ == Instruction::SDiv) {
            if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
                int32_t d = ci->value_;

                if (d == 0) {
                    // 除以 0：让 sdiv 产生实现定义结果，fall-through
                } else {
                    // 安全计算绝对值——INT32_MIN 的 -d 会溢出，特判排除
                    // 若 d == INT32_MIN，abs_d = 0，后续条件不成立，自动 fall-through
                    int32_t abs_d = (d > 0) ? d
                                : (d == INT32_MIN ? 0 : -d);

                    // 正/负 2 的幂统一处理
                    if (abs_d > 0 && (abs_d & (abs_d - 1)) == 0) {
                        int k = __builtin_ctz(abs_d);
                        std::string rNum    = loadInt(v1);
                        // rNum 的最后一次读和 rResult 的首次写在同一条指令上，
                        // 因此 rResult 直接用分配寄存器即使与 rNum 同号也安全
                        std::string rResult = hasAssignedReg(inst) ? assignedReg(inst)
                                                                   : allocIntReg();

                        if (k == 1) {
                            // ÷2:  barrel-shifter folds bias into add
                            //   bias = rNum >>> 31  (0 or 1)
                            emitRawAluMachine("\tadd " + rResult + ", " + rNum + ", " + rNum + ", lsr #31",
                                              rResult, {rNum});
                        } else {
                            std::string rTmp = allocIntReg();
                            emitRawAluMachine("\tasr " + rTmp + ", " + rNum + ", #31",
                                              rTmp, {rNum});
                            emitRawAluMachine("\tbic " + rTmp + ", " + rTmp + ", " + rTmp + ", lsl #" + std::to_string(k),
                                              rTmp, {rTmp});
                            emitBinaryMachine("add", rResult, rNum, rTmp);
                        }
                        emitRawAluMachine("\tasr " + rResult + ", " + rResult + ", #" + std::to_string(k),
                                          rResult, {rResult});
                        if (d < 0)
                            emitUnaryMachine("neg", rResult, rResult);

                        storeInt(inst, rResult);
                        emitted = true;
                    }
                    // 非 2 的幂除数：magic number 优化
                    else if (abs_d > 1) {
                        Magic::MagicNumber mag = Magic::getMagic(d);

                        std::string wNum = loadInt(v1);
                        std::string wMagic = intConstReg(mag.multiplier);

                        std::string xTemp = allocAddrReg();
                        std::string wHi = "w" + xTemp.substr(1);

                        emitRawAluMachine("\tsmull " + xTemp + ", " + wNum + ", " + wMagic,
                                          xTemp, {wNum, wMagic}, MOpcode::Mul, 3);

                        if (mag.strat == Magic::MagicStrat::MULTIPLY_SHIFT) {
                            // 取高 32 位与 >>shift 合并为一条 64 位 asr
                            emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #"
                                                  + std::to_string(32 + mag.shift),
                                              xTemp, {xTemp});
                        } else {
                            emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #32",
                                              xTemp, {xTemp});
                            if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT)
                                emitBinaryMachine("add", wHi, wHi, wNum);
                            else
                                emitBinaryMachine("sub", wHi, wHi, wNum);
                            emitRawAluMachine("\tasr " + wHi + ", " + wHi + ", #"
                                                  + std::to_string(mag.shift),
                                              wHi, {wHi});
                        }
                        // 末条指令同时完成 wNum 的最后一次读，可直接写入分配寄存器
                        std::string wResult = hasAssignedReg(inst) ? assignedReg(inst)
                                                                   : allocIntReg();
                        emitRawAluMachine("\tadd " + wResult + ", " + wHi + ", " + wNum + ", lsr #31",
                                          wResult, {wHi, wNum});

                        storeInt(inst, wResult);
                        emitted = true;
                    }
                }
            }
        }

        // =====================================================================
        // Mul 常量强度削减
        // 覆盖 0、±1、±2^k、±(2^k+1)、±(2^k-1) 六族因数
        // =====================================================================
        if (!emitted && inst->op_id_ == Instruction::Mul) {
            // 乘法可交换——优先在 v2 找常量，找不到再看 v1
            ConstantInt* ci  = dynamic_cast<ConstantInt*>(v2);
            Value*       var = v1;
            if (!ci) { ci = dynamic_cast<ConstantInt*>(v1); var = v2; }

            if (ci) {
                int32_t factor = ci->value_;

                if (factor == -1) {
                    // × -1：negate
                    // 以下各情形 rd 的写入都不早于 r 的最后一次读（同指令内
                    // 读先于写），因此 rd 直接用分配寄存器、与 r 同号也安全
                    std::string r  = loadInt(var);
                    std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                    emitUnaryMachine("neg", rd, r);
                    storeInt(inst, rd);
                    emitted = true;

                } else if (factor != INT32_MIN) {  // INT32_MIN 的 -factor 溢出，跳过
                    bool    negative = (factor < 0);
                    int32_t abs_f    = negative ? -factor : factor;

                    // 情形 A：abs_f = 2^k
                    //   正：lsl rd, r, #k
                    //   负：lsl rd, r, #k  +  neg          各 1 条
                    if ((abs_f & (abs_f - 1)) == 0) {
                        int k = __builtin_ctz(abs_f);
                        std::string r  = loadInt(var);
                        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                        emitRawAluMachine("\tlsl " + rd + ", " + r + ", #" + std::to_string(k),
                                          rd, {r});
                        if (negative)
                            emitUnaryMachine("neg", rd, rd);
                        storeInt(inst, rd);
                        emitted = true;
                    }

                    // 情形 B：abs_f = 2^k + 1（如 3,5,9,17,33…）
                    //   正：add rd, r, r, lsl #k            1 条
                    //   负：add rd, r, r, lsl #k  +  neg    2 条
                    else if (int32_t m1 = abs_f - 1;
                            m1 > 0 && (m1 & (m1 - 1)) == 0)
                    {
                        int k = __builtin_ctz(m1);
                        std::string r  = loadInt(var);
                        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                        emitRawAluMachine("\tadd " + rd + ", " + r + ", "
                                          + r + ", lsl #" + std::to_string(k),
                                          rd, {r});
                        if (negative)
                            emitUnaryMachine("neg", rd, rd);
                        storeInt(inst, rd);
                        emitted = true;
                    }

                    // 情形 C：abs_f = 2^k - 1（如 3,7,15,31,63…）
                    //   正：lsl tmp, r, #k  ;  sub rd, tmp, r    2 条
                    //   负：factor = 1 - 2^k，即 r - r<<k
                    //       sub rd, r, r, lsl #k                 1 条  ← 关键优化
                    else if (int32_t p1 = abs_f + 1;   // +1 不会溢出：abs_f <= INT32_MAX-1
                            p1 > 0 && (p1 & (p1 - 1)) == 0)
                    {
                        int k = __builtin_ctz(p1);
                        std::string r  = loadInt(var);
                        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                        if (negative) {
                            // r*(1 - 2^k) = r - r*2^k
                            emitRawAluMachine("\tsub " + rd + ", " + r + ", "
                                              + r + ", lsl #" + std::to_string(k),
                                              rd, {r});
                        } else {
                            std::string rTmp = allocIntReg();
                            emitRawAluMachine("\tlsl " + rTmp + ", " + r + ", #" + std::to_string(k),
                                              rTmp, {r});
                            emitBinaryMachine("sub", rd, rTmp, r);
                        }
                        storeInt(inst, rd);
                        emitted = true;
                    }
                    // 其余常量：fall-through 到通用 mul
                }
            }
        }

        // =====================================================================
        // 通用路径（Add / Sub / Mul 无法优化，或 SDiv 非常量 / 非 2^k 除数）
        // =====================================================================
        if (!emitted) {
            const char* opcode = nullptr;
            bool usedImm = false;
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

            // Try immediate forms for Add/Sub before loading both operands
            if (inst->op_id_ == Instruction::Add) {
                opcode = "add";
                if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
                    if (ci->value_ >= 0 && ci->value_ <= 4095) {
                        std::string r1 = loadInt(v1);
                        emitMachineInstrLine(
                            "\tadd " + rd + ", " + r1 + ", #" + std::to_string(ci->value_),
                            MOpcode::Alu, {rd}, {r1});
                        usedImm = true;
                    }
                }
                if (!usedImm && dynamic_cast<ConstantInt*>(v1)) {
                    auto ci = static_cast<ConstantInt*>(v1);
                    if (ci->value_ >= 0 && ci->value_ <= 4095) {
                        std::string r2 = loadInt(v2);
                        emitMachineInstrLine(
                            "\tadd " + rd + ", " + r2 + ", #" + std::to_string(ci->value_),
                            MOpcode::Alu, {rd}, {r2});
                        usedImm = true;
                    }
                }
            } else if (inst->op_id_ == Instruction::Sub) {
                opcode = "sub";
                // Skip immediate form when v1 is zero: sub rd, wzr, #imm is illegal
                // ARM64 sub(immediate) uses the WSP encoding slot, so WZR is not allowed.
                if (!(dynamic_cast<ConstantInt*>(v1) && static_cast<ConstantInt*>(v1)->value_ == 0)) {
                    if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
                        if (ci->value_ >= 0 && ci->value_ <= 4095) {
                            std::string r1 = loadInt(v1);
                            emitMachineInstrLine(
                                "\tsub " + rd + ", " + r1 + ", #" + std::to_string(ci->value_),
                                MOpcode::Alu, {rd}, {r1});
                            usedImm = true;
                        }
                    }
                }
            }

            if (!usedImm) {
                std::string r1 = loadInt(v1);
                std::string r2 = loadInt(v2);
                switch (inst->op_id_) {
                    case Instruction::Add:  opcode = "add";  break;
                    case Instruction::Sub:  opcode = "sub";  break;
                    case Instruction::Mul:  opcode = "mul";  break;
                    case Instruction::SDiv: opcode = "sdiv"; break;
                    default: break;
                }
                MOpcode mop = (inst->op_id_ == Instruction::Mul) ? MOpcode::Mul :
                              (inst->op_id_ == Instruction::SDiv) ? MOpcode::Div :
                              MOpcode::Alu;
                int latency = (inst->op_id_ == Instruction::Mul) ? 3 :
                              (inst->op_id_ == Instruction::SDiv) ? 12 : 1;
                emitMachineInstrLine(
                    "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", " + r2,
                    mop, {rd}, {r1, r2}, latency);
            }
            storeInt(inst, rd);
        }
        break;
    }

    // ---- SRem: a % b = a - (a/b) * b ----
    // Be cautious modifying this 
    case Instruction::SRem: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        if (auto ci = dynamic_cast<ConstantInt*>(v2)) { // 检查第二个操作数是否是常量
            int32_t divisor = ci->value_;
            if (divisor == 0) { /* Fallback */ }
            // ---- 除数为 1，余数恒为 0 ----
            else if (divisor == -1) {
                std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitMoveMachine(rd, "wzr");
                storeInt(inst, rd);
                break;
            }
            else if (divisor == 2) {
                std::string r = loadInt(v1);
                // rd 在 tst 读 r 之前就被写入，rd 与 r 同号时必须走 scratch
                std::string rd = (hasAssignedReg(inst) && assignedReg(inst) != r)
                                     ? assignedReg(inst) : allocIntReg();
                emitRawAluMachine("\tand " + rd + ", " + r + ", #1", rd, {r});
                emitMachineInstrLine("\ttst " + r + ", " + r,
                                     MOpcode::Cmp, {}, {r}, 1, true);
                emitMachineInstrLine("\tcneg " + rd + ", " + rd + ", mi",
                                     MOpcode::FlagUse, {rd}, {rd}, 1, false, true);
                storeInt(inst, rd);
                break;
            }
            // ---- 除数为 2 的幂 (d > 0) ----
            else if (divisor > 0 && (divisor & (divisor - 1)) == 0) {
                // rem = num - (((num >> 31) & (d-1)) + num) >> k) << k
                int k = __builtin_ctz(divisor);   // log2(d)
                std::string rNum = loadInt(v1);
                std::string rSign = allocIntReg();
                std::string rQ = allocIntReg();

                emitRawAluMachine("\tasr " + rSign + ", " + rNum + ", #31", rSign, {rNum});
                emitRawAluMachine("\tand " + rSign + ", " + rSign + ", #"
                                      + std::to_string(divisor - 1),
                                  rSign, {rSign});
                emitBinaryMachine("add", rQ, rNum, rSign);
                emitRawAluMachine("\tasr " + rQ + ", " + rQ + ", #" + std::to_string(k), rQ, {rQ});
                emitRawAluMachine("\tlsl " + rQ + ", " + rQ + ", #" + std::to_string(k), rQ, {rQ});

                // 末条 sub 同时是 rNum 的最后一次读，直接写分配寄存器安全
                std::string rResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitBinaryMachine("sub", rResult, rNum, rQ);
                storeInt(inst, rResult);
                break;
            }
            // ---- 除数为正且 > 1，使用 Magic Number ----
            else if (divisor > 1) {
                Magic::MagicNumber mag = Magic::getMagic(divisor);

                std::string wNum = loadInt(v1);
                std::string wMagic = intConstReg(mag.multiplier);

                std::string xTemp = allocAddrReg();
                std::string wHi = "w" + xTemp.substr(1);

                emitRawAluMachine("\tsmull " + xTemp + ", " + wNum + ", " + wMagic,
                                  xTemp, {wNum, wMagic}, MOpcode::Mul, 3);

                if (mag.strat == Magic::MagicStrat::MULTIPLY_SHIFT) {
                    // 取高 32 位与 >>shift 合并为一条 64 位 asr
                    emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #"
                                          + std::to_string(32 + mag.shift),
                                      xTemp, {xTemp});
                } else {
                    emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #32",
                                      xTemp, {xTemp});
                    if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT)
                        emitBinaryMachine("add", wHi, wHi, wNum);
                    else
                        emitBinaryMachine("sub", wHi, wHi, wNum);
                    emitRawAluMachine("\tasr " + wHi + ", " + wHi + ", #"
                                          + std::to_string(mag.shift),
                                      wHi, {wHi});
                }
                emitRawAluMachine("\tadd " + wHi + ", " + wHi + ", " + wNum + ", lsr #31",
                                  wHi, {wHi, wNum});

                std::string wD = intConstReg(divisor);
                // 中间结果全在 scratch，wNum 的最后一次读在末条 msub 上，
                // 结果可直接写分配寄存器（与 wNum 同号亦安全）
                std::string wResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                emitRawAluMachine("\tmsub " + wResult + ", " + wHi + ", " + wD + ", " + wNum,
                                  wResult, {wHi, wD, wNum}, MOpcode::Mul, 3);

                storeInt(inst, wResult);
                break;
            }
            // 负除数或 0 继续走通用路径
        }

        // ---- 通用 SRem (变量除数或未优化情况) ----
        std::string ra = loadInt(v1);
        std::string rb = loadInt(v2);
        std::string rq = allocIntReg();
        // 末条 msub 同时是 ra/rb 的最后一次读，直接写分配寄存器安全
        std::string rr = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitBinaryMachine("sdiv", rq, ra, rb, MOpcode::Div, 12);
        emitRawAluMachine("\tmsub " + rr + ", " + rq + ", " + rb + ", " + ra,
                          rr, {rq, rb, ra}, MOpcode::Mul, 3);
        storeInt(inst, rr);
        break;
    }

        // ---- Integer Bitwise Logical (And / Or / Xor) ----
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        std::string r1 = loadInt(v1);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

        const char *opcode;
        if (inst->op_id_ == Instruction::And)      opcode = "and";
        else if (inst->op_id_ == Instruction::Or)   opcode = "orr";
        else                                        opcode = "eor";

        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            // 立即数位运算：and/orr/eor wd, w1, #imm
            // ARM64 and/orr/eor 支持的立即数格式有限（位掩码立即数），
            // 对于简单的小常数（如 1, 3, 7, 15 等 2^n-1）通常可以编码
            // 若不可编码，则需加载到寄存器
            uint32_t imm = static_cast<uint32_t>(ci->value_);
            // 简单判断：对于 and 指令，2^n-1 形式的掩码总是可编码的
            // 对于 orr/eor，小常数也可编码
            // 为安全起见，如果立即数较小或为位掩码形式，使用立即数
            // 否则先加载到寄存器
            bool useImmediate = false;
            if (inst->op_id_ == Instruction::And) {
                // and 指令的立即数：ARM64 支持复杂的位掩码立即数
                // 简单启发式：值 <= 0xFFFF 或是 2^n-1 形式
                if (imm <= 0xFFFF || (imm & (imm + 1)) == 0) {
                    useImmediate = true;
                }
            } else if (inst->op_id_ == Instruction::Or) {
                // orr 立即数也是位掩码立即数
                if (imm <= 0xFFFF) {
                    useImmediate = true;
                }
            } else {
                // eor 立即数也是位掩码立即数
                if (imm <= 0xFFFF) {
                    useImmediate = true;
                }
            }

            if (useImmediate) {
                emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ", " + r1
                                      + ", #" + std::to_string(ci->value_),
                                  rd, {r1});
            } else {
                // 加载立即数到寄存器
                std::string r2 = allocIntReg();
                emitIntConst(ci->value_, r2);
                emitBinaryMachine(opcode, rd, r1, r2);
                freeIntReg(r2);
            }
        } else {
            // 寄存器位运算：and/orr/eor wd, w1, w2
            std::string r2 = loadInt(v2);
            emitBinaryMachine(opcode, rd, r1, r2);
        }
        storeInt(inst, rd);
        break;
    }

    // ---- Integer Shift ----
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // Vector path
        if (isVector(inst->type_)) {
            std::string r1 = loadVector(v1);
            std::string r2 = loadVector(v2);
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            const char *opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::Shl:  opcode = "sshl"; break;
                case Instruction::AShr: opcode = "sshr"; break;
                case Instruction::LShr: opcode = "ushr"; break;
                default: break;
            }
            emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ".4s, " + r1
                                  + ".4s, " + r2 + ".4s",
                              rd, {r1, r2}, MOpcode::Neon);
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        std::string r1 = loadInt(v1);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

        const char *opcode;
        if (inst->op_id_ == Instruction::Shl)      opcode = "lsl";
        else if (inst->op_id_ == Instruction::LShr) opcode = "lsr";
        else                                        opcode = "asr";

        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            emitMachineInstrLine(
                "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", #" + std::to_string(ci->value_),
                MOpcode::Alu, {rd}, {r1});
        } else {
            std::string r2 = loadInt(v2);
            emitMachineInstrLine(
                "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", " + r2,
                MOpcode::Alu, {rd}, {r1, r2});
        }
        storeInt(inst, rd);
        break;
    }

    // ---- Float Binary ----
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);

        // Vector path: FAdd/FSub/FMul on <4 x float>
        if (isVector(inst->type_)) {
            std::string r1 = loadVector(v1);
            std::string r2 = loadVector(v2);
            std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
            const char *opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::FAdd: opcode = "fadd"; break;
                case Instruction::FSub: opcode = "fsub"; break;
                case Instruction::FMul: opcode = "fmul"; break;
                default: break;
            }
            emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ".4s, " + r1
                                  + ".4s, " + r2 + ".4s",
                              rd, {r1, r2}, MOpcode::Neon,
                              inst->op_id_ == Instruction::FMul ? 4 : 1);
            if (!hasAssignedReg(inst)) storeVector(inst, rd);
            break;
        }

        std::string r1 = loadFloat(v1);
        std::string r2 = loadFloat(v2);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
        const char *opcode = nullptr;
        switch (inst->op_id_) {
        case Instruction::FAdd: opcode = "fadd"; break;
        case Instruction::FSub: opcode = "fsub"; break;
        case Instruction::FMul: opcode = "fmul"; break;
        case Instruction::FDiv: opcode = "fdiv"; break;
        default: break;
        }
        MOpcode mop = (inst->op_id_ == Instruction::FMul) ? MOpcode::Mul :
                      (inst->op_id_ == Instruction::FDiv) ? MOpcode::Div :
                      MOpcode::Alu;
        int latency = (inst->op_id_ == Instruction::FMul) ? 5 :
                      (inst->op_id_ == Instruction::FDiv) ? 12 : 4;
        emitMachineInstrLine(
            "\t" + std::string(opcode) + " " + rd + ", " + r1 + ", " + r2,
            mop, {rd}, {r1, r2}, latency);
        storeFloat(inst, rd);
        break;
    }

    // ---- InsertElement ----
    case Instruction::InsertElement: {
        auto *vec = inst->get_operand(0);
        auto *val = inst->get_operand(1);
        auto *idx = inst->get_operand(2);
        auto *ci = dynamic_cast<ConstantInt*>(idx);
        if (!ci) break; // index must be constant
        int lane = ci->value_;
        // mov v.s[lane] requires a W register; floats need fmov first
        std::string ws;
        if (isFloat(val->type_)) {
            std::string sr = loadFloat(val);
            ws = allocIntReg();
            emitMoveMachine(ws, sr, "fmov");
        } else {
            ws = loadInt(val);
        }
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocNEONReg();
        // If base is not a vector (first insert in construction chain),
        // skip the copy and initialize the vector fresh.
        if (isVector(vec->type_)) {
            std::string vs = loadVector(vec);
            if (rd != vs)
                emitRawAluMachine("\tmov " + rd + ".16b, " + vs + ".16b",
                                  rd, {vs}, MOpcode::Neon);
        }
        emitRawAluMachine("\tmov " + rd + ".s[" + std::to_string(lane) + "], " + ws,
                          rd, {ws}, MOpcode::Neon);
        if (!hasAssignedReg(inst)) storeVector(inst, rd);
        break;
    }

    // ---- ICmp ----
    case Instruction::ICmp: {
        auto icmp = static_cast<ICmpInst*>(inst);
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        std::string r1 = isPtr(v1->type_) ? loadAddr(v1) : loadInt(v1);
        std::string r2 = isPtr(v2->type_) ? loadAddr(v2) : loadInt(v2);
        std::string rd = allocIntReg();
        emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                             MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        emitMachineInstrLine("\tcset " + rd + ", " + icmpCond(icmp->icmp_op_),
                             MOpcode::FlagUse, {rd}, {}, 1, false, true, true);
        storeInt(inst, rd);
        break;
    }

    // ---- Select ----
    case Instruction::Select: {
        auto *condVal = inst->get_operand(0);
        auto *tv = inst->get_operand(1);
        auto *fv = inst->get_operand(2);
        auto *icmp = dynamic_cast<ICmpInst*>(condVal);
        if (!icmp) break; // fallback: can't emit csel without flags

        // Emit cmp (like fused cmp+branch, but followed by csel)
        auto *cv1 = icmp->get_operand(0);
        auto *cv2 = icmp->get_operand(1);
        std::string r1 = isPtr(cv1->type_) ? loadAddr(cv1) : loadInt(cv1);
        const char *cond = icmpCond(icmp->icmp_op_);
        if (auto ci = dynamic_cast<ConstantInt*>(cv2)) {
            int val = ci->value_;
            if (val >= 0 && val <= 4095)
                emitMachineInstrLine("\tcmp " + r1 + ", #" + std::to_string(val),
                                     MOpcode::Cmp, {}, {r1}, 1, true, false, true);
            else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                     MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true); }
        } else {
            std::string r2 = isPtr(cv2->type_) ? loadAddr(cv2) : loadInt(cv2);
            emitMachineInstrLine("\tcmp " + r1 + ", " + r2,
                                 MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        }

        std::string dstReg;
        // If this Select's only user is a Ret, write directly to w0/x0
        // to avoid a redundant mov in the Ret emission.
        bool directRet = false;
        if (!isFloat(inst->type_) && inst->use_list_.size() == 1) {
            auto *user = dynamic_cast<ReturnInst*>((*inst->use_list_.begin()).val_);
            if (user) directRet = true;
        }
        if (directRet) {
            dstReg = isPtr(inst->type_) ? "x0" : "w0";
        } else {
            dstReg = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        }
        std::string trueReg = hasAssignedReg(tv)  ? assignedReg(tv)  : loadInt(tv);
        std::string falseReg= hasAssignedReg(fv)  ? assignedReg(fv)  : loadInt(fv);
        emitMachineInstrLine("\tcsel " + dstReg + ", " + trueReg + ", " + falseReg + ", " + cond,
                             MOpcode::FlagUse, {dstReg}, {trueReg, falseReg}, 1,
                             false, true, true);
        if (!directRet && !hasAssignedReg(inst)) storeInt(inst, dstReg);
        break;
    }

    // ---- FCmp ----
    case Instruction::FCmp: {
        auto fcmp = static_cast<FCmpInst*>(inst);
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        std::string r1 = loadFloat(v1);
        std::string r2 = loadFloat(v2);
        std::string rd = allocIntReg();
        emitMachineInstrLine("\tfcmp " + r1 + ", " + r2,
                             MOpcode::Cmp, {}, {r1, r2}, 1, true, false, true);
        emitMachineInstrLine("\tcset " + rd + ", " + fcmpCond(fcmp->fcmp_op_),
                             MOpcode::FlagUse, {rd}, {}, 1, false, true, true);
        storeInt(inst, rd);
        break;
    }

    // ---- GEP ----
    case Instruction::GetElementPtr: {
        auto gep = static_cast<GetElementPtrInst*>(inst);
        auto ptr = gep->get_operand(0);

        std::string addr;
        auto samePhysReg = [](const std::string &a, const std::string &b) {
            if (a.size() < 2 || b.size() < 2) return a == b;
            bool aInt = a[0] == 'w' || a[0] == 'x';
            bool bInt = b[0] == 'w' || b[0] == 'x';
            bool aFloat = a[0] == 's' || a[0] == 'd';
            bool bFloat = b[0] == 's' || b[0] == 'd';
            return ((aInt && bInt) || (aFloat && bFloat)) && a.substr(1) == b.substr(1);
        };

        bool useAssignedAddr = hasAssignedReg(inst);
        std::string assignedAddr = useAssignedAddr ? assignedReg(inst, true) : "";
        if (useAssignedAddr) {
            for (unsigned i = 1; i < gep->num_ops_; ++i) {
                auto idx = gep->get_operand(i);
                if (dynamic_cast<ConstantInt*>(idx) || !hasAssignedReg(idx))
                    continue;
                if (samePhysReg(assignedAddr, assignedReg(idx))) {
                    useAssignedAddr = false;
                    break;
                }
            }
        }

        if (useAssignedAddr) {
            addr = assignedAddr;
            if (auto gv = dynamic_cast<GlobalVariable*>(ptr)) {
                emitGlobalAddr(gv, addr);
            } else if (auto ci = dynamic_cast<ConstantInt*>(ptr)) {
                emitIntConst(ci->value_, addr);
            } else if (hasAssignedReg(ptr)) {
                std::string base = assignedReg(ptr, true);
                if (addr != base)
                    emitMoveMachine(addr, base);
            } else if (dynamic_cast<AllocaInst*>(ptr)) {
                int off = getSlot(ptr);
                if (off < 0) {
                    int absOff = -off;
                    if (absOff <= 4095) {
                        emitRawAluMachine("\tsub " + addr + ", x29, #" + std::to_string(absOff),
                                          addr, {"x29"});
                    } else {
                        emitIntConst(absOff, "x17");
                        emitBinaryMachine("sub", addr, "x29", "x17");
                    }
                } else {
                    if (off <= 4095) {
                        emitRawAluMachine("\tadd " + addr + ", x29, #" + std::to_string(off),
                                          addr, {"x29"});
                    } else {
                        emitIntConst(off, "x17");
                        emitBinaryMachine("add", addr, "x29", "x17");
                    }
                }
            } else {
                emitLoadRegMachine(addr, getSlot(ptr));
            }
        } else {
            std::string base = loadAddr(ptr);
            if (hasAssignedReg(ptr)) {
                addr = allocAddrReg();
                emitMoveMachine(addr, base);
            } else {
                addr = base;
            }
        }
    
        unsigned numIdx = gep->num_ops_ - 1;
        auto srcTy = static_cast<PointerType*>(ptr->type_)->contained_;
        Type *curTy = srcTy;
    
        for (unsigned i = 1; i < gep->num_ops_; i++) {
            auto idx = gep->get_operand(i);
            
            // 1. 当前层级元素的大小（一定是 typeSize(curTy)）
            int elemSize = typeSize(curTy);
            
            // 2. 更新 curTy 到下一层（为下一次迭代准备）
            if (curTy->tid_ == Type::ArrayTyID) {
                auto at = static_cast<ArrayType*>(curTy);
                curTy = at->contained_;
            } else if (curTy->tid_ == Type::PointerTyID) {
                auto pt = static_cast<PointerType*>(curTy);
                curTy = pt->contained_;
            }
            // 否则是基本类型，不再更新（后续索引非法，但一般不会出现）
            
            if (auto ci = dynamic_cast<ConstantInt*>(idx)) {
                int offset = ci->value_ * elemSize;
                if (offset != 0) {
                    if (offset > 0 && offset <= 4095) {
                        emitRawAluMachine("\tadd " + addr + ", " + addr + ", #" + std::to_string(offset),
                                          addr, {addr});
                    } else if (offset < 0 && -offset <= 4095) {
                        emitRawAluMachine("\tsub " + addr + ", " + addr + ", #" + std::to_string(-offset),
                                          addr, {addr});
                    } else {
                        emitIntConst(abs(offset), "x17");
                        if (offset > 0)
                            emitBinaryMachine("add", addr, addr, "x17");
                        else
                            emitBinaryMachine("sub", addr, addr, "x17");
                    }
                }
            }else {
                std::string idxReg = loadInt(idx);
                std::string scaled = allocAddrReg();
                // 符号扩展索引到64位
                emitMoveMachine(scaled, idxReg, "sxtw");
                freeIntReg(idxReg);
    
                if (elemSize > 1) {
                    auto isPowerOfTwo = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
                    if (isPowerOfTwo(elemSize)) {
                        int shift = 0;
                        while ((1 << shift) < elemSize) shift++;
                        emitRawAluMachine("\tadd " + addr + ", " + addr + ", " + scaled
                                          + ", lsl #" + std::to_string(shift),
                                          addr, {addr, scaled});
                        freeAddrReg(scaled);
                    } else {
                        std::string elemReg = allocAddrReg();
                        emitIntConst(elemSize, elemReg);
                        emitBinaryMachine("mul", scaled, scaled, elemReg, MOpcode::Mul, 3);
                        emitBinaryMachine("add", addr, addr, scaled);
                        freeAddrReg(elemReg);
                        freeAddrReg(scaled);  // scaled 可以释放了，因为结果已累加到 addr
                    }
                } else {
                    emitBinaryMachine("add", addr, addr, scaled);
                    freeAddrReg(scaled);
                }
            }
        }
        if (!useAssignedAddr)
            storeAddr(inst, addr);
        break;
    }

    // ---- ZExt (i1 → i32) ----
    case Instruction::ZExt: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tand " + rd + ", " + r + ", #1", rd, {r});
        storeInt(inst, rd);
        break;
    }

    // ---- FPtoSI (float → i32) ----
    case Instruction::FPtoSI: {
        auto val = inst->get_operand(0);
        std::string r = loadFloat(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitUnaryMachine("fcvtzs", rd, r, MOpcode::Alu, 4);
        storeInt(inst, rd);
        break;
    }

    // ---- SItoFP (i32 → float) ----
    case Instruction::SItoFP: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
        emitUnaryMachine("scvtf", rd, r, MOpcode::Alu, 4);
        storeFloat(inst, rd);
        break;
    }

    // ---- BitCast ----
    case Instruction::BitCast: {
        auto val = inst->get_operand(0);
        if (isFloat(inst->type_) && isInt(val->type_)) {
            std::string r = loadInt(val);
            std::string rd = allocFloatReg();
            emitMoveMachine(rd, r, "fmov");
            storeFloat(inst, rd);
        } else if (isInt(inst->type_) && isFloat(val->type_)) {
            std::string r = loadFloat(val);
            std::string rd = allocIntReg();
            emitMoveMachine(rd, r, "fmov");
            storeInt(inst, rd);
        } else {
            // pointer bitcasts: just copy
            std::string r = loadAddr(val);
            storeAddr(inst, r);
        }
        break;
    }

    // ---- Clz (count leading zeros) ----
    case Instruction::Clz: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = allocIntReg();
        emitUnaryMachine("clz", rd, r);
        storeInt(inst, rd);
        break;
    }

    // ---- Br ----
    case Instruction::Br: {
        auto parentBB = inst->parent_;

        if (inst->num_ops_ == 1) {
            // Unconditional branch: emit copies for the single edge
            auto target = static_cast<BasicBlock*>(inst->get_operand(0));
            emitPhiCopies(parentBB, target);
            emitBranchMachine("\tb " + bbLabel(func_, target));
        } else {
            // Conditional branch: evaluate condition FIRST, then edge-specific copies
            auto cond = inst->get_operand(0);
            auto trueBB = static_cast<BasicBlock*>(inst->get_operand(1));
            auto falseBB = static_cast<BasicBlock*>(inst->get_operand(2));

            // Check if either edge has phi copies
            bool hasTrue = false, hasFalse = false;
            for (const auto &pc : phiCopies_) {
                if (pc.pred != parentBB) continue;
                if (pc.succ == trueBB) hasTrue = true;
                if (pc.succ == falseBB) hasFalse = true;
            }

            std::string cr = loadInt(cond);

            if (!hasTrue && !hasFalse) {
                emitBranchMachine("\tcbnz " + cr + ", " + bbLabel(func_, trueBB), {cr});
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            } else if (hasTrue && !hasFalse) {
                emitBranchMachine("\tcbz " + cr + ", " + bbLabel(func_, falseBB), {cr});
                emitPhiCopies(parentBB, trueBB);
                emitBranchMachine("\tb " + bbLabel(func_, trueBB));
            } else if (!hasTrue && hasFalse) {
                emitBranchMachine("\tcbnz " + cr + ", " + bbLabel(func_, trueBB), {cr});
                emitPhiCopies(parentBB, falseBB);
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            } else {
                // Both edges have copies: use edge label
                std::string edgeLbl = ".L" + func_->name_ + "_edge_" +
                    std::to_string(edgeCounter_++);
                emitBranchMachine("\tcbz " + cr + ", " + edgeLbl, {cr});
                emitPhiCopies(parentBB, trueBB);
                emitBranchMachine("\tb " + bbLabel(func_, trueBB));
                emitMachineLine(edgeLbl + ":");
                emitPhiCopies(parentBB, falseBB);
                emitBranchMachine("\tb " + bbLabel(func_, falseBB));
            }
        }
        break;
    }

    // ---- Ret ----
    case Instruction::Ret: {
        if (inst->num_ops_ > 0) {
            auto val = inst->get_operand(0);
            // If val is a non-float Select whose only user is this Ret,
            // the csel already wrote the result to w0/x0 — skip the mov.
            bool alreadyInW0 = false;
            if (!isFloat(val->type_)) {
                if (auto si = dynamic_cast<SelectInst*>(val)) {
                    if (si->use_list_.size() == 1) alreadyInW0 = true;
                }
            }
            if (!alreadyInW0) {
                if (isFloat(val->type_)) {
                    std::string r = loadFloat(val);
                    emitMoveMachine("s0", r, "fmov");
                } else if (isPtr(val->type_)) {
                    std::string r = loadAddr(val);
                    emitMoveMachine("x0", r);
                } else {
                    std::string r = loadInt(val);
                    emitMoveMachine("w0", r);
                }
            }
        }
        if (needsFrame_)
            emitBranchMachine("\tb .L" + func_->name_ + "_epilogue");
        else
            emitRetMachine();
        break;
    }

    // ---- Call ----
    case Instruction::Call: {
        auto call = static_cast<CallInst*>(inst);
        unsigned numArgs = call->num_ops_ - 1;
        auto callee = static_cast<Function*>(call->get_operand(numArgs));

        struct RegArg {
            unsigned index;
            Value *value;
            std::string dst;
            bool isFloat;
            bool isPtr;
        };

        std::vector<RegArg> regArgs;
        std::set<int> intArgDests;
        std::set<int> floatArgDests;

        int scanIntArg = 0, scanFloatArg = 0;
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                if (scanFloatArg < 8) {
                    regArgs.push_back({i, arg, "s" + std::to_string(scanFloatArg), true, false});
                    floatArgDests.insert(scanFloatArg);
                }
                scanFloatArg++;
            } else {
                bool ptr = isPtr(arg->type_);
                if (scanIntArg < 8) {
                    regArgs.push_back({i, arg,
                                      (ptr ? "x" : "w") + std::to_string(scanIntArg),
                                      false, ptr});
                    intArgDests.insert(scanIntArg);
                }
                scanIntArg++;
            }
        }

        // Register argument assignment is a parallel copy: evaluating it
        // sequentially can clobber a later source already sitting in x0-x7/s0-s7.
        // Pre-copy those sources to scratch registers so regalloc does not need
        // to force every call operand into callee-saved registers.
        std::map<Value*, std::string> callArgTemps;
        auto regNo = [](const std::string &reg) -> int {
            if (reg.size() < 2) return -1;
            return std::stoi(reg.substr(1));
        };
        auto regPrefix = [](const std::string &reg) -> char {
            return reg.empty() ? '\0' : reg[0];
        };
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (!hasAssignedReg(arg) || callArgTemps.count(arg))
                continue;

            bool argIsFloat = isFloat(arg->type_);
            bool argIsPtr = isPtr(arg->type_);
            std::string src = assignedReg(arg, argIsPtr);
            int srcNo = regNo(src);
            if (srcNo < 0)
                continue;

            bool sourceCanBeClobbered = false;
            if (argIsFloat) {
                sourceCanBeClobbered = floatArgDests.count(srcNo) > 0;
            } else {
                sourceCanBeClobbered = intArgDests.count(srcNo) > 0;
            }
            if (!sourceCanBeClobbered)
                continue;

            bool hasSameRegisterDest = false;
            for (const auto &ra : regArgs) {
                if (ra.index != i) continue;
                hasSameRegisterDest = (regNo(ra.dst) == srcNo && regPrefix(ra.dst) == regPrefix(src));
                break;
            }
            if (hasSameRegisterDest)
                continue;

            if (argIsFloat) {
                usedFloatRegs_.insert(srcNo);
                std::string tmp = allocFloatReg();
                emitMoveMachine(tmp, src, "fmov");
                callArgTemps[arg] = tmp;
            } else if (argIsPtr) {
                usedIntRegs_.insert(srcNo);
                std::string tmp = allocAddrReg();
                emitMoveMachine(tmp, src);
                callArgTemps[arg] = tmp;
            } else {
                usedIntRegs_.insert(srcNo);
                std::string tmp = allocIntReg();
                emitMoveMachine(tmp, src);
                callArgTemps[arg] = tmp;
            }
        }

        // 计算参数分配信息
        int intArg = 0, floatArg = 0;
        int stackArgsCount = 0;
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                if (floatArg++ >= 8) stackArgsCount++;
            } else {
                if (intArg++ >= 8) stackArgsCount++;
            }
        }
        int stackBytes = align16(stackArgsCount * 8);

        // 分配栈参数空间
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                emitStackAdjustMachine("sub", stackBytes);
            else {
                emitIntConst(stackBytes, "x17");
                emitStackAdjustMachine("sub", "x17");
            }
        }
    
        // 传递参数 (寄存器 + 栈)
        intArg = 0; floatArg = 0;
        int stackIdx = 0;   // 栈参数写入偏移 (相对于 sp)
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadFloat(arg);
                if (floatArg < 8) {
                    emitMoveMachine("s" + std::to_string(floatArg++), r, "fmov");
                } else {
                    emitStoreMemMachine(r, "[sp, #" + std::to_string(stackIdx * 8) + "]", {r, "sp"});
                    stackIdx++;
                }
            } else if (isPtr(arg->type_)) {
                std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadAddr(arg);
                if (intArg < 8) {
                    emitMoveMachine("x" + std::to_string(intArg++), r);
                } else {
                    emitStoreMemMachine(r, "[sp, #" + std::to_string(stackIdx * 8) + "]", {r, "sp"});
                    stackIdx++;
                }
            } else {
                if (intArg < 8) {
                    std::string dst = "w" + std::to_string(intArg++);
                    if (auto ci = dynamic_cast<ConstantInt*>(arg)) {
                        if (ci->value_ == 0)
                            emitMoveMachine(dst, "wzr");
                        else
                            emitIntConst(ci->value_, dst);
                    } else {
                        std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadInt(arg);
                        emitMoveMachine(dst, r);
                    }
                } else {
                    std::string r = callArgTemps.count(arg) ? callArgTemps[arg] : loadInt(arg);
                    std::string tmp = allocAddrReg();
                    emitMoveMachine(tmp, r, "sxtw");
                    emitStoreMemMachine(tmp, "[sp, #" + std::to_string(stackIdx * 8) + "]", {tmp, "sp"});
                    freeAddrReg(tmp);
                    stackIdx++;
                }
            }
        }
    
        // 执行调用
        emitCallMachine(callee->name_);
    
        // 回收栈参数空间
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                emitStackAdjustMachine("add", stackBytes);
            else {
                emitIntConst(stackBytes, "x17");
                emitStackAdjustMachine("add", "x17");
            }
        }
    
        // 处理返回值 
        if (!isVoid(inst->type_)) {
            if (isFloat(inst->type_)) {
                storeFloat(inst, "s0");
            } else if (isPtr(inst->type_)) {
                storeAddr(inst, "x0");
            } else {
                storeInt(inst, "w0");
            }
        }
        break;
    }

    // ---- PHI: handled by phiCopies, nothing to emit ----
    case Instruction::PHI:
        break;

    // ---- FNeg ----
    case Instruction::FNeg: {
        auto val = inst->get_operand(0);
        std::string r = loadFloat(val);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
        emitMachineInstrLine("\tfneg " + rd + ", " + r,
                             MOpcode::Alu, {rd}, {r}, 4);
        storeFloat(inst, rd);
        break;
    }

    default:
        emitMachineLine("\t// unsupported op_id: " + std::to_string((int)inst->op_id_));
        break;
    }
}
