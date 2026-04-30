#include "../../include/backend/arm_builder.hpp"

namespace {
std::string reg32(int idx) { return "w" + std::to_string(idx); }
std::string freg(int idx) { return "s" + std::to_string(idx); }
}

ArmBuilder::InstrKind ArmBuilder::classifyInstruction(Instruction *inst) const {
    if (!inst) return InstrKind::Unknown;
    switch (inst->op_id_) {
        case Instruction::Ret: return InstrKind::Ret;
        case Instruction::Br: return InstrKind::Br;
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::SDiv:
        case Instruction::SRem:
        case Instruction::UDiv:
        case Instruction::URem:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor: return InstrKind::BinOp;
        case Instruction::ICmp: return InstrKind::ICmp;
        case Instruction::FCmp: return InstrKind::FCmp;
        case Instruction::Call: return InstrKind::Call;
        case Instruction::Load: return InstrKind::Load;
        case Instruction::Store: return InstrKind::Store;
        case Instruction::Alloca: return InstrKind::Alloca;
        case Instruction::ZExt: return InstrKind::Zext;
        case Instruction::FPtoSI: return InstrKind::FpToSi;
        case Instruction::SItoFP: return InstrKind::SiToFp;
        case Instruction::BitCast: return InstrKind::Bitcast;
        case Instruction::GetElementPtr: return InstrKind::Gep;
        case Instruction::PHI: return InstrKind::Phi;
        default: return InstrKind::Unknown;
    }
}

void ArmBuilder::emitValueToReg(ArmFuncContext &ctx, Value *v, const std::string &reg, bool asFloat) {
    if (!v) return;
    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        if (isImm12(ci->value_)) ctx.text << "    mov " << reg << ", #" << ci->value_ << "\n";
        else ctx.text << "    movz " << reg << ", #:abs_g0:" << ci->value_ << "\n";
        return;
    }
    if (auto *cf = dynamic_cast<ConstantFloat *>(v)) {
        ctx.text << "    ldr " << reg << ", =" << floatBits(cf->value_) << "\n";
        return;
    }
    if (auto *gv = dynamic_cast<GlobalVariable *>(v)) {
        ctx.text << "    adrp x16, " << globalName(gv) << "\n";
        ctx.text << "    add x16, x16, :lo12:" << globalName(gv) << "\n";
        ctx.text << "    ldr " << reg << ", [x16]\n";
        return;
    }
    auto it = ctx.slots.find(v);
    if (it == ctx.slots.end()) {
        ctx.text << "    mov " << reg << ", xzr\n";
        return;
    }
    auto off = it->second.offset;
    ctx.text << "    ldr " << reg << ", [x29, #-" << (off + 4) << "]\n";
}

void ArmBuilder::emitStoreFromReg(ArmFuncContext &ctx, Value *dst, const std::string &reg, bool asFloat) {
    auto it = ctx.slots.find(dst);
    if (it == ctx.slots.end()) return;
    auto off = it->second.offset;
    ctx.text << "    str " << reg << ", [x29, #-" << (off + 4) << "]\n";
}

void ArmBuilder::emitLoadValue(ArmFuncContext &ctx, Value *v, const std::string &reg, bool asFloat) { emitValueToReg(ctx, v, reg, asFloat); }

void ArmBuilder::emitAddrOf(ArmFuncContext &ctx, Value *v, const std::string &reg) {
    if (auto *gv = dynamic_cast<GlobalVariable *>(v)) {
        ctx.text << "    adrp " << reg << ", " << globalName(gv) << "\n";
        ctx.text << "    add " << reg << ", " << reg << ", :lo12:" << globalName(gv) << "\n";
        return;
    }
    auto it = ctx.slots.find(v);
    if (it != ctx.slots.end()) {
        ctx.text << "    add " << reg << ", x29, #-" << (it->second.offset + 4) << "\n";
        return;
    }
    ctx.text << "    mov " << reg << ", xzr\n";
}

void ArmBuilder::emitInstruction(ArmFuncContext &ctx, Instruction *inst) {
    switch (classifyInstruction(inst)) {
        case InstrKind::Phi:
            return;
        case InstrKind::Ret: {
            auto *ret = static_cast<ReturnInst *>(inst);
            if (ret->num_ops_) emitValueToReg(ctx, ret->get_operand(0), "x0", isFloatTy(ret->get_operand(0)->type_));
            ctx.text << "    b .Lreturn_" << ctx.func->name_ << "\n";
            return;
        }
        case InstrKind::Br: {
            auto *br = static_cast<BranchInst *>(inst);
            if (br->num_ops_ == 1) {
                ctx.text << "    b " << escapeLabel(static_cast<BasicBlock *>(br->get_operand(0))->name_) << "\n";
            } else {
                emitValueToReg(ctx, br->get_operand(0), "w16", false);
                ctx.text << "    cmp w16, #0\n";
                ctx.text << "    b.ne " << escapeLabel(static_cast<BasicBlock *>(br->get_operand(1))->name_) << "\n";
                ctx.text << "    b " << escapeLabel(static_cast<BasicBlock *>(br->get_operand(2))->name_) << "\n";
            }
            return;
        }
        case InstrKind::BinOp: {
            auto *bin = static_cast<BinaryInst *>(inst);
            bool isf = isFloatTy(inst->type_);
            emitValueToReg(ctx, bin->get_operand(0), isf ? "s16" : "w16", isf);
            emitValueToReg(ctx, bin->get_operand(1), isf ? "s17" : "w17", isf);
            switch (bin->op_id_) {
                case Instruction::Add: ctx.text << "    add w16, w16, w17\n"; break;
                case Instruction::Sub: ctx.text << "    sub w16, w16, w17\n"; break;
                case Instruction::Mul: ctx.text << "    mul w16, w16, w17\n"; break;
                case Instruction::SDiv: ctx.text << "    sdiv w16, w16, w17\n"; break;
                case Instruction::FAdd: ctx.text << "    fadd s16, s16, s17\n"; break;
                case Instruction::FSub: ctx.text << "    fsub s16, s16, s17\n"; break;
                case Instruction::FMul: ctx.text << "    fmul s16, s16, s17\n"; break;
                case Instruction::FDiv: ctx.text << "    fdiv s16, s16, s17\n"; break;
                case Instruction::Shl: ctx.text << "    lsl w16, w16, w17\n"; break;
                case Instruction::LShr: ctx.text << "    lsr w16, w16, w17\n"; break;
                case Instruction::AShr: ctx.text << "    asr w16, w16, w17\n"; break;
                case Instruction::And: ctx.text << "    and w16, w16, w17\n"; break;
                case Instruction::Or: ctx.text << "    orr w16, w16, w17\n"; break;
                case Instruction::Xor: ctx.text << "    eor w16, w16, w17\n"; break;
                default: ctx.text << "    // unsupported binary\n"; break;
            }
            emitStoreFromReg(ctx, inst, isf ? "s16" : "w16", isf);
            return;
        }
        case InstrKind::ICmp: {
            auto *cmp = static_cast<ICmpInst *>(inst);
            // 整数比较：先 cmp 再用条件置位指令生成 i1。
            emitValueToReg(ctx, cmp->get_operand(0), "w16");
            emitValueToReg(ctx, cmp->get_operand(1), "w17");
            ctx.text << "    cmp w16, w17\n";
            switch (cmp->icmp_op_) {
                case ICmpInst::ICMP_EQ: ctx.text << "    cset w16, eq\n"; break;
                case ICmpInst::ICMP_NE: ctx.text << "    cset w16, ne\n"; break;
                case ICmpInst::ICMP_SGT: ctx.text << "    cset w16, gt\n"; break;
                case ICmpInst::ICMP_SGE: ctx.text << "    cset w16, ge\n"; break;
                case ICmpInst::ICMP_SLT: ctx.text << "    cset w16, lt\n"; break;
                case ICmpInst::ICMP_SLE: ctx.text << "    cset w16, le\n"; break;
                case ICmpInst::ICMP_UGT: ctx.text << "    cset w16, hi\n"; break;
                case ICmpInst::ICMP_UGE: ctx.text << "    cset w16, cs\n"; break;
                case ICmpInst::ICMP_ULT: ctx.text << "    cset w16, cc\n"; break;
                case ICmpInst::ICMP_ULE: ctx.text << "    cset w16, ls\n"; break;
                default: ctx.text << "    cset w16, ne\n"; break;
            }
            emitStoreFromReg(ctx, inst, "w16");
            return;
        }
        case InstrKind::FCmp: {
            auto *fcmp = static_cast<FCmpInst *>(inst);
            // 浮点比较结果最终也规约为 i1。
            emitValueToReg(ctx, fcmp->get_operand(0), "s16", true);
            emitValueToReg(ctx, fcmp->get_operand(1), "s17", true);
            ctx.text << "    fcmp s16, s17\n";
            switch (fcmp->fcmp_op_) {
                case FCmpInst::FCMP_OEQ:
                case FCmpInst::FCMP_UEQ: ctx.text << "    cset w16, eq\n"; break;
                case FCmpInst::FCMP_ONE:
                case FCmpInst::FCMP_UNE: ctx.text << "    cset w16, ne\n"; break;
                case FCmpInst::FCMP_OGT:
                case FCmpInst::FCMP_UGT: ctx.text << "    cset w16, gt\n"; break;
                case FCmpInst::FCMP_OGE:
                case FCmpInst::FCMP_UGE: ctx.text << "    cset w16, ge\n"; break;
                case FCmpInst::FCMP_OLT:
                case FCmpInst::FCMP_ULT: ctx.text << "    cset w16, lt\n"; break;
                case FCmpInst::FCMP_OLE:
                case FCmpInst::FCMP_ULE: ctx.text << "    cset w16, le\n"; break;
                default: ctx.text << "    cset w16, ne\n"; break;
            }
            emitStoreFromReg(ctx, inst, "w16");
            return;
        }
        case InstrKind::Call: {
            auto *call = static_cast<CallInst *>(inst);
            unsigned argc = call->num_ops_ - 1;
            for (unsigned i = 0; i < argc && i < 8; ++i)
                emitValueToReg(ctx, call->get_operand(i), isFloatTy(call->get_operand(i)->type_) ? freg(i) : reg32(i), isFloatTy(call->get_operand(i)->type_));
            for (unsigned i = 8; i < argc; ++i) {
                emitValueToReg(ctx, call->get_operand(i), "x16", isFloatTy(call->get_operand(i)->type_));
                ctx.text << "    str x16, [sp, #-16]!\n";
            }
            auto *callee = static_cast<Function *>(call->get_operand(argc));
            ctx.text << "    bl " << callee->name_ << "\n";
            if (argc > 8) ctx.text << "    add sp, sp, #" << ((argc - 8) * 16) << "\n";
            if (!call->is_void()) emitStoreFromReg(ctx, inst, isFloatTy(inst->type_) ? "s0" : "w0", isFloatTy(inst->type_));
            return;
        }
        case InstrKind::Load: {
            auto *ld = static_cast<LoadInst *>(inst);
            emitAddrOf(ctx, ld->get_operand(0), "x16");
            if (isFloatTy(inst->type_)) ctx.text << "    ldr s16, [x16]\n";
            else ctx.text << "    ldr w16, [x16]\n";
            emitStoreFromReg(ctx, inst, isFloatTy(inst->type_) ? "s16" : "w16", isFloatTy(inst->type_));
            return;
        }
        case InstrKind::Store: {
            auto *st = static_cast<StoreInst *>(inst);
            emitValueToReg(ctx, st->get_operand(0), isFloatTy(st->get_operand(0)->type_) ? "s16" : "w16", isFloatTy(st->get_operand(0)->type_));
            emitAddrOf(ctx, st->get_operand(1), "x17");
            ctx.text << "    str " << (isFloatTy(st->get_operand(0)->type_) ? "s16" : "w16") << ", [x17]\n";
            return;
        }
        case InstrKind::Alloca: {
            emitAddrOf(ctx, inst, "x16");
            emitStoreFromReg(ctx, inst, "x16");
            return;
        }
        case InstrKind::Zext: {
            auto *z = static_cast<ZextInst *>(inst);
            emitValueToReg(ctx, z->get_operand(0), "w16");
            emitStoreFromReg(ctx, inst, "w16");
            return;
        }
        case InstrKind::FpToSi: {
            auto *fp = static_cast<FpToSiInst *>(inst);
            emitValueToReg(ctx, fp->get_operand(0), "s16", true);
            ctx.text << "    fcvtzs w16, s16\n";
            emitStoreFromReg(ctx, inst, "w16");
            return;
        }
        case InstrKind::SiToFp: {
            auto *si = static_cast<SiToFpInst *>(inst);
            emitValueToReg(ctx, si->get_operand(0), "w16");
            ctx.text << "    scvtf s16, w16\n";
            emitStoreFromReg(ctx, inst, "s16", true);
            return;
        }
        case InstrKind::Bitcast: {
            auto *bit = static_cast<Bitcast *>(inst);
            emitValueToReg(ctx, bit->get_operand(0), isPointerLike(bit->get_operand(0)->type_) ? "x16" : "w16", false);
            emitStoreFromReg(ctx, inst, isPointerLike(inst->type_) ? "x16" : "w16", isPointerLike(inst->type_));
            return;
        }
        case InstrKind::Gep: {
            auto *gep = static_cast<GetElementPtrInst *>(inst);
            emitAddrOf(ctx, gep->get_operand(0), "x16");
            ctx.text << "    mov x17, x16\n";
            auto *base_ty = static_cast<PointerType *>(gep->get_operand(0)->type_)->contained_;
            for (unsigned i = 1; i < gep->num_ops_; ++i) {
                auto *idx = gep->get_operand(i);
                if (auto *ci = dynamic_cast<ConstantInt *>(idx)) {
                    size_t elem = 4;
                    if (base_ty && base_ty->tid_ == Type::ArrayTyID) elem = typeSize(static_cast<ArrayType *>(base_ty)->contained_);
                    ctx.text << "    add x17, x17, #" << (ci->value_ * static_cast<int64_t>(elem)) << "\n";
                    if (base_ty && base_ty->tid_ == Type::ArrayTyID) base_ty = static_cast<ArrayType *>(base_ty)->contained_;
                } else {
                    emitValueToReg(ctx, idx, "x18");
                    ctx.text << "    add x17, x17, x18, lsl #2\n";
                }
            }
            emitStoreFromReg(ctx, inst, "x17", true);
            return;
        }
        default:
            return;
    }
}
