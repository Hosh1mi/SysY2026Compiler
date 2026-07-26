#include "../../include/backend/arm64/context.hpp"
#include "../../include/backend/arm64/helpers.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include "../../include/mid/ir/ir.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════
// AArch64 整数常数强度削减 —— 集中归口
//
// 这里收拢 emitInstruction 里原本分散的"常量操作数 → 更便宜机器序列"逻辑：
// 乘 / 有符号除 / 有符号取余 / 加减立即数 / 按位逻辑 / GEP 地址。除数魔数分析仍
// 复用 magicNumber.hpp。每个 tryEmit* 返回是否已发射（false 表示交回通用路径）。
//
// 适用层级：
//   - 这些削减在 -O0 与 -O1 下都会运行（后端无论是否开启 regalloc 都自行降级）。
//   - 两处 Mul 分支（2^k+1 / 2^k-1）额外受 enableRegAlloc_ 门控，仅 -O1 生效；
//     -O0 下退回通用 mul。原因见各分支注释。
//   - -O1 时中端 InstCombine 往往已把 2 的幂乘除取余改写成移位/与，故这里的若干
//     分支在 -O1 主要作为兜底，在 -O0（中端关闭）才是主降级路径。
// ═══════════════════════════════════════════════════════════════════════════

// ── SDiv：常量除数强度削减（2 的幂偏置移位 / 非 2 幂魔数乘）──────────────────
bool Arm64FuncContext::tryEmitSDivConst(Instruction *inst, Value *v1, Value *v2) {
    auto *ci = dynamic_cast<ConstantInt *>(v2);
    if (!ci) return false;

    int32_t d = ci->value_;
    Magic::SignedDivisorInfo divisor = Magic::analyzeDivisor(d);
    if (!divisor.reducible) return false;

    // 正/负 2 的幂统一处理
    if (divisor.powerOfTwo) {
        int k = divisor.shift;
        std::string rNum = loadInt(v1);
        // rNum 的最后一次读和 rResult 的首次写在同一条指令上，
        // 因此 rResult 直接用分配寄存器即使与 rNum 同号也安全
        std::string rResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

        if (k == 1) {
            // ÷2:  barrel-shifter folds bias into add  (bias = rNum >>> 31)
            emitRawAluMachine("\tadd " + rResult + ", " + rNum + ", " + rNum + ", lsr #31",
                              rResult, {rNum});
        } else {
            std::string rTmp = allocIntReg();
            emitRawAluMachine("\tasr " + rTmp + ", " + rNum + ", #31", rTmp, {rNum});
            emitRawAluMachine("\tbic " + rTmp + ", " + rTmp + ", " + rTmp + ", lsl #" + std::to_string(k),
                              rTmp, {rTmp});
            emitBinaryMachine("add", rResult, rNum, rTmp);
        }
        emitRawAluMachine("\tasr " + rResult + ", " + rResult + ", #" + std::to_string(k),
                          rResult, {rResult});
        if (d < 0)
            emitUnaryMachine("neg", rResult, rResult);

        storeInt(inst, rResult);
        return true;
    }

    // 非 2 的幂除数：magic number 优化
    if (divisor.usesMagic()) {
        Magic::MagicNumber mag = Magic::getMagic(d);

        std::string wNum = loadInt(v1);
        std::string wMagic = intConstReg(mag.multiplier);

        std::string xTemp = allocAddrReg();
        std::string wHi = "w" + xTemp.substr(1);

        emitRawAluMachine("\tsmull " + xTemp + ", " + wNum + ", " + wMagic,
                          xTemp, {wNum, wMagic}, MOpcode::Mul, 3);

        if (mag.strat == Magic::MagicStrat::MULTIPLY_SHIFT) {
            // 取高 32 位与 >>shift 合并为一条 64 位 asr
            emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #" + std::to_string(32 + mag.shift),
                              xTemp, {xTemp});
        } else {
            emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #32", xTemp, {xTemp});
            if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT)
                emitBinaryMachine("add", wHi, wHi, wNum);
            else
                emitBinaryMachine("sub", wHi, wHi, wNum);
            emitRawAluMachine("\tasr " + wHi + ", " + wHi + ", #" + std::to_string(mag.shift),
                              wHi, {wHi});
        }
        // 末条指令同时完成 wNum 的最后一次读，可直接写入分配寄存器
        std::string wResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tadd " + wResult + ", " + wHi + ", " + wNum + ", lsr #31",
                          wResult, {wHi, wNum});

        storeInt(inst, wResult);
        return true;
    }

    return false;
}

// ── Mul：常量因数强度削减（覆盖 0、±1、±2^k、±(2^k+1)、±(2^k-1) 六族）────────
bool Arm64FuncContext::tryEmitMulConst(Instruction *inst, Value *v1, Value *v2) {
    // 乘法可交换——优先在 v2 找常量，找不到再看 v1
    ConstantInt *ci = dynamic_cast<ConstantInt *>(v2);
    Value *var = v1;
    if (!ci) { ci = dynamic_cast<ConstantInt *>(v1); var = v2; }
    if (!ci) return false;

    int32_t factor = ci->value_;

    if (factor == 0) {
        storeInt(inst, "wzr");
        return true;
    }
    if (factor == 1) {
        std::string r = loadInt(var);
        storeInt(inst, r);
        return true;
    }
    if (factor == -1) {
        // × -1：negate
        // 以下各情形 rd 的写入都不早于 r 的最后一次读（同指令内读先于写），
        // 因此 rd 直接用分配寄存器、与 r 同号也安全
        std::string r = loadInt(var);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitUnaryMachine("neg", rd, r);
        storeInt(inst, rd);
        return true;
    }
    if (factor == INT32_MIN) {
        std::string r = loadInt(var);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tlsl " + rd + ", " + r + ", #31", rd, {r});
        storeInt(inst, rd);
        return true;
    }

    bool negative = (factor < 0);
    uint32_t abs_f = negative ? (0u - static_cast<uint32_t>(factor))
                              : static_cast<uint32_t>(factor);

    // 情形 A：abs_f = 2^k    正：lsl；负：lsl + neg
    if ((abs_f & (abs_f - 1)) == 0) {
        int k = __builtin_ctz(abs_f);
        std::string r = loadInt(var);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tlsl " + rd + ", " + r + ", #" + std::to_string(k), rd, {r});
        if (negative)
            emitUnaryMachine("neg", rd, rd);
        storeInt(inst, rd);
        return true;
    }

    // 情形 B：abs_f = 2^k + 1（3,5,9,17,33…）正：add rd,r,r,lsl#k；负：再 neg
    // 仅 -O1：用到分配寄存器与 shifted-add 融合，-O0 退回通用 mul
    if (enableRegAlloc_ && (abs_f - 1) > 0 && (((abs_f - 1) & (abs_f - 2)) == 0)) {
        uint32_t m1 = abs_f - 1;
        int k = __builtin_ctz(m1);
        std::string r = loadInt(var);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tadd " + rd + ", " + r + ", " + r + ", lsl #" + std::to_string(k),
                          rd, {r});
        if (negative)
            emitUnaryMachine("neg", rd, rd);
        storeInt(inst, rd);
        return true;
    }

    // 情形 C：abs_f = 2^k - 1（3,7,15,31,63…）
    //   正：lsl tmp,r,#k ; sub rd,tmp,r   2 条
    //   负：r*(1-2^k) = r - r<<k → sub rd,r,r,lsl#k   1 条  ← 关键优化
    // 仅 -O1（同情形 B）
    if (enableRegAlloc_ && (abs_f + 1) > 0 && (((abs_f + 1) & abs_f) == 0)) {
        uint32_t p1 = abs_f + 1;
        int k = __builtin_ctz(p1);
        std::string r = loadInt(var);
        std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        if (negative) {
            emitRawAluMachine("\tsub " + rd + ", " + r + ", " + r + ", lsl #" + std::to_string(k),
                              rd, {r});
        } else {
            std::string rTmp = allocIntReg();
            emitRawAluMachine("\tlsl " + rTmp + ", " + r + ", #" + std::to_string(k), rTmp, {r});
            emitBinaryMachine("sub", rd, rTmp, r);
        }
        storeInt(inst, rd);
        return true;
    }

    // 其余常量：交回通用 mul
    return false;
}

// ── Add/Sub：立即数折叠，发射到调用方已分配的 rd，返回是否命中立即数形 ──
bool Arm64FuncContext::tryEmitAddSubImm(Instruction *inst, Value *v1, Value *v2,
                                        const std::string &rd) {
    // AArch64 add/sub immediate accepts imm12, optionally shifted left by 12.
    // A negative source constant is represented by swapping add and sub; keep
    // the magnitude in int64_t so INT_MIN never overflows during negation.
    auto emitImmediate = [&](const char *positiveOpcode, const char *negativeOpcode,
                             Value *source, int32_t value) -> bool {
        int64_t signedValue = static_cast<int64_t>(value);
        bool negative = signedValue < 0;
        uint64_t magnitude = static_cast<uint64_t>(negative ? -signedValue : signedValue);

        uint64_t encoded = magnitude;
        bool shifted = false;
        if (encoded > 4095) {
            if ((encoded & 0xfffU) != 0)
                return false;
            encoded >>= 12;
            shifted = true;
        }
        if (encoded > 4095)
            return false;

        std::string r = loadInt(source);
        std::string immediate = "#" + std::to_string(encoded);
        if (shifted)
            immediate += ", lsl #12";
        const char *opcode = negative ? negativeOpcode : positiveOpcode;
        emitMachineInstrLine("\t" + std::string(opcode) + " " + rd + ", " + r + ", " + immediate,
                             MOpcode::Alu, {rd}, {r});
        return true;
    };

    if (inst->op_id_ == Instruction::Add) {
        if (auto *ci = dynamic_cast<ConstantInt *>(v2)) {
            if (emitImmediate("add", "sub", v1, ci->value_))
                return true;
        }
        if (auto *ci = dynamic_cast<ConstantInt *>(v1)) {
            if (emitImmediate("add", "sub", v2, ci->value_))
                return true;
        }
    } else if (inst->op_id_ == Instruction::Sub) {
        // Skip immediate form when v1 is zero: sub rd, wzr, #imm is illegal
        // (ARM64 sub(immediate) uses the WSP encoding slot, WZR not allowed).
        if (!(dynamic_cast<ConstantInt *>(v1) && static_cast<ConstantInt *>(v1)->value_ == 0)) {
            if (auto *ci = dynamic_cast<ConstantInt *>(v2)) {
                if (emitImmediate("sub", "add", v1, ci->value_))
                    return true;
            }
        }
    }
    return false;
}

// ── SRem：常量除数取余（mag-1 → 0、mag-2 特例、2 的幂掩码、非 2 幂魔数 msub）────
bool Arm64FuncContext::tryEmitSRemConst(Instruction *inst, Value *v1, Value *v2) {
    auto *ci = dynamic_cast<ConstantInt *>(v2);
    if (!ci) return false;

    int32_t divisor = ci->value_;
    Magic::SignedDivisorInfo divisorInfo = Magic::analyzeDivisor(divisor);
    if (!divisorInfo.reducible) return false;

    if (divisorInfo.magnitude == 1) {
        storeInt(inst, "wzr");
        return true;
    }

    int32_t absDivisor = static_cast<int32_t>(divisorInfo.magnitude);

    if (absDivisor == 2) {
        std::string rNum = loadInt(v1);
        std::string rResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

        emitMachineInstrLine("\ttst " + rNum + ", " + rNum, MOpcode::Cmp, {}, {rNum}, 1, true);
        emitRawAluMachine("\tand " + rResult + ", " + rNum + ", #1", rResult, {rNum});
        emitMachineInstrLine("\tcneg " + rResult + ", " + rResult + ", mi",
                             MOpcode::FlagUse, {rResult}, {rResult}, 1, false, true);
        storeInt(inst, rResult);
        return true;
    }
    if (divisorInfo.powerOfTwo) {
        std::string rNum = loadInt(v1);
        std::string rResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();

        // |INT_MIN| wraps to INT_MIN, but masking a power-of-two remainder
        // still produces zero, so no special case is needed.
        emitMachineInstrLine("\tcmp " + rNum + ", #0", MOpcode::Cmp, {}, {rNum}, 1, true);
        emitMachineInstrLine("\tcneg " + rResult + ", " + rNum + ", mi",
                             MOpcode::FlagUse, {rResult}, {rNum}, 1, false, true);
        emitRawAluMachine("\tand " + rResult + ", " + rResult + ", #" + std::to_string(absDivisor - 1),
                          rResult, {rResult});
        emitMachineInstrLine("\tcneg " + rResult + ", " + rResult + ", mi",
                             MOpcode::FlagUse, {rResult}, {rResult}, 1, false, true);
        storeInt(inst, rResult);
        return true;
    }
    if (divisorInfo.usesMagic()) {
        Magic::MagicNumber mag = Magic::getMagic(absDivisor);
        std::string wNum = loadInt(v1);
        std::string wMagic = intConstReg(mag.multiplier);
        std::string xTemp = allocAddrReg();
        std::string wHi = "w" + xTemp.substr(1);

        emitRawAluMachine("\tsmull " + xTemp + ", " + wNum + ", " + wMagic,
                          xTemp, {wNum, wMagic}, MOpcode::Mul, 3);

        if (mag.strat == Magic::MagicStrat::MULTIPLY_SHIFT) {
            emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #" + std::to_string(32 + mag.shift),
                              xTemp, {xTemp});
        } else {
            emitRawAluMachine("\tasr " + xTemp + ", " + xTemp + ", #32", xTemp, {xTemp});
            if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT)
                emitBinaryMachine("add", wHi, wHi, wNum);
            else
                emitBinaryMachine("sub", wHi, wHi, wNum);
            emitRawAluMachine("\tasr " + wHi + ", " + wHi + ", #" + std::to_string(mag.shift),
                              wHi, {wHi});
        }
        emitRawAluMachine("\tadd " + wHi + ", " + wHi + ", " + wNum + ", lsr #31",
                          wHi, {wHi, wNum});

        std::string wD = intConstReg(absDivisor);
        std::string wResult = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
        emitRawAluMachine("\tmsub " + wResult + ", " + wHi + ", " + wD + ", " + wNum,
                          wResult, {wHi, wD, wNum}, MOpcode::Mul, 3);
        storeInt(inst, wResult);
        return true;
    }

    return false;
}

// ── And/Or/Xor：常量短路（0 / 全 1）与 logical-immediate 折叠 ─────────────────
bool Arm64FuncContext::tryEmitLogicalConst(Instruction *inst, Value *v1, Value *v2) {
    ConstantInt *ci = dynamic_cast<ConstantInt *>(v2);
    Value *var = v1;
    if (!ci) { ci = dynamic_cast<ConstantInt *>(v1); var = v2; }
    if (!ci) return false;

    const char *opcode;
    if (inst->op_id_ == Instruction::And)     opcode = "and";
    else if (inst->op_id_ == Instruction::Or) opcode = "orr";
    else                                      opcode = "eor";

    uint32_t imm = static_cast<uint32_t>(ci->value_);

    if (inst->op_id_ == Instruction::And && imm == 0) {
        storeInt(inst, "wzr");
        return true;
    }
    if (inst->op_id_ == Instruction::And && imm == UINT32_MAX) {
        std::string r = loadInt(var);
        storeInt(inst, r);
        return true;
    }
    if ((inst->op_id_ == Instruction::Or || inst->op_id_ == Instruction::Xor) && imm == 0) {
        std::string r = loadInt(var);
        storeInt(inst, r);
        return true;
    }
    if (inst->op_id_ == Instruction::Or && imm == UINT32_MAX) {
        std::string allOnes = intConstReg(ci->value_);
        storeInt(inst, allOnes);
        return true;
    }

    std::string r = loadInt(var);
    std::string rd = hasAssignedReg(inst) ? assignedReg(inst) : allocIntReg();
    if (isLogicalImm32(imm)) {
        emitRawAluMachine("\t" + std::string(opcode) + " " + rd + ", " + r + ", #" + std::to_string(imm),
                          rd, {r});
    } else {
        std::string r2 = intConstReg(ci->value_);
        emitBinaryMachine(opcode, rd, r, r2);
    }
    storeInt(inst, rd);
    return true;
}

// ── GEP：单层下标的常量驱动地址削减，原地累加到 addr ────────────────────────
//   常量下标 → 折叠为 add/sub #imm（越界则物化进 x17）
//   变量下标 + 常量 elemSize：2 的幂 → shifted-register add（含 extend）；
//                             非 2 幂 → 物化 elemSize 后 mul 缩放再 add
void Arm64FuncContext::emitGepIndexStep(std::string &addr, Value *idx, int elemSize,
                                        Instruction *inst) {
    if (auto *ci = dynamic_cast<ConstantInt *>(idx)) {
        int64_t offset =
            static_cast<int64_t>(ci->value_) * elemSize;
        if (offset != 0) {
            if (offset > 0 && offset <= 4095) {
                emitRawAluMachine("\tadd " + addr + ", " + addr + ", #" + std::to_string(offset),
                                  addr, {addr});
            } else if (offset < 0 && -offset <= 4095) {
                emitRawAluMachine("\tsub " + addr + ", " + addr + ", #" + std::to_string(-offset),
                                  addr, {addr});
            } else if (static_cast<uint64_t>(
                           offset < 0 ? -offset : offset) <=
                       (static_cast<uint64_t>(4095) << 12) + 4095) {
                const uint64_t magnitude =
                    static_cast<uint64_t>(
                        offset < 0 ? -offset : offset);
                const uint64_t high = magnitude >> 12;
                const uint64_t low = magnitude & 0xfffU;
                const char *opcode = offset > 0 ? "add" : "sub";
                if (high)
                    emitRawAluMachine(
                        "\t" + std::string(opcode) + " " +
                            addr + ", " + addr + ", #" +
                            std::to_string(high) +
                            ", lsl #12",
                        addr, {addr});
                if (low)
                    emitRawAluMachine(
                        "\t" + std::string(opcode) + " " +
                            addr + ", " + addr + ", #" +
                            std::to_string(low),
                        addr, {addr});
            } else {
                emitIntConst(
                    static_cast<int>(offset < 0 ? -offset : offset),
                    "x17");
                if (offset > 0)
                    emitBinaryMachine("add", addr, addr, "x17");
                else
                    emitBinaryMachine("sub", addr, addr, "x17");
            }
        }
    } else {
        std::string idxReg = loadInt(idx);
        auto isPowerOfTwo = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
        auto log2Int = [](int n) {
            int shift = 0;
            while ((1 << shift) < n) shift++;
            return shift;
        };

        if (elemSize > 1) {
            if (isPowerOfTwo(elemSize)) {
                int shift = log2Int(elemSize);
                if (shift <= 4) {
                    std::string ext = indexExtendOpcode(idx, inst->parent_);
                    if (shift > 0)
                        ext += " #" + std::to_string(shift);
                    emitRawAluMachine("\tadd " + addr + ", " + addr + ", " + idxReg + ", " + ext,
                                      addr, {addr, idxReg});
                } else {
                    std::string scaled = allocAddrReg();
                    emitMoveMachine(scaled, idxReg, indexExtendOpcode(idx, inst->parent_));
                    emitRawAluMachine("\tadd " + addr + ", " + addr + ", " + scaled
                                          + ", lsl #" + std::to_string(shift),
                                      addr, {addr, scaled});
                    freeAddrReg(scaled);
                }
            } else {
                std::string scaled = allocAddrReg();
                emitMoveMachine(scaled, idxReg, indexExtendOpcode(idx, inst->parent_));
                std::string elemReg = allocAddrReg();
                emitIntConst(elemSize, elemReg);
                emitBinaryMachine("mul", scaled, scaled, elemReg, MOpcode::Mul, 3);
                emitBinaryMachine("add", addr, addr, scaled);
                freeAddrReg(elemReg);
                freeAddrReg(scaled);  // scaled 可以释放了，因为结果已累加到 addr
            }
        } else {
            emitRawAluMachine("\tadd " + addr + ", " + addr + ", "
                                  + idxReg + ", " + indexExtendOpcode(idx, inst->parent_),
                              addr, {addr, idxReg});
        }
        freeIntReg(idxReg);
    }
}
