#include "../../../include/backend/arm64/arm64_context.hpp"
#include "../../../include/backend/arm64/arm64_codegen.hpp"
#include "../../../include/mid/ir/ir.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>

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
                getSlot(inst); // allocate space for the alloca region
            } else if (inst->type_->tid_ != Type::VoidTyID &&
                       !dynamic_cast<Constant*>(inst) &&
                       !inst->is_store() && !inst->is_br() && !inst->is_ret()) {
                getSlot(inst); // result value needs a slot
            }
        }
    }

    int totalFrame = align16(frameSize_ + 32); // +32 for FP/LR + red zone
    int subSp = totalFrame - 16;              // 16 already covered by stp pre-index

    os_ << "\tstp x29, x30, [sp, #-" << totalFrame << "]!\n";
    os_ << "\tmov x29, sp\n";

    // store arguments from registers to their slots
    int intArg = 0, floatArg = 0;
    for (auto arg : func_->arguments_) {
        if (isFloat(arg->type_)) {
            if (floatArg < 8) {
                os_ << "\tstr s" << floatArg << ", [x29, #" << getSlot(arg) << "]\n";
                floatArg++;
            }
        } else {
            if (intArg < 8) {
                os_ << "\tstr w" << intArg << ", [x29, #" << getSlot(arg) << "]\n";
                intArg++;
            }
        }
    }
}

void Arm64FuncContext::emitEpilogue() {
    if (!epilogueBB_) return;
    os_ << ".L" << func_->name_ << "_epilogue:\n";

    int totalFrame = align16(frameSize_ + 32);
    os_ << "\tldp x29, x30, [sp], #" << totalFrame << "\n";
    os_ << "\tret\n";
}

void Arm64FuncContext::emitBlock(BasicBlock *bb) {
    os_ << bb->name_ << ":\n";
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
            if (curTy->tid_ == Type::ArrayTyID) {
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
                if (elemSize > 1) {
                    int shift = 0;
                    while ((1 << shift) < elemSize) shift++;
                    os_ << "\tadd " << newAddr << ", " << addr << ", " << scaled
                        << ", lsl #" << shift << "\n";
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
            os_ << "\tb " << target->name_ << "\n";
        } else {
            auto cond = inst->get_operand(0);
            auto trueBB = static_cast<BasicBlock*>(inst->get_operand(1));
            auto falseBB = static_cast<BasicBlock*>(inst->get_operand(2));
            std::string cr = loadInt(cond);
            os_ << "\tcbnz " << cr << ", " << trueBB->name_ << "\n";
            os_ << "\tb " << falseBB->name_ << "\n";
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
        size = 8; // 8-byte slot for values
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
    os_ << "\tldr " << r << ", [x29, #" << off << "]\n";
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
    os_ << "\tldr " << r << ", [x29, #" << off << "]\n";
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
        os_ << "\tadd " << r << ", x29, #" << off << "\n";
        return r;
    }
    std::string r = allocAddrReg();
    int off = getSlot(v);
    os_ << "\tldr " << r << ", [x29, #" << off << "]\n";
    return r;
}

// ---- store from register to slot ----

void Arm64FuncContext::storeInt(Value *v, const std::string &reg) {
    int off = getSlot(v);
    os_ << "\tstr " << reg << ", [x29, #" << off << "]\n";
}

void Arm64FuncContext::storeFloat(Value *v, const std::string &reg) {
    int off = getSlot(v);
    os_ << "\tstr " << reg << ", [x29, #" << off << "]\n";
}

void Arm64FuncContext::storeAddr(Value *v, const std::string &reg) {
    int off = getSlot(v);
    os_ << "\tstr " << reg << ", [x29, #" << off << "]\n";
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
            os_ << "\tstr " << r << ", [x29, #" << slot << "]\n";
        } else if (auto cf = dynamic_cast<ConstantFloat*>(val)) {
            std::string r = allocFloatReg();
            emitFloatConst(cf->value_, r);
            os_ << "\tstr " << r << ", [x29, #" << slot << "]\n";
        } else if (hasSlot(val)) {
            if (isFloat(val->type_)) {
                std::string r = allocFloatReg();
                os_ << "\tldr " << r << ", [x29, #" << getSlot(val) << "]\n";
                os_ << "\tstr " << r << ", [x29, #" << slot << "]\n";
            } else if (isPtr(val->type_)) {
                std::string r = allocAddrReg();
                os_ << "\tldr " << r << ", [x29, #" << getSlot(val) << "]\n";
                os_ << "\tstr " << r << ", [x29, #" << slot << "]\n";
            } else {
                std::string r = allocIntReg();
                os_ << "\tldr " << r << ", [x29, #" << getSlot(val) << "]\n";
                os_ << "\tstr " << r << ", [x29, #" << slot << "]\n";
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
