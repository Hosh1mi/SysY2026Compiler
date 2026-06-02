#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>

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
        os_ << bbLabel(func_, bb) << ":\n";

    resetRegs();
    neonEmitted_.clear();
    deferredNEONCode_.clear();

    tryEmitNEON(bb);

    bool neonEmitted = false;
    auto &instrs = bb->instr_list_;
    for (auto it = instrs.begin(); it != instrs.end(); ++it) {
        auto inst = *it;
        if (inst->is_phi()) continue;

        // Emit deferred NEON code at the position of the first
        // NEON-lowered instruction.  This places NEON stores after
        // __aeabi_memclr4 (zeroing) so that the values survive.
        if (!neonEmitted && !deferredNEONCode_.empty() && neonEmitted_.count(inst)) {
            os_ << deferredNEONCode_;
            neonEmitted = true;
        }
        if (neonEmitted_.count(inst)) continue;

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
        if (inst->op_id_ == Instruction::Mul && inst->use_list_.size() == 1) {
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
                    //    w8 is never returned by allocIntReg (pool is w9-w15).
                    //    w16 is the fallback — we re-mark it after each
                    //    intervening instruction to prevent re-use.
                    resetRegs();
                    std::string rA = loadInt(mulInst->get_operand(0));
                    std::string rB = loadInt(mulInst->get_operand(1));
                    os_ << "\tmov w8, " << rA << "\n";
                    os_ << "\tmov w16, " << rB << "\n";

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
                            os_ << "\tmadd " << rd << ", " << rA2 << ", " << rB2
                                << ", " << rAcc << "\n";
                        } else {
                            Value *minuend = addSub->get_operand(0);
                            if (auto *ci = dynamic_cast<ConstantInt*>(minuend)) {
                                if (ci->value_ == 0) {
                                    os_ << "\tmneg " << rd << ", " << rA2
                                        << ", " << rB2 << "\n";
                                    storeInt(addSub, rd);
                                    it = scan; fused = true; break;
                                }
                            }
                            std::string rMin = loadInt(minuend);
                            os_ << "\tmsub " << rd << ", " << rA2 << ", " << rB2
                                << ", " << rMin << "\n";
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
    // If the deferred code was never flushed (e.g. NEON-matched
    // instructions are the very last in the block), emit now.
    if (!neonEmitted && !deferredNEONCode_.empty()) {
        os_ << deferredNEONCode_;
    }
}

void Arm64FuncContext::emitInstruction(Instruction *inst) {
    resetRegs();
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
        if (auto gv = dynamic_cast<GlobalVariable*>(ptr)) {
            std::string base = allocAddrReg();
            os_ << "\tadrp " << base << ", " << gv->name_ << "\n";
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                os_ << "\tstr " << r << ", [" << base << ", :lo12:" << gv->name_ << "]\n";
            } else if (isPtr(val->type_)) {
                std::string r = loadAddr(val);
                os_ << "\tstr " << r << ", [" << base << ", :lo12:" << gv->name_ << "]\n";
            } else {
                std::string r = loadInt(val);
                os_ << "\tstr " << r << ", [" << base << ", :lo12:" << gv->name_ << "]\n";
            }
        } else {
            std::string addr = loadAddr(ptr);
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                os_ << "\tstr " << r << ", [" << addr << "]\n";
            } else if (isPtr(val->type_)) {
                std::string r = loadAddr(val);
                os_ << "\tstr " << r << ", [" << addr << "]\n";
            } else {
                std::string r = loadInt(val);
                os_ << "\tstr " << r << ", [" << addr << "]\n";
            }
        }
        break;
    }

    // ---- Load ----
    case Instruction::Load: {
        auto ptr = inst->get_operand(0);
        if (auto gv = dynamic_cast<GlobalVariable*>(ptr)) {
            std::string base = allocAddrReg();
            os_ << "\tadrp " << base << ", " << gv->name_ << "\n";
            if (isFloat(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
                os_ << "\tldr " << r << ", [" << base << ", :lo12:" << gv->name_ << "]\n";
                storeFloat(inst, r);
            } else if (isPtr(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
                os_ << "\tldr " << r << ", [" << base << ", :lo12:" << gv->name_ << "]\n";
                storeAddr(inst, r);
            } else {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                os_ << "\tldr " << r << ", [" << base << ", :lo12:" << gv->name_ << "]\n";
                storeInt(inst, r);
            }
        } else {
            std::string addr = loadAddr(ptr);
            if (isFloat(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocFloatReg();
                os_ << "\tldr " << r << ", [" << addr << "]\n";
                storeFloat(inst, r);
            } else if (isPtr(inst->type_)) {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst, true) : allocAddrReg();
                os_ << "\tldr " << r << ", [" << addr << "]\n";
                storeAddr(inst, r);
            } else {
                std::string r = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
                os_ << "\tldr " << r << ", [" << addr << "]\n";
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
        bool emitted = false;

        // =====================================================================
        // SDiv 常量强度削减
        // =====================================================================
        if (inst->op_id_ == Instruction::SDiv) {
            if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
                int32_t d = ci->value_;

                if (d == 0) {
                    // 除以 0：让 sdiv 产生实现定义结果，fall-through
                } else if (d == 1) {
                    // ÷1：identity
                    std::string r = loadInt(v1);
                    storeInt(inst, r);
                    emitted = true;
                } else if (d == -1) {
                    // ÷(-1)：negate
                    std::string r  = loadInt(v1);
                    std::string rd = allocIntReg();
                    os_ << "\tneg " << rd << ", " << r << "\n";
                    storeInt(inst, rd);
                    emitted = true;
                } else {
                    // 安全计算绝对值——INT32_MIN 的 -d 会溢出，特判排除
                    // 若 d == INT32_MIN，abs_d = 0，后续条件不成立，自动 fall-through
                    int32_t abs_d = (d > 0) ? d
                                : (d == INT32_MIN ? 0 : -d);

                    // 正/负 2 的幂统一处理
                    if (abs_d > 0 && (abs_d & (abs_d - 1)) == 0) {
                        int k = __builtin_ctz(abs_d);
                        std::string rNum    = loadInt(v1);
                        std::string rResult = allocIntReg();

                        if (k == 1) {
                            // ÷2:  barrel-shifter folds bias into add
                            //   bias = rNum >>> 31  (0 or 1)
                            os_ << "\tadd " << rResult << ", " << rNum << ", " << rNum << ", lsr #31\n";
                        } else {
                            std::string rTmp = allocIntReg();
                            os_ << "\tasr " << rTmp << ", " << rNum << ", #31\n";
                            os_ << "\tbic " << rTmp << ", " << rTmp << ", " << rTmp << ", lsl #" << k << "\n";
                            os_ << "\tadd " << rResult << ", " << rNum << ", " << rTmp << "\n";
                        }
                        os_ << "\tasr " << rResult << ", " << rResult << ", #" << k << "\n";
                        if (d < 0)
                            os_ << "\tneg " << rResult << ", " << rResult << "\n";

                        storeInt(inst, rResult);
                        emitted = true;
                    }
                    // 非 2 的幂除数：magic number 优化
                    else if (abs_d > 1) {
                        Magic::MagicNumber mag = Magic::getMagic(d);

                        std::string wNum = loadInt(v1);
                        std::string wMagic = allocIntReg();
                        emitIntConst(mag.multiplier, wMagic);

                        std::string wNumSafe = allocIntReg();
                        os_ << "\tmov " << wNumSafe << ", " << wNum << "\n";

                        std::string xTemp = allocAddrReg();
                        std::string wHi = "w" + xTemp.substr(1);

                        os_ << "\tsmull " << xTemp << ", " << wNumSafe << ", " << wMagic << "\n";
                        os_ << "\tasr " << xTemp << ", " << xTemp << ", #32\n";

                        if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT) {
                            os_ << "\tadd " << wHi << ", " << wHi << ", " << wNumSafe << "\n";
                        } else if (mag.strat == Magic::MagicStrat::MULTIPLY_SUB_SHIFT) {
                            os_ << "\tsub " << wHi << ", " << wHi << ", " << wNumSafe << "\n";
                        }

                        os_ << "\tasr " << wHi << ", " << wHi << ", #" << mag.shift << "\n";
                        os_ << "\tadd " << wHi << ", " << wHi << ", " << wNumSafe << ", lsr #31\n";

                        storeInt(inst, wHi);
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

                if (factor == 0) {
                    // × 0
                    std::string rd = allocIntReg();
                    os_ << "\tmov " << rd << ", #0\n";
                    storeInt(inst, rd);
                    emitted = true;

                } else if (factor == 1) {
                    // × 1：identity
                    std::string r = loadInt(var);
                    storeInt(inst, r);
                    emitted = true;

                } else if (factor == -1) {
                    // × -1：negate
                    std::string r  = loadInt(var);
                    std::string rd = allocIntReg();
                    os_ << "\tneg " << rd << ", " << r << "\n";
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
                        std::string rd = allocIntReg();
                        os_ << "\tlsl " << rd << ", " << r << ", #" << k << "\n";
                        if (negative)
                            os_ << "\tneg " << rd << ", " << rd << "\n";
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
                        std::string rd = allocIntReg();
                        os_ << "\tadd " << rd << ", " << r << ", "
                            << r << ", lsl #" << k << "\n";
                        if (negative)
                            os_ << "\tneg " << rd << ", " << rd << "\n";
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
                        std::string rd = allocIntReg();
                        if (negative) {
                            // r*(1 - 2^k) = r - r*2^k
                            os_ << "\tsub " << rd << ", " << r << ", "
                                << r << ", lsl #" << k << "\n";
                        } else {
                            std::string rTmp = allocIntReg();
                            os_ << "\tlsl " << rTmp << ", " << r << ", #" << k << "\n";
                            os_ << "\tsub " << rd   << ", " << rTmp << ", " << r << "\n";
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
            std::string r1 = loadInt(v1);
            std::string r2 = loadInt(v2);
            std::string rd = allocIntReg();
            const char* opcode = nullptr;
            switch (inst->op_id_) {
                case Instruction::Add:  opcode = "add";  break;
                case Instruction::Sub:  opcode = "sub";  break;
                case Instruction::Mul:  opcode = "mul";  break;
                case Instruction::SDiv: opcode = "sdiv"; break;
                default: break;
            }
            os_ << "\t" << opcode << " " << rd << ", " << r1 << ", " << r2 << "\n";
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
            else if (divisor == 1 || divisor == -1) {
                std::string rd = allocIntReg();
                os_ << "\tmov " << rd << ", wzr\n";
                storeInt(inst, rd);
                break;
            }
            else if (divisor == 2) {
                std::string r = loadInt(v1);
                std::string rd = allocIntReg();
                os_ << "\tand " << rd << ", " << r << ", #1\n";   
                os_ << "\ttst " << r << ", " << r << "\n";        
                os_ << "\tcneg " << rd << ", " << rd << ", mi\n"; 
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

                os_ << "\tasr " << rSign << ", " << rNum << ", #31\n";
                os_ << "\tand " << rSign << ", " << rSign << ", #" << (divisor - 1) << "\n";
                os_ << "\tadd " << rQ << ", " << rNum << ", " << rSign << "\n";
                os_ << "\tasr " << rQ << ", " << rQ << ", #" << k << "\n";
                os_ << "\tlsl " << rQ << ", " << rQ << ", #" << k << "\n";

                std::string rResult = allocIntReg();
                os_ << "\tsub " << rResult << ", " << rNum << ", " << rQ << "\n";
                storeInt(inst, rResult);
                break;
            }
            // ---- 除数为正且 > 1，使用 Magic Number ----
            else if (divisor > 1) {
                Magic::MagicNumber mag = Magic::getMagic(divisor);

                std::string wNum = loadInt(v1);
                std::string wMagic = allocIntReg();
                emitIntConst(mag.multiplier, wMagic);

                std::string wNumSafe = allocIntReg();
                os_ << "\tmov " << wNumSafe << ", " << wNum << "\n";

                std::string xTemp = allocAddrReg();
                std::string wHi = "w" + xTemp.substr(1);

                os_ << "\tsmull " << xTemp << ", " << wNumSafe << ", " << wMagic << "\n";
                os_ << "\tasr " << xTemp << ", " << xTemp << ", #32\n";

                if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT) {
                    os_ << "\tadd " << wHi << ", " << wHi << ", " << wNumSafe << "\n";
                } else if (mag.strat == Magic::MagicStrat::MULTIPLY_SUB_SHIFT) {
                    os_ << "\tsub " << wHi << ", " << wHi << ", " << wNumSafe << "\n";
                }

                os_ << "\tasr " << wHi << ", " << wHi << ", #" << mag.shift << "\n";
                os_ << "\tadd " << wHi << ", " << wHi << ", " << wNumSafe << ", lsr #31\n";

                std::string wD = allocIntReg();
                emitIntConst(divisor, wD);
                std::string wResult = allocIntReg();
                os_ << "\tmsub " << wResult << ", " << wHi << ", " << wD << ", " << wNumSafe << "\n";

                storeInt(inst, wResult);
                break;
            }
            // 负除数或 0 继续走通用路径
        }

        // ---- 通用 SRem (变量除数或未优化情况) ----
        std::string ra = loadInt(v1);
        std::string rb = loadInt(v2);
        std::string rq = allocIntReg();
        std::string rr = allocIntReg();
        os_ << "\tsdiv " << rq << ", " << ra << ", " << rb << "\n";
        os_ << "\tmsub " << rr << ", " << rq << ", " << rb << ", " << ra << "\n";
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
        std::string rd = allocIntReg();

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
                os_ << "\t" << opcode << " " << rd << ", " << r1 << ", #" << ci->value_ << "\n";
            } else {
                // 加载立即数到寄存器
                std::string r2 = allocIntReg();
                emitIntConst(ci->value_, r2);
                os_ << "\t" << opcode << " " << rd << ", " << r1 << ", " << r2 << "\n";
                freeIntReg(r2);
            }
        } else {
            // 寄存器位运算：and/orr/eor wd, w1, w2
            std::string r2 = loadInt(v2);
            os_ << "\t" << opcode << " " << rd << ", " << r1 << ", " << r2 << "\n";
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
        std::string r1 = loadInt(v1);
        std::string rd = allocIntReg();

        const char *opcode;
        if (inst->op_id_ == Instruction::Shl)      opcode = "lsl";
        else if (inst->op_id_ == Instruction::LShr) opcode = "lsr";
        else                                        opcode = "asr";

        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            // 立即数移位：lsl wd, w1, #shift
            os_ << "\t" << opcode << " " << rd << ", " << r1 << ", #" << ci->value_ << "\n";
        } else {
            // 寄存器移位：lsl wd, w1, w2
            std::string r2 = loadInt(v2);
            os_ << "\t" << opcode << " " << rd << ", " << r1 << ", " << r2 << "\n";
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
        std::string r1 = loadFloat(v1);
        std::string r2 = loadFloat(v2);
        std::string rd = allocFloatReg();
        const char *opcode = nullptr;
        switch (inst->op_id_) {
        case Instruction::FAdd: opcode = "fadd"; break;
        case Instruction::FSub: opcode = "fsub"; break;
        case Instruction::FMul: opcode = "fmul"; break;
        case Instruction::FDiv: opcode = "fdiv"; break;
        default: break;
        }
        os_ << "\t" << opcode << " " << rd << ", " << r1 << ", " << r2 << "\n";
        storeFloat(inst, rd);
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
        os_ << "\tcmp " << r1 << ", " << r2 << "\n";
        os_ << "\tcset " << rd << ", " << icmpCond(icmp->icmp_op_) << "\n";
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
                os_ << "\tcmp " << r1 << ", #" << val << "\n";
            else { std::string r2 = allocIntReg(); emitIntConst(val, r2);
                os_ << "\tcmp " << r1 << ", " << r2 << "\n"; }
        } else {
            std::string r2 = isPtr(cv2->type_) ? loadAddr(cv2) : loadInt(cv2);
            os_ << "\tcmp " << r1 << ", " << r2 << "\n";
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
        os_ << "\tcsel " << dstReg << ", " << trueReg << ", " << falseReg << ", " << cond << "\n";
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
        os_ << "\tfcmp " << r1 << ", " << r2 << "\n";
        os_ << "\tcset " << rd << ", " << fcmpCond(fcmp->fcmp_op_) << "\n";
        storeInt(inst, rd);
        break;
    }

    // ---- GEP ----
    case Instruction::GetElementPtr: {
        auto gep = static_cast<GetElementPtrInst*>(inst);
        auto ptr = gep->get_operand(0);
        std::string base = loadAddr(ptr);          
        std::string addr;
        if(hasAssignedReg(ptr)) {
            addr = allocAddrReg();
            os_ << "\tmov " << addr << ", " << base << "\n";
        } else {
            addr = base;
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
                        os_ << "\tadd " << addr << ", " << addr << ", #" << offset << "\n";
                    } else if (offset < 0 && -offset <= 4095) {
                        os_ << "\tsub " << addr << ", " << addr << ", #" << -offset << "\n";
                    } else {
                        os_ << "\tmovz x17, #" << (abs(offset) & 0xFFFF) << "\n";
                        os_ << "\tmovk x17, #" << ((abs(offset) >> 16) & 0xFFFF) << ", lsl #16\n";
                        if (offset > 0)
                            os_ << "\tadd " << addr << ", " << addr << ", x17\n";
                        else
                            os_ << "\tsub " << addr << ", " << addr << ", x17\n";
                    }
                }
            }else {
                std::string idxReg = loadInt(idx);
                std::string scaled = allocAddrReg();
                // 符号扩展索引到64位
                os_ << "\tsxtw " << scaled << ", " << idxReg << "\n";
                freeIntReg(idxReg);
    
                if (elemSize > 1) {
                    auto isPowerOfTwo = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
                    if (isPowerOfTwo(elemSize)) {
                        int shift = 0;
                        while ((1 << shift) < elemSize) shift++;
                        os_ << "\tadd " << addr << ", " << addr << ", " << scaled << ", lsl #" << shift << "\n";
                        freeAddrReg(scaled);
                    } else {
                        std::string elemReg = allocAddrReg();
                        uint32_t val = static_cast<uint32_t>(elemSize);
                        os_ << "\tmovz " << elemReg << ", #" << (val & 0xFFFF) << "\n";
                        if (val & 0xFFFF0000) {
                            os_ << "\tmovk " << elemReg << ", #" << ((val >> 16) & 0xFFFF) << ", lsl #16\n";
                        }
                        os_ << "\tmul " << scaled << ", " << scaled << ", " << elemReg << "\n";
                        os_ << "\tadd " << addr << ", " << addr << ", " << scaled << "\n";
                        freeAddrReg(elemReg);
                        freeAddrReg(scaled);  // scaled 可以释放了，因为结果已累加到 addr
                    }
                } else {
                    os_ << "\tadd " << addr << ", " << addr << ", " << scaled << "\n";
                    freeAddrReg(scaled);
                }
            }
        }
        storeAddr(inst, addr);
        break;
    }

    // ---- ZExt (i1 → i32) ----
    case Instruction::ZExt: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = allocIntReg();
        os_ << "\tand " << rd << ", " << r << ", #1\n";
        storeInt(inst, rd);
        break;
    }

    // ---- FPtoSI (float → i32) ----
    case Instruction::FPtoSI: {
        auto val = inst->get_operand(0);
        std::string r = loadFloat(val);
        std::string rd = allocIntReg();
        os_ << "\tfcvtzs " << rd << ", " << r << "\n";
        storeInt(inst, rd);
        break;
    }

    // ---- SItoFP (i32 → float) ----
    case Instruction::SItoFP: {
        auto val = inst->get_operand(0);
        std::string r = loadInt(val);
        std::string rd = allocFloatReg();
        os_ << "\tscvtf " << rd << ", " << r << "\n";
        storeFloat(inst, rd);
        break;
    }

    // ---- BitCast ----
    case Instruction::BitCast: {
        auto val = inst->get_operand(0);
        if (isFloat(inst->type_) && isInt(val->type_)) {
            std::string r = loadInt(val);
            std::string rd = allocFloatReg();
            os_ << "\tfmov " << rd << ", " << r << "\n";
            storeFloat(inst, rd);
        } else if (isInt(inst->type_) && isFloat(val->type_)) {
            std::string r = loadFloat(val);
            std::string rd = allocIntReg();
            os_ << "\tfmov " << rd << ", " << r << "\n";
            storeInt(inst, rd);
        } else {
            // pointer bitcasts: just copy
            std::string r = loadAddr(val);
            storeAddr(inst, r);
        }
        break;
    }

    // ---- Br ----
    case Instruction::Br: {
        auto parentBB = inst->parent_;

        if (inst->num_ops_ == 1) {
            // Unconditional branch: emit copies for the single edge
            auto target = static_cast<BasicBlock*>(inst->get_operand(0));
            emitPhiCopies(parentBB, target);
            os_ << "\tb " << bbLabel(func_, target) << "\n";
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
                os_ << "\tcbnz " << cr << ", " << bbLabel(func_, trueBB) << "\n";
                os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
            } else if (hasTrue && !hasFalse) {
                os_ << "\tcbz " << cr << ", " << bbLabel(func_, falseBB) << "\n";
                emitPhiCopies(parentBB, trueBB);
                os_ << "\tb " << bbLabel(func_, trueBB) << "\n";
            } else if (!hasTrue && hasFalse) {
                os_ << "\tcbnz " << cr << ", " << bbLabel(func_, trueBB) << "\n";
                emitPhiCopies(parentBB, falseBB);
                os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
            } else {
                // Both edges have copies: use edge label
                std::string edgeLbl = ".L" + func_->name_ + "_edge_" +
                    std::to_string(edgeCounter_++);
                os_ << "\tcbz " << cr << ", " << edgeLbl << "\n";
                emitPhiCopies(parentBB, trueBB);
                os_ << "\tb " << bbLabel(func_, trueBB) << "\n";
                os_ << edgeLbl << ":\n";
                emitPhiCopies(parentBB, falseBB);
                os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
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
                    os_ << "\tfmov s0, " << r << "\n";
                } else if (isPtr(val->type_)) {
                    std::string r = loadAddr(val);
                    os_ << "\tmov x0, " << r << "\n";
                } else {
                    std::string r = loadInt(val);
                    os_ << "\tmov w0, " << r << "\n";
                }
            }
        }
        if (needsFrame_)
            os_ << "\tb .L" << func_->name_ << "_epilogue\n";
        else
            os_ << "\tret\n";
        break;
    }

    // ---- Call ----
    case Instruction::Call: {
        auto call = static_cast<CallInst*>(inst);
        unsigned numArgs = call->num_ops_ - 1;
        auto callee = static_cast<Function*>(call->get_operand(numArgs));
    
        // ---- 内联 __aeabi_memclr4 ----
        if (callee->name_ == "__aeabi_memclr4") {
            auto ptr = call->get_operand(0);
            auto sizeVal = call->get_operand(1);
            std::string addr = loadAddr(ptr);
    
            bool useLoop = true;
            if (auto sizeConst = dynamic_cast<ConstantInt*>(sizeVal)) {
                int bytes = sizeConst->value_;
                constexpr int MAX_UNROLL_BYTES = 256;
                if (bytes <= MAX_UNROLL_BYTES) {
                    int off;
                    for (off = 0; off + 8 <= bytes; off += 8) {
                        if (off == 0)
                            os_ << "\tstp wzr, wzr, [" << addr << "]\n";
                        else
                            os_ << "\tstp wzr, wzr, [" << addr << ", #" << off << "]\n";
                    }
                    if (off < bytes) {
                        if (off == 0)
                            os_ << "\tstr wzr, [" << addr << "]\n";
                        else
                            os_ << "\tstr wzr, [" << addr << ", #" << off << "]\n";
                    }
                    useLoop = false;
                }
            }
            if (useLoop) {
                // use member variable instead of static (thread-safe)
                std::string sizeReg = loadInt(sizeVal);
                std::string zeroReg = allocIntReg();
                std::string loop = ".L" + func_->name_ + "_memclr_" + std::to_string(memclrCounter_++);
                std::string done = loop + "_done";
                os_ << "\tmov " << zeroReg << ", wzr\n";
                os_ << loop << ":\n";
                os_ << "\tcmp " << sizeReg << ", #0\n";
                os_ << "\tble " << done << "\n";
                os_ << "\tstr " << zeroReg << ", [" << addr << "], #4\n";
                os_ << "\tsub " << sizeReg << ", " << sizeReg << ", #4\n";
                os_ << "\tb " << loop << "\n";
                os_ << done << ":\n";
            }
            break;
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
                os_ << "\tsub sp, sp, #" << stackBytes << "\n";
            else {
                os_ << "\tmovz x17, #" << (stackBytes & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((stackBytes >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tsub sp, sp, x17\n";
            }
        }
    
        // 传递参数 (寄存器 + 栈)
        intArg = 0; floatArg = 0;
        int stackIdx = 0;   // 栈参数写入偏移 (相对于 sp)
        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                std::string r = loadFloat(arg);
                if (floatArg < 8) {
                    os_ << "\tfmov s" << floatArg++ << ", " << r << "\n";
                } else {
                    os_ << "\tstr " << r << ", [sp, #" << (stackIdx * 8) << "]\n";
                    stackIdx++;
                }
            } else if (isPtr(arg->type_)) {
                std::string r = loadAddr(arg);
                if (intArg < 8) {
                    os_ << "\tmov x" << intArg++ << ", " << r << "\n";
                } else {
                    os_ << "\tstr " << r << ", [sp, #" << (stackIdx * 8) << "]\n";
                    stackIdx++;
                }
            } else {
                std::string r = loadInt(arg);
                if (intArg < 8) {
                    os_ << "\tmov w" << intArg++ << ", " << r << "\n";
                } else {
                    std::string tmp = allocAddrReg();
                    os_ << "\tsxtw " << tmp << ", " << r << "\n";
                    os_ << "\tstr " << tmp << ", [sp, #" << (stackIdx * 8) << "]\n";
                    freeAddrReg(tmp);
                    stackIdx++;
                }
            }
        }
    
        // 执行调用
        os_ << "\tbl " << callee->name_ << "\n";
    
        // 回收栈参数空间
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                os_ << "\tadd sp, sp, #" << stackBytes << "\n";
            else {
                os_ << "\tmovz x17, #" << (stackBytes & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((stackBytes >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tadd sp, sp, x17\n";
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
        std::string rd = allocFloatReg();
        os_ << "\tfneg " << rd << ", " << r << "\n";
        storeFloat(inst, rd);
        break;
    }

    default:
        os_ << "\t// unsupported op_id: " << (int)inst->op_id_ << "\n";
        break;
    }
}
