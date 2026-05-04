#include "../../../include/backend/arm64/arm64_context.hpp"
#include "../../../include/backend/arm64/arm64_codegen.hpp"
#include "../../../include/mid/ir/ir.hpp"
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

// emit str/ldr with potentially large negative offset
static void emitStoreReg(std::ostream &os, const std::string &reg, int off) {
    if (off >= -256 && off <= 255) {
        os << "\tstr " << reg << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        if (pos <= 4095) {
            os << "\tsub x16, x29, #" << pos << "\n";
        } else {
            os << "\tmovz x16, #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk x16, #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub x16, x29, x16\n";
        }
        os << "\tstr " << reg << ", [x16]\n";
    }
}

static void emitLoadReg(std::ostream &os, const std::string &reg, int off) {
    if (off >= -256 && off <= 255) {
        os << "\tldr " << reg << ", [x29, #" << off << "]\n";
    } else {
        int pos = -off;
        if (pos <= 4095) {
            os << "\tsub x16, x29, #" << pos << "\n";
        } else {
            os << "\tmovz x16, #" << (pos & 0xFFFF) << "\n";
            os << "\tmovk x16, #" << ((pos >> 16) & 0xFFFF) << ", lsl #16\n";
            os << "\tsub x16, x29, x16\n";
        }
        os << "\tldr " << reg << ", [x16]\n";
    }
}

// ---- Arm64FuncContext ----

Arm64FuncContext::Arm64FuncContext(Function *f, Arm64CodeGen &parent, std::ostream &os)
    : func_(f), parent_(parent), os_(os) {}

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

    emitPrologue();
    for (auto bb : func_->basic_blocks_) {
        emitBlock(bb);
    }
    emitEpilogue();
}

void Arm64FuncContext::emitPrologue() {
    os_ << "\t.globl " << func_->name_ << "\n";
    os_ << "\t.p2align 2\n";
    os_ << func_->name_ << ":\n";

    // allocate slots for arguments
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
                       !inst->is_store() && !inst->is_br() && !inst->is_ret()) {
                getSlot(inst);
            }
        }
    }

    int localSize = align16(frameSize_);

    // stp supports only -512..504 range; use minimal stp + sub for large frames
    os_ << "\tstp x29, x30, [sp, #-16]!\n";
    os_ << "\tmov x29, sp\n";
    if (localSize > 0) {
        if (localSize <= 4095) {
            os_ << "\tsub sp, sp, #" << localSize << "\n";
        } else {
            os_ << "\tmovz x16, #" << (localSize & 0xFFFF) << "\n";
            os_ << "\tmovk x16, #" << ((localSize >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tsub sp, sp, x16\n";
        }
    }

    // store arguments from registers to their slots
    int intArg = 0, floatArg = 0;
    for (auto arg : func_->arguments_) {
        int off = getSlot(arg);
        if (isFloat(arg->type_)) {
            if (floatArg < 8) {
                emitStoreReg(os_, "s" + std::to_string(floatArg), off);
                floatArg++;
            }
        } else {
            if (intArg < 8) {
                // pointer needs 64-bit register
                // bool isPointer = (arg->type_->tid_ == Type::PointerTyID);
                // or maybe array too, revert this if things got wrong
                bool isPointer = (arg->type_->tid_ == Type::PointerTyID ||
                                  arg->type_->tid_ == Type::ArrayTyID);
                std::string reg = (isPointer ? "x" : "w") + std::to_string(intArg);
                emitStoreReg(os_, reg, off);
                intArg++;
            }
        }
    }
}

void Arm64FuncContext::emitEpilogue() {
    if (!epilogueBB_) return;
    os_ << ".L" << func_->name_ << "_epilogue:\n";

    int localSize = align16(frameSize_);
    if (localSize > 0) {
        if (localSize <= 4095) {
            os_ << "\tadd sp, sp, #" << localSize << "\n";
        } else {
            os_ << "\tmovz x16, #" << (localSize & 0xFFFF) << "\n";
            os_ << "\tmovk x16, #" << ((localSize >> 16) & 0xFFFF) << ", lsl #16\n";
            os_ << "\tadd sp, sp, x16\n";
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
        std::string ra = loadInt(v1);
        std::string rb = loadInt(v2);
        std::string rq = allocIntReg();
        std::string rr = allocIntReg();
        os_ << "\tsdiv " << rq << ", " << ra << ", " << rb << "\n";
        os_ << "\tmsub " << rr << ", " << rq << ", " << rb << ", " << ra << "\n";
        storeInt(inst, rr);
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
        std::string base = loadAddr(ptr);
        unsigned numIdx = gep->num_ops_ - 1;

        if (numIdx == 0) {
            storeAddr(inst, base);
            break;
        }

        auto srcTy = static_cast<PointerType*>(ptr->type_)->contained_;
        Type *curTy = srcTy;
        std::string addr = base;

        for (unsigned i = 1; i < gep->num_ops_; i++) {
            auto idx = gep->get_operand(i);

            // determine element size at this level
            int elemSize;
            if (i == 1) {
                // The first GEP index steps over whole objects pointed to by ptr.
                // For the common pattern gep [N x T]* %arr, 0, i, ... this
                // leading zero must not descend into the array; otherwise the
                // next index is scaled by sizeof(T) instead of sizeof([M x T]).
                elemSize = typeSize(curTy);
            } else if (curTy->tid_ == Type::ArrayTyID) {
                auto at = static_cast<ArrayType*>(curTy);
                elemSize = typeSize(at->contained_);
                curTy = at->contained_;
            } else if (curTy->tid_ == Type::PointerTyID) {
                auto pt = static_cast<PointerType*>(curTy);
                elemSize = typeSize(pt->contained_);
                curTy = pt->contained_;
            } else {
                elemSize = typeSize(curTy);
            }

            if (auto ci = dynamic_cast<ConstantInt*>(idx)) {
                int offset = ci->value_ * elemSize;
                if (offset != 0) {
                    std::string newAddr = allocAddrReg();
                    os_ << "\tadd " << newAddr << ", " << addr << ", #" << offset << "\n";
                    addr = newAddr;
                }
            } else {
                std::string idxReg = loadInt(idx);
                std::string scaled = allocAddrReg();
                std::string newAddr = allocAddrReg();
                // sign-extend index to 64-bit for address calculation
                os_ << "\tsxtw " << scaled << ", " << idxReg << "\n";
                if (elemSize > 1) { // check if elemSize is a power of two
                    auto isPowerOfTwo = [](int n) { return n > 0 && (n & (n - 1)) == 0; };
                    if (isPowerOfTwo(elemSize)) {
                        int shift = 0;
                        while ((1 << shift) < elemSize) shift++;
                        os_ << "\tadd " << newAddr << ", " << addr << ", " << scaled
                            << ", lsl #" << shift << "\n";
                    } else {
                        std::string elemReg = allocAddrReg();
                        std::string productReg = allocAddrReg();
                        os_ << "\tmovz " << elemReg << ", #" << elemSize << "\n";
                        os_ << "\tmul " << productReg << ", " << scaled << ", " << elemReg << "\n";
                        os_ << "\tadd " << newAddr << ", " << addr << ", " << productReg << "\n";
                    }
                } else {
                    os_ << "\tadd " << newAddr << ", " << addr << ", " << scaled << "\n";
                }
                addr = newAddr;
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

        if (callee->name_ == "__aeabi_memclr4") {
            // Inline the compiler-generated local array zero-initialization helper.
            // This avoids requiring libgcc's ARM EABI helper at link time.
            auto ptr = call->get_operand(0);
            auto sizeVal = call->get_operand(1);
            std::string addr = loadAddr(ptr);

            if (auto sizeConst = dynamic_cast<ConstantInt*>(sizeVal)) {
                int bytes = sizeConst->value_;
                for (int off = 0; off < bytes; off += 4) {
                    if (off == 0) {
                        os_ << "\tstr wzr, [" << addr << "]\n";
                    } else {
                        os_ << "\tstr wzr, [" << addr << ", #" << off << "]\n";
                    }
                }
            } else {
                static int memclrLoopId = 0;
                std::string sizeReg = loadInt(sizeVal);
                std::string zeroReg = allocIntReg();
                std::string loop = ".L" + func_->name_ + "_memclr_" + std::to_string(memclrLoopId++);
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

        // assign arguments to registers
        int intArg = 0, floatArg = 0;
        std::vector<std::pair<std::string, bool>> savedArgs; // reg, isFloat

        for (unsigned i = 0; i < numArgs; i++) {
            auto arg = call->get_operand(i);
            if (isFloat(arg->type_)) {
                std::string r = loadFloat(arg);
                if (floatArg < 8) {
                    os_ << "\tfmov s" << floatArg << ", " << r << "\n";
                }
                floatArg++;
            } else if (isPtr(arg->type_)) {
                std::string r = loadAddr(arg);
                if (intArg < 8) {
                    os_ << "\tmov x" << intArg << ", " << r << "\n";
                }
                intArg++;
            } else {
                std::string r = loadInt(arg);
                if (intArg < 8) {
                    os_ << "\tmov w" << intArg << ", " << r << "\n";
                }
                intArg++;
            }
        }

        os_ << "\tbl " << callee->name_ << "\n";

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

std::string Arm64FuncContext::allocIntReg() {
    for (int r = 9; r <= 15; r++) {
        if (!usedIntRegs_.count(r)) {
            usedIntRegs_.insert(r);
            return "w" + std::to_string(r);
        }
    }
    return "w9";
}

std::string Arm64FuncContext::allocFloatReg() {
    for (int r = 8; r <= 15; r++) {
        if (!usedFloatRegs_.count(r)) {
            usedFloatRegs_.insert(r);
            return "s" + std::to_string(r);
        }
    }
    return "s8";
}

std::string Arm64FuncContext::allocAddrReg() {
    for (int r = 9; r <= 15; r++) {
        if (!usedIntRegs_.count(r)) {
            usedIntRegs_.insert(r);
            return "x" + std::to_string(r);
        }
    }
    return "x9";
}

// ---- load from slot/constant/global to scratch register ----

std::string Arm64FuncContext::loadInt(Value *v) {
    if (auto ci = dynamic_cast<ConstantInt*>(v)) {
        std::string r = allocIntReg();
        emitIntConst(ci->value_, r);
        return r;
    }
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
    // AllocaInst: the "value" is the address of its stack space
    if (dynamic_cast<AllocaInst*>(v)) {
        std::string r = allocAddrReg();
        int off = getSlot(v);
        if (off < 0) {
            os_ << "\tsub " << r << ", x29, #" << -off << "\n";
        } else {
            os_ << "\tadd " << r << ", x29, #" << off << "\n";
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
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

void Arm64FuncContext::storeFloat(Value *v, const std::string &reg) {
    int off = getSlot(v);
    emitStoreReg(os_, reg, off);
}

void Arm64FuncContext::storeAddr(Value *v, const std::string &reg) {
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
    for (auto &pc : phiCopies_) {
        if (pc.first != bb) continue;
        auto val = pc.second.first;
        int slot = pc.second.second;

        resetRegs();
        if (auto ci = dynamic_cast<ConstantInt*>(val)) {
            std::string r = allocIntReg();
            emitIntConst(ci->value_, r);
            emitStoreReg(os_, r, slot);
        } else if (auto cf = dynamic_cast<ConstantFloat*>(val)) {
            std::string r = allocFloatReg();
            emitFloatConst(cf->value_, r);
            emitStoreReg(os_, r, slot);
        } else if (hasSlot(val)) {
            if (isFloat(val->type_)) {
                std::string r = allocFloatReg();
                emitLoadReg(os_, r, getSlot(val));
                emitStoreReg(os_, r, slot);
            } else if (isPtr(val->type_)) {
                std::string r = allocAddrReg();
                emitLoadReg(os_, r, getSlot(val));
                emitStoreReg(os_, r, slot);
            } else {
                std::string r = allocIntReg();
                emitLoadReg(os_, r, getSlot(val));
                emitStoreReg(os_, r, slot);
            }
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
    default: return "eq";
    }
}

const char *Arm64FuncContext::fcmpCond(FCmpInst::FCmpOp op) {
    switch (op) {
    case FCmpInst::FCMP_UEQ: return "eq";
    case FCmpInst::FCMP_UNE: return "ne";
    case FCmpInst::FCMP_UGT: return "gt";
    case FCmpInst::FCMP_UGE: return "ge";
    case FCmpInst::FCMP_ULT: return "mi";
    case FCmpInst::FCMP_ULE: return "ls";
    default: return "eq";
    }
}
