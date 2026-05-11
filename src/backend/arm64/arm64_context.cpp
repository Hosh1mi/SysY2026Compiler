#include "../../include/backend/arm64/arm64_context.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"
#include "../../include/mid/ir/ir.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>

// ---- helpers ----
static int typeSize(Type *ty) {
    switch (ty->tid_) {
    case Type::IntegerTyID: return 4;
    case Type::FloatTyID:   return 4;
    case Type::PointerTyID: return 8;
    case Type::ArrayTyID:
        return static_cast<ArrayType*>(ty)->num_elements_ *
               typeSize(static_cast<ArrayType*>(ty)->contained_);
    default: return 8;
    }
}

static bool isFloat(Type *ty)  { return ty->tid_ == Type::FloatTyID; }
static bool isInt(Type *ty)    { return ty->tid_ == Type::IntegerTyID; }
static bool isPtr(Type *ty)    { return ty->tid_ == Type::PointerTyID; }
static bool isVoid(Type *ty)   { return ty->tid_ == Type::VoidTyID; }
static bool isLabel(Type *ty)  { return ty->tid_ == Type::LabelTyID; }

static int align16(int n) { return (n + 15) & ~15; }

static bool isAllocatableIntValue(Type *ty) {
    return ty->tid_ == Type::IntegerTyID;
}

static bool isAllocatableFloatValue(Type *ty) {
    return ty->tid_ == Type::FloatTyID;
}

static std::vector<int> collectAssignedIntRegs(const std::map<Value*, std::string> &assignedRegs) {
    std::set<int> regs;
    for (const auto &entry : assignedRegs) {
        const std::string &reg = entry.second;
        if (!reg.empty() && reg[0] == 'w') regs.insert(std::stoi(reg.substr(1)));
    }
    return std::vector<int>(regs.begin(), regs.end());
}

static std::vector<int> collectAssignedFloatRegs(const std::map<Value*, std::string> &assignedRegs) {
    std::set<int> regs;
    for (const auto &entry : assignedRegs) {
        const std::string &reg = entry.second;
        if (!reg.empty() && reg[0] == 's') regs.insert(std::stoi(reg.substr(1)));
    }
    return std::vector<int>(regs.begin(), regs.end());
}

// emit str/ldr with potentially large negative offset
static void emitStoreReg(std::ostream &os, const std::string &reg, int off) {
    if (off >= -256 && off <= 255) {
        os << "\tstr " << reg << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        // 使用 x16 作为临时基址寄存器，避免覆盖可能为 x17 的源寄存器
        std::string base = (reg == "x17") ? "x16" : "x17";
        if (pos <= 4095) {
            os << "\tsub " << base << ", x29, #" << pos << "\n";
        } else {
            os << "\tmovz " << base << ", #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk " << base << ", #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub " << base << ", x29, " << base << "\n";
        }
        os << "\tstr " << reg << ", [" << base << "]\n";
    }
}

static void emitLoadReg(std::ostream &os, const std::string &reg, int off) {
    if (off >= -256 && off <= 255) {
        os << "\tldr " << reg << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        // 使用 x16 作为临时基址寄存器，避免覆盖可能为 x17 的目标寄存器
        std::string base = (reg == "x17") ? "x16" : "x17";
        if (pos <= 4095) {
            os << "\tsub " << base << ", x29, #" << pos << "\n";
        } else {
            os << "\tmovz " << base << ", #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk " << base << ", #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub " << base << ", x29, " << base << "\n";
        }
        os << "\tldr " << reg << ", [" << base << "]\n";
    }
}

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

    preparePhi();
    allocateLinearScanRegisters();

    emitPrologue();
    for (auto bb : func_->basic_blocks_) {
        emitBlock(bb);
    }
    emitEpilogue();
}

void Arm64FuncContext::emitPrologue() {
    os_ << "\t.global " << func_->name_ << "\n";
    os_ << "\t.p2align 2\n";
    os_ << func_->name_ << ":\n";

    // allocate slots for arguments that were not promoted to registers
    for (auto arg : func_->arguments_) {
        getSlot(arg);
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
                getSlot(inst);
            }
        }
    }

    auto savedIntRegs = collectAssignedIntRegs(assignedRegs_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size()) * 8;
    int localSize = align16(frameSize_ + savedRegBytes);
    int saveOffset = -frameSize_;

    // stp supports only -512..504 range; use minimal stp + sub for large frames
    os_ << "\tstp x29, x30, [sp, #-16]!\n";
    os_ << "\tmov x29, sp\n";
    if (localSize > 0) {
        if (localSize <= 4095) {
            os_ << "\tsub sp, sp, #" << localSize << "\n";
        } else {
            os_ << "\tmovz x17, #" << (localSize & 0xFFFF) << "\n";
            os_ << "\tmovk x17, #" << ((localSize >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tsub sp, sp, x17\n";
        }
    }

    for (int reg : savedIntRegs) {
        saveOffset -= 8;
        emitStoreReg(os_, "x" + std::to_string(reg), saveOffset);
    }
    for (int reg : savedFloatRegs) {
        saveOffset -= 8;
        emitStoreReg(os_, "d" + std::to_string(reg), saveOffset);
    }

   // ----- load arguments (including register arguments and stack arguments) -----
   int intRegIdx = 0;     // x0-x7 / w0-w7
   int floatRegIdx = 0;   // s0-s7
   int stackOffset = 0;   // stack argument offset (relative to x29+16)

   for (auto arg : func_->arguments_) {
        if (isFloat(arg->type_)) {
            if (floatRegIdx < 8) {
                std::string src = "s" + std::to_string(floatRegIdx++);
                int slot = getSlot(arg);
                emitStoreReg(os_, src, slot);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg);
                    if (dst != src) os_ << "\tfmov " << dst << ", " << src << "\n";
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
                    std::string dst = assignedReg(arg);
                    os_ << "\tfmov " << dst << ", s17\n";   // 浮点参数用 s17
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
                int slot = getSlot(arg);
                emitStoreReg(os_, reg, slot);
                if (hasAssignedReg(arg)) {
                    std::string dst = assignedReg(arg, isPtr);
                    if (dst != reg) os_ << "\tmov " << dst << ", " << reg << "\n";
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
    os_ << ".L" << func_->name_ << "_epilogue:\n";

    auto savedIntRegs = collectAssignedIntRegs(assignedRegs_);
    auto savedFloatRegs = collectAssignedFloatRegs(assignedRegs_);
    int savedRegBytes = static_cast<int>(savedIntRegs.size() + savedFloatRegs.size()) * 8;
    int localSize = align16(frameSize_ + savedRegBytes);
    int restoreOffset = -frameSize_;

    for (int reg : savedIntRegs) {
        restoreOffset -= 8;
        emitLoadReg(os_, "x" + std::to_string(reg), restoreOffset);
    }
    for (int reg : savedFloatRegs) {
        restoreOffset -= 8;
        emitLoadReg(os_, "d" + std::to_string(reg), restoreOffset);
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

static std::string bbLabel(Function *f, BasicBlock *bb) {
    return f->name_ + "_" + bb->name_;
}

void Arm64FuncContext::emitBlock(BasicBlock *bb) {
    os_ << bbLabel(func_, bb) << ":\n";
    resetRegs();

    for (auto inst : bb->instr_list_) {
        if (!inst->is_phi()) {
            emitInstruction(inst);
        }
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
        break;
    }

    // ---- Load ----
    case Instruction::Load: {
        auto ptr = inst->get_operand(0);
        std::string addr = loadAddr(ptr);
        if (isFloat(inst->type_)) {
            std::string r = allocFloatReg();
            os_ << "\tldr " << r << ", [" << addr << "]\n";
            storeFloat(inst, r);
        } else if (isPtr(inst->type_)) {
            std::string r = allocAddrReg();
            os_ << "\tldr " << r << ", [" << addr << "]\n";
            storeAddr(inst, r);
        } else {
            std::string r = allocIntReg();
            os_ << "\tldr " << r << ", [" << addr << "]\n";
            storeInt(inst, r);
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
        std::string r1 = loadInt(v1);
        std::string r2 = loadInt(v2);
        std::string rd = allocIntReg();
        const char *opcode = nullptr;
        switch (inst->op_id_) {
        case Instruction::Add: opcode = "add"; break;
        case Instruction::Sub: opcode = "sub"; break;
        case Instruction::Mul: opcode = "mul"; break;
        case Instruction::SDiv: opcode = "sdiv"; break;
        default: break;
        }
        os_ << "\t" << opcode << " " << rd << ", " << r1 << ", " << r2 << "\n";
        storeInt(inst, rd);
        break;
    }

    // ---- SRem: a % b = a - (a/b) * b ----
    case Instruction::SRem: {
        auto v1 = inst->get_operand(0);
        auto v2 = inst->get_operand(1);
        // std::string ra = loadInt(v1);
        // std::string rb = loadInt(v2);
        // std::string rq = allocIntReg();
        // std::string rr = allocIntReg();
        // os_ << "\tsdiv " << rq << ", " << ra << ", " << rb << "\n";
        // os_ << "\tmsub " << rr << ", " << rq << ", " << rb << ", " << ra << "\n";
        // storeInt(inst, rr);
        // 检查第二个操作数是否是常量
        if (auto ci = dynamic_cast<ConstantInt*>(v2)) {
            int32_t divisor = ci->value_;

            if (divisor == 0) { /* Fallback */ }
            // ---- 除数为 1，余数恒为 0 ----
            else if (divisor == 1 || divisor == -1) {
                std::string rd = allocIntReg();
                os_ << "\tmov " << rd << ", wzr\n";
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
            // Divisors below 8 are gueranteed to be correct
            // TODO: Enable divisors above 8
            // And **I HATE MATH**
            else if (divisor > 1 && divisor < 8) {
                unsigned magic;
                unsigned shift;
                bool negMagic;
                GetSignedMagic(divisor, magic, shift, negMagic);

                std::string wNum = loadInt(v1);          // 分子
                std::string wMagic = allocIntReg();
                emitIntConst(static_cast<int>(magic), wMagic);

                // 分配一个 64 位寄存器，确保不与已用 wNum / wMagic 冲突
                // 安全做法：先保存分子到另一个寄存器，避免被 smull 目标覆盖
                std::string wNumSafe = allocIntReg();
                os_ << "\tmov " << wNumSafe << ", " << wNum << "\n";

                // 显式分配 xTemp，其低 32 位即 wTemp = "w" + xTemp.substr(1)
                std::string xTemp = allocAddrReg();      // 64-bit
                std::string wHi = "w" + xTemp.substr(1); // 32-bit 视图

                os_ << "\tsmull " << xTemp << ", " << wNumSafe << ", " << wMagic << "\n";
                os_ << "\tasr " << xTemp << ", " << xTemp << ", #32\n";

                if (negMagic) {
                    os_ << "\tadd " << wHi << ", " << wHi << ", " << wNumSafe << "\n";
                } else {
                    os_ << "\tadd " << wHi << ", " << wHi << ", " << wNumSafe << ", lsr #31\n";
                }

                os_ << "\tasr " << wHi << ", " << wHi << ", #" << shift << "\n";

                // 余数 = num - q * divisor
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
        std::string r1 = loadInt(v1);
        std::string r2 = loadInt(v2);
        std::string rd = allocIntReg();
        os_ << "\tcmp " << r1 << ", " << r2 << "\n";
        os_ << "\tcset " << rd << ", " << icmpCond(icmp->icmp_op_) << "\n";
        storeInt(inst, rd);
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
        std::string addr = loadAddr(ptr);          // addr 是基址寄存器
    
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
        // emit PHI copies before the branch
        emitPhiCopies(inst->parent_);

        if (inst->num_ops_ == 1) {
            auto target = static_cast<BasicBlock*>(inst->get_operand(0));
            os_ << "\tb " << bbLabel(func_, target) << "\n";
        } else {
            auto cond = inst->get_operand(0);
            auto trueBB = static_cast<BasicBlock*>(inst->get_operand(1));
            auto falseBB = static_cast<BasicBlock*>(inst->get_operand(2));
            std::string cr = loadInt(cond);
            os_ << "\tcbnz " << cr << ", " << bbLabel(func_, trueBB) << "\n";
            os_ << "\tb " << bbLabel(func_, falseBB) << "\n";
        }
        break;
    }

    // ---- Ret ----
    case Instruction::Ret: {
        if (inst->num_ops_ > 0) {
            auto val = inst->get_operand(0);
            if (isFloat(val->type_)) {
                std::string r = loadFloat(val);
                os_ << "\tfmov s0, " << r << "\n";
            } else {
                std::string r = loadInt(val);
                os_ << "\tmov w0, " << r << "\n";
            }
        }
        os_ << "\tb .L" << func_->name_ << "_epilogue\n";
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
                    for (int off = 0; off < bytes; off += 4) {
                        if (off == 0)
                            os_ << "\tstr wzr, [" << addr << "]\n";
                        else
                            os_ << "\tstr wzr, [" << addr << ", #" << off << "]\n";
                    }
                    useLoop = false;
                }
            }
            if (useLoop) {
                // Fix 2: use member variable instead of static (thread-safe)
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
    
        // ================================================
        // 1. 计算参数分配信息
        // ================================================
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
    
        // ================================================
        // Fix 5: w19-w28 / d8-d15 are callee-saved per AArch64 ABI.
        // The callee guarantees to preserve them, so we must NOT
        // save/restore them around a bl.  The prologue/epilogue already
        // handle them for our own frame.
        // ================================================

        // ================================================
        // 3. 分配栈参数空间
        // ================================================
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                os_ << "\tsub sp, sp, #" << stackBytes << "\n";
            else {
                os_ << "\tmovz x17, #" << (stackBytes & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((stackBytes >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tsub sp, sp, x17\n";
            }
        }
    
        // ================================================
        // 5. 传递参数 (寄存器 + 栈)
        // ================================================
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
    
        // ================================================
        // 6. 执行调用
        // ================================================
        os_ << "\tbl " << callee->name_ << "\n";
    
        // ================================================
        // 7. 回收栈参数空间
        // ================================================
        if (stackBytes > 0) {
            if (stackBytes <= 4095)
                os_ << "\tadd sp, sp, #" << stackBytes << "\n";
            else {
                os_ << "\tmovz x17, #" << (stackBytes & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((stackBytes >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tadd sp, sp, x17\n";
            }
        }
    
        // ================================================
        // 8. 处理返回值  (Fix 5: step 8 "restore callee-saved" removed)
        // ================================================
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

// ---- linear-scan register allocation ----

bool Arm64FuncContext::canAssignRegister(Value *v) const {
    if (!v || dynamic_cast<Constant*>(v) || dynamic_cast<GlobalVariable*>(v)) return false;
    if (auto inst = dynamic_cast<Instruction*>(v)) {
        if (inst->is_void() || inst->is_alloca() || inst->is_phi() ||
            inst->op_id_ == Instruction::BitCast ) {
            return false;
        }
    }
    return isAllocatableIntValue(v->type_) || isAllocatableFloatValue(v->type_);
}

void Arm64FuncContext::allocateLinearScanRegisters() {
    struct Interval { Value *value; int start; int end; bool isFloat; };
    std::map<Value*, int> defPos;
    std::map<Value*, int> lastUse;
    std::vector<Interval> intervals;

    // ---- 1. 构建 CFG 前驱关系 ----
    std::map<BasicBlock*, std::vector<BasicBlock*>> preds;
    std::vector<BasicBlock*> blocksOrder;        // 保持原始遍历顺序（用于线性编号）
    for (auto bb : func_->basic_blocks_) {
        blocksOrder.push_back(bb);
        auto term = bb->get_terminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->num_ops_; ++i) {
            if (auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                preds[succ].push_back(bb);
            }
        }
    }

    // ---- 2. 为每条指令分配线性序号，同时收集 Def/Use 信息 ----
    // 指令编号从 1 开始，0 保留给函数参数的定义
    std::map<BasicBlock*, int> blockStart, blockEnd;   // 块内第一条和最后一条指令的编号
    std::map<Instruction*, int> instIdx;                 // 指令编号

    int idx = 0;
    // 为参数分配 def 在 0 号位置
    for (auto arg : func_->arguments_) {
        if (canAssignRegister(arg)) {
            defPos[arg] = 0;
            lastUse[arg] = 0;        // 初始为 0，后续由使用处更新
        }
    }

    for (auto bb : blocksOrder) {
        if (bb->instr_list_.empty()) {
            blockStart[bb] = blockEnd[bb] = idx;
            continue;
        }

        blockStart[bb] = idx + 1;    // 第一条指令将要占据 idx+1
        for (auto inst : bb->instr_list_) {
            ++idx;
            instIdx[inst] = idx;

            // 如果指令产生值且可分配寄存器，记录其定义位置
            if (canAssignRegister(inst)) {
                defPos[inst] = idx;
                lastUse[inst] = idx;          
            }

            // 遍历指令的操作数，更新 lastUse
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val)) {
                    lastUse[val] = std::max(lastUse[val], idx);
                }
            }
        }
        blockEnd[bb] = idx;
    }

    // ---- 3. 数据流分析：计算每个块的 LiveIn / LiveOut ----
    // 收集 PHI 出口活跃信息
     std::map<BasicBlock*, std::set<Value*>> phiOut;
     for (auto bb : blocksOrder) {
         for (auto inst : bb->instr_list_) {
             auto phi = dynamic_cast<PhiInst*>(inst);
             if (!phi) continue;
             for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                 auto val = phi->get_operand(i);
                 auto pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                 if (canAssignRegister(val)) {
                     phiOut[pred].insert(val);
                 }
             }
         }
     }

    struct BBInfo {
        std::set<Value*> def, use;
    };
    std::map<BasicBlock*, BBInfo> bbInfo;

    for (auto bb : blocksOrder) {
        BBInfo info;
        for (auto inst : bb->instr_list_) {
            // 处理 phi 指令：左值加入 def，右值加入对应前驱的 use（稍后单独处理）
            if (auto phi = dynamic_cast<PhiInst*>(inst)) {
                if (canAssignRegister(phi)) info.def.insert(phi);
                for (unsigned i = 0; i < phi->num_ops_; i += 2) {
                    auto val = phi->get_operand(i);
                    auto pred = static_cast<BasicBlock*>(phi->get_operand(i + 1));
                    // 将 val 加入 pred 块的 use 集合（后续会反映到 pred 的 LiveOut）
                    bbInfo[pred].use.insert(val);
                }
                continue;
            }

            // 非 phi 指令
            if (canAssignRegister(inst)) info.def.insert(inst);
            for (unsigned i = 0; i < inst->num_ops_; ++i) {
                auto val = inst->get_operand(i);
                if (canAssignRegister(val) && !info.def.count(val)) {
                    info.use.insert(val);      // upward exposed use
                }
            }
        }
        bbInfo[bb] = info;
    }

    // 迭代计算 LiveIn / LiveOut
    bool changed;
    std::map<BasicBlock*, std::set<Value*>> liveIn, liveOut;
    do {
        changed = false;
        for (auto bb : blocksOrder) {
            std::set<Value*> newIn;

            // LiveOut = 所有后继 LiveIn 的并集
            std::set<Value*> newOut;
            auto term = bb->get_terminator();
            if (term) {
                for (unsigned i = 0; i < term->num_ops_; ++i) {
                    if (auto succ = dynamic_cast<BasicBlock*>(term->get_operand(i))) {
                        for (auto v : liveIn[succ]) newOut.insert(v);
                    }
                }
            }
            for (auto v : phiOut[bb]) {
                newOut.insert(v);
            }
            
            // LiveIn = use ∪ (LiveOut - def)
            auto &info = bbInfo[bb];
            for (auto v : info.use) newIn.insert(v);
            for (auto v : newOut) {
                if (!info.def.count(v)) newIn.insert(v);
            }

            if (newIn != liveIn[bb] || newOut != liveOut[bb]) changed = true;
            liveIn[bb] = std::move(newIn);
            liveOut[bb] = std::move(newOut);
        }
    } while (changed);

    // ---- 4. 构建精确的活跃区间 ----
    // 对于每个可分配寄存器的值，其区间需要从 def 开始，到所有使用（包括因 LiveOut 而隐含的使用）结束
    for (auto &entry : defPos) {
        Value *v = entry.first;
        int start = entry.second;

        // 从 lastUse 获取最大使用指令编号
        int end = lastUse[v];

        // 扩展：如果 v 在某块的 LiveOut 中，那么它的活跃期至少要覆盖到该块的结束位置
        for (auto bb : blocksOrder) {
            if (liveOut[bb].count(v)) {
                end = std::max(end, blockEnd[bb]);
            }
        }

        // 对于参数，如果有使用且 end 仍然为 0，则强制扩展到整个函数（保守处理）
        if (start == 0 && end == 0 && dynamic_cast<Argument*>(v)) {
            // 参数但未被纳入 lastUse，可能是死参数，可忽略
            continue;
        }

        if (end >= start) {
            intervals.push_back({v, start, end, isAllocatableFloatValue(v->type_)});
        }
    }

    // ---- 5. 区间排序 ----
    std::sort(intervals.begin(), intervals.end(), [](const Interval &a, const Interval &b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });

    // ---- 6. 原有的线性扫描分配（保持不变） ----
    auto scanKind = [&](bool floats) {
        std::vector<Interval> active;
        std::set<int> freeRegs;
        int first = floats ? 8 : 19;
        int last = floats ? 15 : 28;
        for (int r = first; r <= last; ++r) freeRegs.insert(r);

        for (const auto &iv : intervals) {
            if (iv.isFloat != floats) continue;
            // 清理已结束的区间
            for (auto it = active.begin(); it != active.end();) {
                if (it->end < iv.start) {
                    std::string reg = assignedRegs_[it->value];
                    freeRegs.insert(std::stoi(reg.substr(1)));
                    it = active.erase(it);
                } else {
                    ++it;
                }
            }
            if (freeRegs.empty()) continue;
            int regNo = *freeRegs.begin();
            freeRegs.erase(freeRegs.begin());
            assignedRegs_[iv.value] = std::string(floats ? "s" : "w") + std::to_string(regNo);
            active.push_back(iv);
            std::sort(active.begin(), active.end(), [](const Interval &a, const Interval &b) {
                return a.end < b.end;
            });
        }
    };

    scanKind(false);
    scanKind(true);
}

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
    } else {
        size = 8; // keep SSA/temp slots naturally aligned
    }

    frameSize_ += size;
    int offset = -frameSize_;
    slots_[v] = offset;
    return offset;
}

bool Arm64FuncContext::hasSlot(Value *v) const {
    return slots_.count(v) > 0;
}

// ---- scratch register pool ----

void Arm64FuncContext::resetRegs() {
    usedIntRegs_.clear();
    usedFloatRegs_.clear();
}

void Arm64FuncContext::freeIntReg(const std::string &reg) {
    if (reg.size() >= 2 && reg[0] == 'w') {
        int num = std::stoi(reg.substr(1));
        usedIntRegs_.erase(num);
    }
}

void Arm64FuncContext::freeAddrReg(const std::string& reg) {
    if (reg.size() >= 2 && reg[0] == 'x') {
        int num = std::stoi(reg.substr(1));
        usedIntRegs_.erase(num);
    }
}

std::string Arm64FuncContext::allocIntReg() {
    for (int r = 9; r <= 15; r++) {
        if (!usedIntRegs_.count(r)) {
            usedIntRegs_.insert(r);
            return "w" + std::to_string(r);
        }
    }
    usedIntRegs_.insert(16);
    return "w16";
}

std::string Arm64FuncContext::allocFloatReg() {
    // 临时浮点寄存器池与持久寄存器池完全分离
    // 持久池：s8 ~ s15（由线性扫描分配，保存在 assigndRegs_ 中）
    // 临时池：s16 ~ s31（仅在本指令内使用，由 usedFloatRegs_ 管理）
    for (int r = 16; r <= 31; ++r) {
        if (!usedFloatRegs_.count(r)) {
            usedFloatRegs_.insert(r);
            return "s" + std::to_string(r);
        }
    }
    // 极端情况：16 个临时寄存器全部被占用，回退到 s16
    // （实际在单条指令内几乎不可能发生）
    usedFloatRegs_.insert(16);
    return "s16";
}

std::string Arm64FuncContext::allocAddrReg() {
    for (int r = 9; r <= 15; r++) {
        if (!usedIntRegs_.count(r)) {
            usedIntRegs_.insert(r);
            return "x" + std::to_string(r);
        }
    }
    // return "x9";
    usedIntRegs_.insert(16);
    return "x16"; // highly improbable but use x16 as a temporary register when x9-x15 are all occupied
}

// ---- load from slot/constant/global to scratch register ----

std::string Arm64FuncContext::loadInt(Value *v) {
    if (auto ci = dynamic_cast<ConstantInt*>(v)) {
        std::string r = allocIntReg();
        emitIntConst(ci->value_, r);
        return r;
    }
    if (hasAssignedReg(v)) return assignedReg(v);
    std::string r = allocIntReg();
    int off = getSlot(v);
    emitLoadReg(os_, r, off);
    return r;
}

std::string Arm64FuncContext::loadFloat(Value *v) {
    if (auto cf = dynamic_cast<ConstantFloat*>(v)) {
        std::string r = allocFloatReg();
        emitFloatConst(cf->value_, r);
        return r;
    }
    if (hasAssignedReg(v)) return assignedReg(v);
    std::string r = allocFloatReg();
    int off = getSlot(v);
    emitLoadReg(os_, r, off);
    return r;
}

std::string Arm64FuncContext::loadAddr(Value *v) {
    if (auto gv = dynamic_cast<GlobalVariable*>(v)) {
        std::string r = allocAddrReg();
        emitGlobalAddr(gv, r);
        return r;
    }
    if (auto ci = dynamic_cast<ConstantInt*>(v)) {
        std::string r = allocAddrReg();
        os_ << "\tmov " << r << ", #" << ci->value_ << "\n";
        return r;
    }
    if (hasAssignedReg(v)) return assignedReg(v, true);
    // AllocaInst: the "value" is the address of its stack space
    if (dynamic_cast<AllocaInst*>(v)) {
        std::string r = allocAddrReg();
        int off = getSlot(v);
        if (off < 0) {
            int absOff = -off;
            if (absOff <= 4095) {
                os_ << "\tsub " << r << ", x29, #" << absOff << "\n";
            } else {
                os_ << "\tmovz x17, #" << (absOff & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((absOff >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tsub " << r << ", x29, x17\n";
            }
        } else {
            if (off <= 4095) {
                os_ << "\tadd " << r << ", x29, #" << off << "\n";
            } else {
                os_ << "\tmovz x17, #" << (off & 0xFFFF) << "\n";
                os_ << "\tmovk x17, #" << ((off >> 16) & 0xFFFF) << ", lsl #16\n";
                os_ << "\tadd " << r << ", x29, x17\n";
            }
        }
        return r;
    }
    std::string r = allocAddrReg();
    int off = getSlot(v);
    emitLoadReg(os_, r, off);
    return r;
}

// ---- store from register to slot ----

void Arm64FuncContext::storeInt(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) os_ << "\tmov " << target << ", " << reg << "\n";
        return;
    }
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

void Arm64FuncContext::storeFloat(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v);
        if (target != reg) os_ << "\tfmov " << target << ", " << reg << "\n";
        return;
    }
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

void Arm64FuncContext::storeAddr(Value *v, const std::string &reg) {
    if (hasAssignedReg(v)) {
        std::string target = assignedReg(v, true);
        if (target != reg) os_ << "\tmov " << target << ", " << reg << "\n";
        return;
    }
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

// ---- constants ----

void Arm64FuncContext::emitIntConst(int val, const std::string &reg) {
    uint32_t u = (uint32_t)val;
    os_ << "\tmovz " << reg << ", #" << (u & 0xFFFF) << "\n";
    if ((u >> 16) & 0xFFFF) {
        os_ << "\tmovk " << reg << ", #" << ((u >> 16) & 0xFFFF) << ", lsl #16\n";
    }
}

void Arm64FuncContext::emitFloatConst(float val, const std::string &reg) {
    int bits;
    std::memcpy(&bits, &val, sizeof(bits));
    uint32_t u = (uint32_t)bits;
    std::string tmp = allocIntReg();
    os_ << "\tmovz " << tmp << ", #" << (u & 0xFFFF) << "\n";
    if ((u >> 16) & 0xFFFF) {
        os_ << "\tmovk " << tmp << ", #" << ((u >> 16) & 0xFFFF) << ", lsl #16\n";
    }
    os_ << "\tfmov " << reg << ", " << tmp << "\n";
}

void Arm64FuncContext::emitGlobalAddr(GlobalVariable *gv, const std::string &reg) {
    os_ << "\tadrp " << reg << ", " << gv->name_ << "\n";
    os_ << "\tadd " << reg << ", " << reg << ", :lo12:" << gv->name_ << "\n";
}

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
                phiCopies_.push_back({predBB, {val, phiSlot}});
            }
        }
    }
}

void Arm64FuncContext::emitPhiCopies(BasicBlock *bb) {
    struct Copy { Value *src; int dstSlot; };
    std::vector<Copy> copies;
    for(const auto &pc : phiCopies_){
        if (pc.first != bb) continue;
        copies.push_back({pc.second.first, pc.second.second});
    }
    if (copies.empty()) return;

    resetRegs();

    // Phase 1: read all sources into temporary registers
    struct Temp { std::string reg; int dstSlot; };
    std::vector<Temp> temps;
    for (const auto &cp : copies) {
        Value *val = cp.src;
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
        temps.push_back({tmpReg, cp.dstSlot});
    }

    // Phase 2: write all temporary registers to their destination slots
    for (const auto &t : temps) {
        emitStoreReg(os_, t.reg, t.dstSlot);
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
