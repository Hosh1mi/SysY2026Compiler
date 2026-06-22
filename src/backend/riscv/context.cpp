#include "../../include/backend/riscv/context.hpp"
#include "../../include/backend/riscv/regalloc.hpp"
#include "../../include/backend/arm64/magicNumber.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace riscv;

namespace {
int alignUp(int x, int a) { return ((x + a - 1) / a) * a; }

bool icmpToBranch(int op, const char *&mn, bool &swap) {
    swap = false;
    switch (op) {
    case ICmpInst::ICMP_EQ:  mn = "beq";  break;
    case ICmpInst::ICMP_NE:  mn = "bne";  break;
    case ICmpInst::ICMP_SLT: mn = "blt";  break;
    case ICmpInst::ICMP_SGT: mn = "blt";  swap = true; break;  // a>b ⇔ b<a
    case ICmpInst::ICMP_SLE: mn = "bge";  swap = true; break;  // a<=b ⇔ b>=a
    case ICmpInst::ICMP_SGE: mn = "bge";  break;
    case ICmpInst::ICMP_ULT: mn = "bltu"; break;
    case ICmpInst::ICMP_UGT: mn = "bltu"; swap = true; break;
    case ICmpInst::ICMP_ULE: mn = "bgeu"; swap = true; break;
    case ICmpInst::ICMP_UGE: mn = "bgeu"; break;
    default: return false;
    }
    return true;
}

// 取条件分支的反向助记符（操作数顺序不变即为逻辑取反）。
const char *invertBranch(const char *mn) {
    std::string s(mn);
    if (s == "beq")  return "bne";
    if (s == "bne")  return "beq";
    if (s == "blt")  return "bge";
    if (s == "bge")  return "blt";
    if (s == "bltu") return "bgeu";
    if (s == "bgeu") return "bltu";
    return mn;
}
}  // namespace

RiscvFuncContext::RiscvFuncContext(Function *f, MFunction &mfunc, bool enableRegAlloc)
    : func_(f), mfunc_(mfunc), enableRegAlloc_(enableRegAlloc) {}

// ── 类型/栈槽 ──────────────────────────────────────────────────────────────

int RiscvFuncContext::sizeOfType(Type *ty) const {
    if (!ty) return 4;
    switch (ty->tid_) {
    case Type::IntegerTyID: {
        int bits = static_cast<IntegerType *>(ty)->num_bits_;
        return bits <= 1 ? 1 : bits / 8;
    }
    case Type::FloatTyID:
        return 4;
    case Type::PointerTyID:
        return 8;
    case Type::ArrayTyID: {
        auto *arr = static_cast<ArrayType *>(ty);
        return arr->num_elements_ * sizeOfType(arr->contained_);
    }
    case Type::VectorTyID: {
        auto *vec = static_cast<VectorType *>(ty);
        return vec->num_elements_ * sizeOfType(vec->contained_);
    }
    default:
        return 4;
    }
}

RiscvFuncContext::SlotKind RiscvFuncContext::slotKindOf(Value *v) const {
    Type *ty = v->type_;
    if (!ty) return SlotKind::Int32;
    if (ty->tid_ == Type::FloatTyID) return SlotKind::Float;
    if (ty->tid_ == Type::PointerTyID) return SlotKind::Pointer;
    return SlotKind::Int32;
}

int RiscvFuncContext::slotOf(Value *v) {
    auto it = slots_.find(v);
    return it != slots_.end() ? it->second : 0;
}

// 预扫描：确定 callee-saved 保存区，为溢出值/alloca 区域分配栈槽，计算帧大小。
void RiscvFuncContext::planFrame() {
    // 收集实际占用的 callee-saved（来自图着色结果），有序去重，确定保存区大小。
    {
        std::set<std::string> cs;
        for (auto &kv : assignedRegs_) {
            const std::string &reg = kv.second;
            bool calleeSavedGpr = !reg.empty() && reg[0] == 's';
            bool calleeSavedFpr = reg.rfind("fs", 0) == 0;
            if (calleeSavedGpr || calleeSavedFpr) cs.insert(reg);
        }
        usedCalleeSaved_.assign(cs.begin(), cs.end());
    }
    saveAreaBytes_ = 16 + static_cast<int>(usedCalleeSaved_.size()) * 8;

    localCursor_ = 0;
    auto slotBase = [&]() { return -(saveAreaBytes_ + localCursor_); };

    // 形参槽：仅未分配寄存器（溢出）的形参需要。
    for (auto *arg : func_->arguments_) {
        if (arg->use_list_.empty()) continue;
        if (hasReg(arg)) continue;
        localCursor_ += 8;
        slots_[arg] = slotBase();
    }

    int maxPhi = 0;
    for (auto *bb : func_->basic_blocks_) {
        int phiCnt = 0;
        for (auto *inst : bb->instr_list_) {
            if (inst->is_phi()) phiCnt++;
            if (inst->is_alloca()) {
                if (inst->use_list_.empty()) continue;
                auto *al = static_cast<AllocaInst *>(inst);
                int region = alignUp(sizeOfType(al->alloca_ty_), 8);
                localCursor_ += region;
                slots_[inst] = slotBase();  // 区域基址（最低地址）
                continue;
            }
            if (inst->is_void()) continue;  // store/br/ret/void-call 无结果
            if (inst->use_list_.empty()) continue;
            if (hasReg(inst)) continue;     // 已分配寄存器，无需栈槽
            localCursor_ += 8;
            slots_[inst] = slotBase();
        }
        if (phiCnt > maxPhi) maxPhi = phiCnt;
    }

    // phi 并行拷贝临时区（maxPhi 个 8 字节槽，供边上两遍拷贝使用）。
    phiTempBytes_ = maxPhi * 8;
    localCursor_ += phiTempBytes_;
    phiTempBase_ = -(saveAreaBytes_ + localCursor_);

    // 出参溢出区：取所有 call 的栈传参字节上界。
    outArgBytes_ = 0;
    for (auto *bb : func_->basic_blocks_) {
        for (auto *inst : bb->instr_list_) {
            if (inst->op_id_ != Instruction::Call) continue;
            int gp = 0, fp = 0, stackCnt = 0;
            unsigned nArgs = inst->num_ops_ - 1;  // 末位是被调函数
            for (unsigned i = 0; i < nArgs; ++i) {
                Value *a = inst->get_operand(i);
                if (a->type_ && a->type_->tid_ == Type::FloatTyID) {
                    if (fp < kNumFpArgRegs) fp++; else stackCnt++;
                } else {
                    if (gp < kNumIntArgRegs) gp++; else stackCnt++;
                }
            }
            outArgBytes_ = std::max(outArgBytes_, stackCnt * 8);
        }
    }
    outArgBytes_ = alignUp(outArgBytes_, 16);

    frameSize_ = alignUp(saveAreaBytes_ + localCursor_ + outArgBytes_, 16);
}

// ── 发射辅助 ────────────────────────────────────────────────────────────────

void RiscvFuncContext::emit(const std::string &text) { mfunc_.push(MInst::inst(text)); }
void RiscvFuncContext::emitLabel(const std::string &label) { mfunc_.startBlock(label); }

void RiscvFuncContext::emitMem(const std::string &text, bool load) {
    MInst m = MInst::inst(text);
    m.mayLoad = load;
    m.mayStore = !load;
    mfunc_.push(m);
}

void RiscvFuncContext::emitCall(const std::string &text) {
    MInst m = MInst::inst(text);
    m.isCall = true;
    mfunc_.push(m);
}

void RiscvFuncContext::emitTerminator(const std::string &text) {
    MInst m = MInst::inst(text);
    m.isTerminator = true;
    mfunc_.push(m);
}

void RiscvFuncContext::emitLoadSlot(const std::string &reg, int off, SlotKind kind,
                                    const std::string &base) {
    const char *mn = memMnemonic(kind, /*load=*/true);
    if (fitsImm12(off)) {
        emitMem(std::string(mn) + " " + reg + ", " + std::to_string(off) + "(" + base + ")", true);
    } else {
        emit("li " + std::string(scratch::frame0()) + ", " + std::to_string(off));
        emit("add " + std::string(scratch::frame0()) + ", " + base + ", " + scratch::frame0());
        emitMem(std::string(mn) + " " + reg + ", 0(" + scratch::frame0() + ")", true);
    }
}

void RiscvFuncContext::emitStoreSlot(const std::string &reg, int off, SlotKind kind,
                                     const std::string &base) {
    const char *mn = memMnemonic(kind, /*load=*/false);
    if (fitsImm12(off)) {
        emitMem(std::string(mn) + " " + reg + ", " + std::to_string(off) + "(" + base + ")", false);
    } else {
        emit("li " + std::string(scratch::frame0()) + ", " + std::to_string(off));
        emit("add " + std::string(scratch::frame0()) + ", " + base + ", " + scratch::frame0());
        emitMem(std::string(mn) + " " + reg + ", 0(" + scratch::frame0() + ")", false);
    }
}

const char *RiscvFuncContext::memMnemonic(SlotKind kind, bool load) {
    switch (kind) {
    case SlotKind::Float:
        return load ? "flw" : "fsw";
    case SlotKind::Pointer:
        return load ? "ld" : "sd";
    case SlotKind::Int32:
    default:
        return load ? "lw" : "sw";
    }
}

// ── 取值 / 写回 ──────────────────────────────────────────────────────────────

void RiscvFuncContext::loadInt(Value *v, const std::string &reg) {
    if (hasReg(v)) {
        if (reg != regOf(v)) emit("mv " + reg + ", " + regOf(v));
        return;
    }
    if (auto *ci = dynamic_cast<ConstantInt *>(v)) {
        emit("li " + reg + ", " + std::to_string(ci->value_));
        return;
    }
    emitLoadSlot(reg, slotOf(v), SlotKind::Int32);
}

std::string RiscvFuncContext::intOperand(Value *v, const std::string &scratch) {
    if (auto *ci = dynamic_cast<ConstantInt *>(v); ci && ci->value_ == 0) return "zero";
    loadInt(v, scratch);
    return scratch;
}

void RiscvFuncContext::loadAddr(Value *v, const std::string &reg) {
    if (hasReg(v)) {
        if (reg != regOf(v)) emit("mv " + reg + ", " + regOf(v));
        return;
    }
    if (auto *gv = dynamic_cast<GlobalVariable *>(v)) {
        emit("la " + reg + ", " + gv->name_);
        return;
    }
    if (dynamic_cast<AllocaInst *>(v)) {
        int off = slotOf(v);
        if (fitsImm12(off)) {
            emit("addi " + reg + ", s0, " + std::to_string(off));
        } else {
            emit("li " + reg + ", " + std::to_string(off));
            emit("add " + reg + ", s0, " + reg);
        }
        return;
    }
    emitLoadSlot(reg, slotOf(v), SlotKind::Pointer);
}

void RiscvFuncContext::loadFloat(Value *v, const std::string &freg) {
    if (hasReg(v)) {
        if (freg != regOf(v)) emit("fmv.s " + freg + ", " + regOf(v));
        return;
    }
    if (auto *cf = dynamic_cast<ConstantFloat *>(v)) {
        int bits;
        float f = cf->value_;
        std::memcpy(&bits, &f, sizeof(bits));
        emit("li " + std::string(scratch::frame1()) + ", " + std::to_string(bits));
        emit("fmv.w.x " + freg + ", " + scratch::frame1());
        return;
    }
    emitLoadSlot(freg, slotOf(v), SlotKind::Float);
}

void RiscvFuncContext::storeResult(Value *v, const std::string &reg) {
    if (v->use_list_.empty()) return;
    if (hasReg(v)) {
        const std::string &dst = regOf(v);
        if (dst == reg) return;
        if (slotKindOf(v) == SlotKind::Float)
            emit("fmv.s " + dst + ", " + reg);
        else
            emit("mv " + dst + ", " + reg);
        return;
    }
    emitStoreSlot(reg, slotOf(v), slotKindOf(v));
}

std::string RiscvFuncContext::blockLabel(BasicBlock *bb) const {
    return ".L_" + func_->name_ + "_" + bb->name_;
}

// ── 顶层生成 ────────────────────────────────────────────────────────────────

void RiscvFuncContext::generate() {
    if (enableRegAlloc_) {
        RiscvRegAlloc ra(func_);
        ra.allocate();
        assignedRegs_ = ra.assignedRegs();
    }
    planFrame();
    emitPrologue();
    for (auto *bb : func_->basic_blocks_)
        emitBlock(bb);
}

void RiscvFuncContext::emitPrologue() {
    mfunc_.addDirective("\t.globl " + func_->name_);
    emitLabel(func_->name_);

    int fs = frameSize_;
    // sp -= fs
    if (fitsImm12(fs)) {
        emit("addi sp, sp, -" + std::to_string(fs));
    } else {
        emit("li t0, " + std::to_string(fs));
        emit("sub sp, sp, t0");
    }
    // 保存 ra / 旧 s0 于帧顶（sp 相对）
    auto storeSpRel = [&](const std::string &reg, int off) {
        if (fitsImm12(off)) {
            emitMem("sd " + reg + ", " + std::to_string(off) + "(sp)", false);
        } else {
            emit("li t1, " + std::to_string(off));
            emit("add t1, sp, t1");
            emitMem("sd " + reg + ", 0(t1)", false);
        }
    };
    storeSpRel("ra", fs - 8);
    storeSpRel("s0", fs - 16);
    // s0 = sp + fs（帧指针指向调用者 sp）
    if (fitsImm12(fs)) {
        emit("addi s0, sp, " + std::to_string(fs));
    } else {
        emit("add s0, sp, t0");  // t0 仍持有 fs
    }

    // 保存被占用的 callee-saved（在搬入实参覆盖它们之前）。整型 sd、浮点 fsd。
    for (size_t k = 0; k < usedCalleeSaved_.size(); ++k) {
        const std::string &r = usedCalleeSaved_[k];
        int off = -(24 + 8 * static_cast<int>(k));
        bool isF = !r.empty() && r[0] == 'f';
        emitMem((isF ? "fsd " : "sd ") + r + ", " + std::to_string(off) + "(s0)", false);
    }

    // 入参搬入：已分配寄存器的形参搬到其物理寄存器，溢出的写入栈槽。
    // 实参源寄存器(a*/fa*)与被分配寄存器(s*/fs*)不相交，顺序搬移即安全。
    int gp = 0, fp = 0, stackIn = 0;
    for (auto *arg : func_->arguments_) {
        bool isFloat = arg->type_ && arg->type_->tid_ == Type::FloatTyID;
        bool isUsed = !arg->use_list_.empty();
        if (isFloat) {
            bool inReg = fp < kNumFpArgRegs;
            std::string src = inReg ? fpArgRegs()[fp] : std::string();
            if (!isUsed) {
                // Keep ABI position accounting even when no move is needed.
            } else if (hasReg(arg)) {
                if (inReg) emit("fmv.s " + regOf(arg) + ", " + src);
                else emitLoadSlot(regOf(arg), stackIn, SlotKind::Float);
            } else if (inReg) {
                emitStoreSlot(src, slotOf(arg), SlotKind::Float);
            } else {
                emitLoadSlot(scratch::fp0(), stackIn, SlotKind::Float);
                emitStoreSlot(scratch::fp0(), slotOf(arg), SlotKind::Float);
            }
            if (inReg) fp++; else stackIn += 8;
        } else {
            SlotKind kind = slotKindOf(arg);
            bool inReg = gp < kNumIntArgRegs;
            std::string src = inReg ? intArgRegs()[gp] : std::string();
            if (!isUsed) {
                // Keep ABI position accounting even when no move is needed.
            } else if (hasReg(arg)) {
                if (inReg) emit("mv " + regOf(arg) + ", " + src);
                else emitLoadSlot(regOf(arg), stackIn, kind);
            } else if (inReg) {
                emitStoreSlot(src, slotOf(arg), kind);
            } else {
                emitLoadSlot(scratch::int0(), stackIn, kind);
                emitStoreSlot(scratch::int0(), slotOf(arg), kind);
            }
            if (inReg) gp++; else stackIn += 8;
        }
    }
}

void RiscvFuncContext::emitEpilogue() {
    int fs = frameSize_;
    // 恢复 callee-saved（s0 仍有效，用 s0 相对寻址）。
    for (size_t k = 0; k < usedCalleeSaved_.size(); ++k) {
        const std::string &r = usedCalleeSaved_[k];
        int off = -(24 + 8 * static_cast<int>(k));
        bool isF = !r.empty() && r[0] == 'f';
        emitMem((isF ? "fld " : "ld ") + r + ", " + std::to_string(off) + "(s0)", true);
    }
    auto loadSpRel = [&](const std::string &reg, int off) {
        if (fitsImm12(off)) {
            emitMem("ld " + reg + ", " + std::to_string(off) + "(sp)", true);
        } else {
            emit("li t1, " + std::to_string(off));
            emit("add t1, sp, t1");
            emitMem("ld " + reg + ", 0(t1)", true);
        }
    };
    loadSpRel("ra", fs - 8);
    loadSpRel("s0", fs - 16);
    if (fitsImm12(fs)) {
        emit("addi sp, sp, " + std::to_string(fs));
    } else {
        emit("li t0, " + std::to_string(fs));
        emit("add sp, sp, t0");
    }
    emitTerminator("ret");
}

void RiscvFuncContext::emitBlock(BasicBlock *bb) {
    curBlock_ = bb;
    emitLabel(blockLabel(bb));
    for (auto *inst : bb->instr_list_) {
        if (inst->is_phi()) continue;  // 在边上解析
        emitInstruction(inst);
    }
}

// ── 指令选择分派 ────────────────────────────────────────────────────────────

void RiscvFuncContext::emitInstruction(Instruction *inst) {
    switch (inst->op_id_) {
    case Instruction::Alloca:
        break;  // 仅占栈槽，地址按需计算
    case Instruction::Load:
        emitLoad(static_cast<LoadInst *>(inst));
        break;
    case Instruction::Store:
        emitStore(static_cast<StoreInst *>(inst));
        break;
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv:
    case Instruction::SRem:
    case Instruction::UDiv:
    case Instruction::URem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
        emitBinary(inst);
        break;
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
        emitFloatBinary(inst);
        break;
    case Instruction::FNeg:
        loadFloat(inst->get_operand(0), scratch::fp0());
        emit("fneg.s " + std::string(scratch::fp0()) + ", " + scratch::fp0());
        storeResult(inst, scratch::fp0());
        break;
    case Instruction::ICmp: {
        auto *cmp = static_cast<ICmpInst *>(inst);
        // 融合进紧邻的条件分支时不物化布尔结果，留给 emitBranch 直接发射。
        if (cmp->use_list_.size() == 1) {
            auto *br = dynamic_cast<BranchInst *>(cmp->use_list_.front().val_);
            if (br && br->num_ops_ == 3 && fuseCmpBranch(cmp, br)) break;
        }
        emitICmp(cmp);
        break;
    }
    case Instruction::FCmp:
        emitFCmp(static_cast<FCmpInst *>(inst));
        break;
    case Instruction::Br:
        emitBranch(static_cast<BranchInst *>(inst));
        break;
    case Instruction::Ret:
        emitReturn(static_cast<ReturnInst *>(inst));
        break;
    case Instruction::Call:
        emitCall(static_cast<CallInst *>(inst));
        break;
    case Instruction::GetElementPtr:
        emitGep(static_cast<GetElementPtrInst *>(inst));
        break;
    case Instruction::ZExt:
        // i1 → i32：值已是 0/1，直接搬运。
        loadInt(inst->get_operand(0), scratch::int0());
        storeResult(inst, scratch::int0());
        break;
    case Instruction::FPtoSI:
        loadFloat(inst->get_operand(0), scratch::fp0());
        emit("fcvt.w.s " + std::string(scratch::int0()) + ", " + scratch::fp0() + ", rtz");
        storeResult(inst, scratch::int0());
        break;
    case Instruction::SItoFP:
        loadInt(inst->get_operand(0), scratch::int0());
        emit("fcvt.s.w " + std::string(scratch::fp0()) + ", " + scratch::int0());
        storeResult(inst, scratch::fp0());
        break;
    case Instruction::BitCast:
        loadAddr(inst->get_operand(0), scratch::addr0());
        storeResult(inst, scratch::addr0());
        break;
    case Instruction::Select:
        emitSelect(static_cast<SelectInst *>(inst));
        break;
    case Instruction::PHI:
        break;  // 由 emitPhiCopies 处理
    default:
        throw std::runtime_error("RISC-V backend: unsupported IR opcode " +
                                 std::to_string(inst->op_id_));
    }
}

bool RiscvFuncContext::tryEmitConstDivRem(Instruction *inst) {
    const auto op = inst->op_id_;
    const bool isSDiv = op == Instruction::SDiv;
    const bool isSRem = op == Instruction::SRem;
    if (!isSDiv && !isSRem) return false;  // unsigned 留给硬件（SysY 无 unsigned）

    auto *rc = dynamic_cast<ConstantInt *>(inst->get_operand(1));
    if (!rc) return false;
    const int divisor = rc->value_;

    Magic::SignedDivisorInfo info = Magic::analyzeDivisor(divisor);
    if (!info.reducible) return false;  // 0 / INT_MIN：保留 divw/remw 语义

    const std::string n = scratch::int0();    // 被除数（始终保留）
    const std::string tmp = scratch::int1();
    const std::string q = scratch::int2();    // 商
    loadInt(inst->get_operand(0), n);

    if (info.powerOfTwo) {
        // 有符号 n / 2^k：向零舍入需对负数加偏置 (2^k - 1)。
        const int k = info.shift;
        if (k == 0) {
            // |divisor| == 1：商即 ±n。
            if (divisor < 0) emit("negw " + q + ", " + n);
            else emit("mv " + q + ", " + n);
        } else {
            emit("sraiw " + tmp + ", " + n + ", 31");
            emit("srliw " + tmp + ", " + tmp + ", " + std::to_string(32 - k));
            emit("addw " + tmp + ", " + n + ", " + tmp);
            emit("sraiw " + q + ", " + tmp + ", " + std::to_string(k));
            if (divisor < 0) emit("negw " + q + ", " + q);
        }
    } else {
        // Magic-number 乘高序列（Hacker's Delight）。n、M 均为符号扩展的 32 位，
        // 64 位 mul 得到精确乘积，srai 32 取高字，再按策略加/减并移位。
        Magic::MagicNumber mag = Magic::getMagic(divisor);
        emit("li " + tmp + ", " + std::to_string(mag.multiplier));
        emit("mul " + q + ", " + n + ", " + tmp);
        emit("srai " + q + ", " + q + ", 32");
        if (mag.strat == Magic::MagicStrat::MULTIPLY_ADD_SHIFT)
            emit("addw " + q + ", " + q + ", " + n);
        else if (mag.strat == Magic::MagicStrat::MULTIPLY_SUB_SHIFT)
            emit("subw " + q + ", " + q + ", " + n);
        if (mag.shift > 0)
            emit("sraiw " + q + ", " + q + ", " + std::to_string(mag.shift));
        // 末尾修正加的是“移位结果”的符号位（非被除数 n）：负除数时商与 n 异号，
        // 用 n 的符号会差 1。正除数下两者同号，此式同样正确。
        emit("srliw " + tmp + ", " + q + ", 31");
        emit("addw " + q + ", " + q + ", " + tmp);
    }

    if (isSDiv) {
        storeResult(inst, q);
        return true;
    }
    // 余数：r = n - q * divisor（对任意符号、2 的幂均正确）。
    emit("li " + tmp + ", " + std::to_string(divisor));
    emit("mulw " + tmp + ", " + q + ", " + tmp);
    emit("subw " + n + ", " + n + ", " + tmp);
    storeResult(inst, n);
    return true;
}

void RiscvFuncContext::emitBinary(Instruction *inst) {
    auto *lhsConst = dynamic_cast<ConstantInt *>(inst->get_operand(0));
    auto *rhsConst = dynamic_cast<ConstantInt *>(inst->get_operand(1));

    // Exact algebraic identities whose RV32 word semantics match the IR for
    // every input, including INT_MIN. These reduce instruction count without
    // relying on a target latency guess.
    if (inst->op_id_ == Instruction::Mul && (lhsConst || rhsConst)) {
        ConstantInt *constant = rhsConst ? rhsConst : lhsConst;
        Value *other = rhsConst ? inst->get_operand(0) : inst->get_operand(1);
        int value = constant->value_;
        if (value == 0) {
            emit("li " + std::string(scratch::int0()) + ", 0");
            storeResult(inst, scratch::int0());
            return;
        }
        if (value == 1) {
            loadInt(other, scratch::int0());
            storeResult(inst, scratch::int0());
            return;
        }
        if (value == -1) {
            loadInt(other, scratch::int0());
            emit("negw " + std::string(scratch::int0()) + ", " + scratch::int0());
            storeResult(inst, scratch::int0());
            return;
        }
        if (value > 0 && (value & (value - 1)) == 0) {
            int shift = 0;
            while ((1U << shift) != static_cast<unsigned>(value)) ++shift;
            loadInt(other, scratch::int0());
            emit("slliw " + std::string(scratch::int0()) + ", " + scratch::int0() +
                 ", " + std::to_string(shift));
            storeResult(inst, scratch::int0());
            return;
        }
    }

    if ((inst->op_id_ == Instruction::SDiv ||
         inst->op_id_ == Instruction::UDiv) && rhsConst && rhsConst->value_ == 1) {
        loadInt(inst->get_operand(0), scratch::int0());
        storeResult(inst, scratch::int0());
        return;
    }
    if (inst->op_id_ == Instruction::SDiv && rhsConst && rhsConst->value_ == -1) {
        loadInt(inst->get_operand(0), scratch::int0());
        emit("negw " + std::string(scratch::int0()) + ", " + scratch::int0());
        storeResult(inst, scratch::int0());
        return;
    }
    if (rhsConst &&
        ((inst->op_id_ == Instruction::SRem &&
          (rhsConst->value_ == 1 || rhsConst->value_ == -1)) ||
         (inst->op_id_ == Instruction::URem && rhsConst->value_ == 1))) {
        emit("li " + std::string(scratch::int0()) + ", 0");
        storeResult(inst, scratch::int0());
        return;
    }

    if (tryEmitConstDivRem(inst)) return;

    Value *registerOperand = nullptr;
    long immediate = 0;
    const char *immMnemonic = nullptr;

    switch (inst->op_id_) {
    case Instruction::Add:
        if (rhsConst && fitsImm12(rhsConst->value_)) {
            registerOperand = inst->get_operand(0);
            immediate = rhsConst->value_;
            immMnemonic = "addiw";
        } else if (lhsConst && fitsImm12(lhsConst->value_)) {
            registerOperand = inst->get_operand(1);
            immediate = lhsConst->value_;
            immMnemonic = "addiw";
        }
        break;
    case Instruction::Sub:
        if (rhsConst) {
            long negated = -static_cast<long>(rhsConst->value_);
            if (fitsImm12(negated)) {
                registerOperand = inst->get_operand(0);
                immediate = negated;
                immMnemonic = "addiw";
            }
        }
        break;
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
        ConstantInt *constant = rhsConst ? rhsConst : lhsConst;
        if (constant && fitsImm12(constant->value_)) {
            registerOperand = rhsConst ? inst->get_operand(0) : inst->get_operand(1);
            immediate = constant->value_;
            immMnemonic = inst->op_id_ == Instruction::And ? "andi" :
                          inst->op_id_ == Instruction::Or ? "ori" : "xori";
        }
        break;
    }
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
        if (rhsConst && rhsConst->value_ >= 0 && rhsConst->value_ < 32) {
            registerOperand = inst->get_operand(0);
            immediate = rhsConst->value_;
            immMnemonic = inst->op_id_ == Instruction::Shl ? "slliw" :
                          inst->op_id_ == Instruction::LShr ? "srliw" : "sraiw";
        }
        break;
    default:
        break;
    }

    if (immMnemonic) {
        loadInt(registerOperand, scratch::int0());
        emit(std::string(immMnemonic) + " " + scratch::int0() + ", " +
             scratch::int0() + ", " + std::to_string(immediate));
        storeResult(inst, scratch::int0());
        return;
    }

    const char *mn = "addw";
    switch (inst->op_id_) {
    case Instruction::Add:  mn = "addw"; break;
    case Instruction::Sub:  mn = "subw"; break;
    case Instruction::Mul:  mn = "mulw"; break;
    case Instruction::SDiv: mn = "divw"; break;
    case Instruction::SRem: mn = "remw"; break;
    case Instruction::UDiv: mn = "divuw"; break;
    case Instruction::URem: mn = "remuw"; break;
    case Instruction::Shl:  mn = "sllw"; break;
    case Instruction::LShr: mn = "srlw"; break;
    case Instruction::AShr: mn = "sraw"; break;
    case Instruction::And:  mn = "and"; break;
    case Instruction::Or:   mn = "or"; break;
    case Instruction::Xor:  mn = "xor"; break;
    default: break;
    }
    loadInt(inst->get_operand(0), scratch::int0());
    loadInt(inst->get_operand(1), scratch::int1());
    emit(std::string(mn) + " " + scratch::int0() + ", " + scratch::int0() + ", " + scratch::int1());
    storeResult(inst, scratch::int0());
}

void RiscvFuncContext::emitFloatBinary(Instruction *inst) {
    const char *mn = "fadd.s";
    switch (inst->op_id_) {
    case Instruction::FAdd: mn = "fadd.s"; break;
    case Instruction::FSub: mn = "fsub.s"; break;
    case Instruction::FMul: mn = "fmul.s"; break;
    case Instruction::FDiv: mn = "fdiv.s"; break;
    default: break;
    }
    loadFloat(inst->get_operand(0), scratch::fp0());
    loadFloat(inst->get_operand(1), scratch::fp1());
    emit(std::string(mn) + " " + scratch::fp0() + ", " + scratch::fp0() + ", " + scratch::fp1());
    storeResult(inst, scratch::fp0());
}

void RiscvFuncContext::emitICmp(ICmpInst *inst) {
    // 输入按需用 zero 顶替常量 0；结果固定写入 int0（不会是 zero）。
    const std::string a = intOperand(inst->get_operand(0), scratch::int0());
    const std::string b = intOperand(inst->get_operand(1), scratch::int1());
    const std::string d = scratch::int0();
    switch (inst->icmp_op_) {
    case ICmpInst::ICMP_EQ:
        emit("subw " + d + ", " + a + ", " + b);
        emit("seqz " + d + ", " + d);
        break;
    case ICmpInst::ICMP_NE:
        emit("subw " + d + ", " + a + ", " + b);
        emit("snez " + d + ", " + d);
        break;
    case ICmpInst::ICMP_SLT:
        emit("slt " + d + ", " + a + ", " + b);
        break;
    case ICmpInst::ICMP_SGT:
        emit("slt " + d + ", " + b + ", " + a);
        break;
    case ICmpInst::ICMP_SLE:
        emit("slt " + d + ", " + b + ", " + a);
        emit("xori " + d + ", " + d + ", 1");
        break;
    case ICmpInst::ICMP_SGE:
        emit("slt " + d + ", " + a + ", " + b);
        emit("xori " + d + ", " + d + ", 1");
        break;
    case ICmpInst::ICMP_ULT:
        emit("sltu " + d + ", " + a + ", " + b);
        break;
    case ICmpInst::ICMP_UGT:
        emit("sltu " + d + ", " + b + ", " + a);
        break;
    case ICmpInst::ICMP_ULE:
        emit("sltu " + d + ", " + b + ", " + a);
        emit("xori " + d + ", " + d + ", 1");
        break;
    case ICmpInst::ICMP_UGE:
        emit("sltu " + d + ", " + a + ", " + b);
        emit("xori " + d + ", " + d + ", 1");
        break;
    }
    storeResult(inst, d);
}

void RiscvFuncContext::emitFCmp(FCmpInst *inst) {
    loadFloat(inst->get_operand(0), scratch::fp0());
    loadFloat(inst->get_operand(1), scratch::fp1());
    const std::string d = scratch::int0(), x = scratch::fp0(), y = scratch::fp1();
    switch (inst->fcmp_op_) {
    case FCmpInst::FCMP_OEQ:
    case FCmpInst::FCMP_UEQ:
        emit("feq.s " + d + ", " + x + ", " + y);
        break;
    case FCmpInst::FCMP_ONE:
    case FCmpInst::FCMP_UNE:
        emit("feq.s " + d + ", " + x + ", " + y);
        emit("xori " + d + ", " + d + ", 1");
        break;
    case FCmpInst::FCMP_OLT:
    case FCmpInst::FCMP_ULT:
        emit("flt.s " + d + ", " + x + ", " + y);
        break;
    case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_UGT:
        emit("flt.s " + d + ", " + y + ", " + x);
        break;
    case FCmpInst::FCMP_OLE:
    case FCmpInst::FCMP_ULE:
        emit("fle.s " + d + ", " + x + ", " + y);
        break;
    case FCmpInst::FCMP_OGE:
    case FCmpInst::FCMP_UGE:
        emit("fle.s " + d + ", " + y + ", " + x);
        break;
    default:
        emit("li " + d + ", 0");
        break;
    }
    storeResult(inst, d);
}

bool RiscvFuncContext::fuseCmpBranch(ICmpInst *cmp, BranchInst *br) const {
    if (!cmp || cmp->use_list_.size() != 1) return false;
    if (cmp->parent_ != br->parent_) return false;
    // cmp 必须是 br 的前一条指令：中间没有其它指令会重定义 cmp 操作数所在的
    // 寄存器，因此把对操作数的读取下沉到分支点仍能读到正确的值。
    auto &list = br->parent_->instr_list_;
    auto it = std::find(list.begin(), list.end(), static_cast<Instruction *>(br));
    if (it == list.begin() || it == list.end()) return false;
    return *std::prev(it) == static_cast<Instruction *>(cmp);
}

void RiscvFuncContext::emitBranch(BranchInst *inst) {
    if (inst->num_ops_ == 1) {
        auto *succ = static_cast<BasicBlock *>(inst->get_operand(0));
        emitPhiCopies(curBlock_, succ);
        emitTerminator("j " + blockLabel(succ));
        return;
    }
    Value *cond = inst->get_operand(0);
    auto *trueBB = static_cast<BasicBlock *>(inst->get_operand(1));
    auto *falseBB = static_cast<BasicBlock *>(inst->get_operand(2));

    // 比较与分支融合：条件是仅被本分支使用、且紧邻其前的整型比较时，直接对其
    // 两个操作数发射 RISC-V 条件分支，省去 slt/seqz + 布尔回存/重载。
    const char *cmpMn = nullptr;
    bool cmpSwap = false;
    if (auto *cmp = dynamic_cast<ICmpInst *>(cond);
        cmp && fuseCmpBranch(cmp, inst) &&
        icmpToBranch(cmp->icmp_op_, cmpMn, cmpSwap)) {
        std::string a = intOperand(cmp->get_operand(0), scratch::int0());
        std::string b = intOperand(cmp->get_operand(1), scratch::int1());
        std::string r1 = cmpSwap ? b : a;
        std::string r2 = cmpSwap ? a : b;

        bool tphi = succHasPhi(trueBB), fphi = succHasPhi(falseBB);
        if (!tphi && !fphi) {
            emit(std::string(cmpMn) + " " + r1 + ", " + r2 + ", " + blockLabel(trueBB));
            emitTerminator("j " + blockLabel(falseBB));
            return;
        }
        // 关键边：比较为假时跳到 false 拷贝块，否则贯穿到 true 拷贝。
        int id = edgeCounter_++;
        std::string fedge = ".Ledge_" + func_->name_ + "_" + std::to_string(id);
        emit(std::string(invertBranch(cmpMn)) + " " + r1 + ", " + r2 + ", " + fedge);
        emitPhiCopies(curBlock_, trueBB);
        emit("j " + blockLabel(trueBB));
        emitLabel(fedge);
        emitPhiCopies(curBlock_, falseBB);
        emitTerminator("j " + blockLabel(falseBB));
        return;
    }

    loadInt(cond, scratch::int0());

    bool tphi = succHasPhi(trueBB), fphi = succHasPhi(falseBB);
    if (!tphi && !fphi) {
        emit("bnez " + std::string(scratch::int0()) + ", " + blockLabel(trueBB));
        emitTerminator("j " + blockLabel(falseBB));
        return;
    }
    // 关键边：经由独立的拷贝块发射各自方向的 phi 并行拷贝，避免误写。
    int id = edgeCounter_++;
    std::string fedge = ".Ledge_" + func_->name_ + "_" + std::to_string(id);
    emit("beqz " + std::string(scratch::int0()) + ", " + fedge);
    emitPhiCopies(curBlock_, trueBB);
    emit("j " + blockLabel(trueBB));
    emitLabel(fedge);
    emitPhiCopies(curBlock_, falseBB);
    emitTerminator("j " + blockLabel(falseBB));
}

void RiscvFuncContext::emitReturn(ReturnInst *inst) {
    if (inst->num_ops_ == 1) {
        Value *rv = inst->get_operand(0);
        if (rv->type_ && rv->type_->tid_ == Type::FloatTyID)
            loadFloat(rv, fpRetReg());
        else
            loadInt(rv, intRetReg());
    }
    emitEpilogue();
}

void RiscvFuncContext::emitCall(CallInst *inst) {
    auto *callee = static_cast<Function *>(inst->get_operand(inst->num_ops_ - 1));
    unsigned nArgs = inst->num_ops_ - 1;

    int gp = 0, fp = 0, stackOff = 0;
    for (unsigned i = 0; i < nArgs; ++i) {
        Value *a = inst->get_operand(i);
        bool isFloat = a->type_ && a->type_->tid_ == Type::FloatTyID;
        if (isFloat) {
            if (fp < kNumFpArgRegs) {
                loadFloat(a, fpArgRegs()[fp]);
                fp++;
            } else {
                loadFloat(a, scratch::fp0());
                emitStoreSlot(scratch::fp0(), stackOff, SlotKind::Float, "sp");
                stackOff += 8;
            }
        } else {
            bool isPtr = a->type_ && a->type_->tid_ == Type::PointerTyID;
            if (gp < kNumIntArgRegs) {
                if (isPtr) loadAddr(a, intArgRegs()[gp]);
                else loadInt(a, intArgRegs()[gp]);
                gp++;
            } else {
                if (isPtr) loadAddr(a, scratch::int0());
                else loadInt(a, scratch::int0());
                emitStoreSlot(scratch::int0(), stackOff, SlotKind::Pointer, "sp");
                stackOff += 8;
            }
        }
    }

    emitCall("call " + callee->name_);

    if (!inst->is_void()) {
        if (inst->type_ && inst->type_->tid_ == Type::FloatTyID)
            storeResult(inst, fpRetReg());
        else
            storeResult(inst, intRetReg());
    }
}

void RiscvFuncContext::emitGep(GetElementPtrInst *inst) {
    Value *base = inst->get_operand(0);
    loadAddr(base, scratch::addr0());

    Type *cur = static_cast<PointerType *>(base->type_)->contained_;
    unsigned nIdx = inst->num_ops_ - 1;
    for (unsigned k = 0; k < nIdx; ++k) {
        int stride;
        if (k == 0) {
            stride = sizeOfType(cur);
        } else {
            if (cur->tid_ == Type::ArrayTyID)
                cur = static_cast<ArrayType *>(cur)->contained_;
            stride = sizeOfType(cur);
        }
        Value *idx = inst->get_operand(k + 1);
        if (auto *ci = dynamic_cast<ConstantInt *>(idx)) {
            long delta = static_cast<long>(ci->value_) * stride;
            if (delta == 0) continue;
            emit("li " + std::string(scratch::int1()) + ", " + std::to_string(delta));
            emit("add " + std::string(scratch::addr0()) + ", " + scratch::addr0() + ", " + scratch::int1());
        } else {
            loadInt(idx, scratch::int0());
            if (stride > 0 && (stride & (stride - 1)) == 0) {
                int shift = 0;
                while ((1U << shift) != static_cast<unsigned>(stride)) ++shift;
                if (shift != 0)
                    emit("slli " + std::string(scratch::int0()) + ", " +
                         scratch::int0() + ", " + std::to_string(shift));
            } else {
                emit("li " + std::string(scratch::int1()) + ", " +
                     std::to_string(stride));
                emit("mul " + std::string(scratch::int0()) + ", " +
                     scratch::int0() + ", " + scratch::int1());
            }
            emit("add " + std::string(scratch::addr0()) + ", " + scratch::addr0() + ", " + scratch::int0());
        }
    }
    storeResult(inst, scratch::addr0());
}

void RiscvFuncContext::emitLoad(LoadInst *inst) {
    loadAddr(inst->get_operand(0), scratch::addr0());
    const std::string addr = "0(" + std::string(scratch::addr0()) + ")";
    if (inst->type_ && inst->type_->tid_ == Type::FloatTyID) {
        emitMem("flw " + std::string(scratch::fp0()) + ", " + addr, true);
        storeResult(inst, scratch::fp0());
    } else if (inst->type_ && inst->type_->tid_ == Type::PointerTyID) {
        emitMem("ld " + std::string(scratch::int0()) + ", " + addr, true);
        storeResult(inst, scratch::int0());
    } else {
        emitMem("lw " + std::string(scratch::int0()) + ", " + addr, true);
        storeResult(inst, scratch::int0());
    }
}

void RiscvFuncContext::emitStore(StoreInst *inst) {
    Value *val = inst->get_operand(0);
    Value *ptr = inst->get_operand(1);
    loadAddr(ptr, scratch::addr0());
    const std::string addr = "0(" + std::string(scratch::addr0()) + ")";
    if (val->type_ && val->type_->tid_ == Type::FloatTyID) {
        loadFloat(val, scratch::fp0());
        emitMem("fsw " + std::string(scratch::fp0()) + ", " + addr, false);
    } else if (val->type_ && val->type_->tid_ == Type::PointerTyID) {
        loadAddr(val, scratch::int0());
        emitMem("sd " + std::string(scratch::int0()) + ", " + addr, false);
    } else {
        loadInt(val, scratch::int0());
        emitMem("sw " + std::string(scratch::int0()) + ", " + addr, false);
    }
}

void RiscvFuncContext::emitSelect(SelectInst *inst) {
    loadInt(inst->get_operand(0), scratch::int0());
    int id = edgeCounter_++;
    std::string lfalse = ".Lsel_false_" + func_->name_ + "_" + std::to_string(id);
    std::string lend = ".Lsel_end_" + func_->name_ + "_" + std::to_string(id);

    auto materialize = [&](Value *v) {
        SlotKind kind = slotKindOf(inst);
        if (kind == SlotKind::Float) {
            loadFloat(v, scratch::fp0());
            storeResult(inst, scratch::fp0());
        } else if (kind == SlotKind::Pointer) {
            loadAddr(v, scratch::int0());
            storeResult(inst, scratch::int0());
        } else {
            loadInt(v, scratch::int0());
            storeResult(inst, scratch::int0());
        }
    };

    emit("beqz " + std::string(scratch::int0()) + ", " + lfalse);
    materialize(inst->get_operand(1));
    emit("j " + lend);
    emitLabel(lfalse);
    materialize(inst->get_operand(2));
    emitLabel(lend);
}

// ── phi 解析 ────────────────────────────────────────────────────────────────

bool RiscvFuncContext::succHasPhi(BasicBlock *succ) const {
    for (auto *inst : succ->instr_list_)
        if (inst->is_phi()) return true;
    return false;
}

void RiscvFuncContext::emitPhiCopies(BasicBlock *pred, BasicBlock *succ) {
    std::vector<std::pair<Instruction *, Value *>> pairs;
    for (auto *inst : succ->instr_list_) {
        if (!inst->is_phi()) continue;
        Value *src = nullptr;
        for (unsigned i = 0; i + 1 < inst->num_ops_; i += 2) {
            if (inst->get_operand(i + 1) == pred) {
                src = inst->get_operand(i);
                break;
            }
        }
        if (src) pairs.push_back({inst, src});
    }
    if (pairs.empty()) return;

    auto materializeInto = [&](Value *src, SlotKind kind, const std::string &reg) {
        if (kind == SlotKind::Float) loadFloat(src, reg);
        else if (kind == SlotKind::Pointer) loadAddr(src, reg);
        else loadInt(src, reg);
    };

    // 快速路径：当没有“某个源寄存器同时又是另一对的目标寄存器”的覆盖冲突时，
    // 直接发射寄存器/栈槽搬移即可（任意顺序安全），免去临时槽的 store/reload 往返。
    // 存在冲突（含寄存器循环、目标寄存器重复）时回退到经内存的两遍并行拷贝。
    bool hazard = false;
    std::set<std::string> dstRegs;
    std::set<Value *> dstVals;
    for (auto &pr : pairs) {
        dstVals.insert(pr.first);
        if (hasReg(pr.first) && !dstRegs.insert(regOf(pr.first)).second) hazard = true;
    }
    for (auto &pr : pairs) {
        // 源本身是本组的某个 phi 目标（含溢出到栈槽的 phi-读-phi 循环/交换）。
        if (pr.second != pr.first && dstVals.count(pr.second)) hazard = true;
        // 源寄存器是某目标寄存器：仅当它正是本对自身目标(自拷贝)时无害。
        if (hasReg(pr.second)) {
            const std::string sr = regOf(pr.second);
            if (dstRegs.count(sr) && !(hasReg(pr.first) && regOf(pr.first) == sr))
                hazard = true;
        }
    }

    if (!hazard) {
        for (auto &pr : pairs) {
            Instruction *phi = pr.first;
            SlotKind kind = slotKindOf(phi);
            if (hasReg(phi)) {
                materializeInto(pr.second, kind, regOf(phi));  // 自拷贝由 loadX 内部跳过
            } else {
                const std::string scratchReg =
                    kind == SlotKind::Float ? scratch::fp0() : scratch::int0();
                materializeInto(pr.second, kind, scratchReg);
                storeResult(phi, scratchReg);
            }
        }
        return;
    }

    // 第一遍：把所有源读入 phi 临时槽。
    for (size_t i = 0; i < pairs.size(); ++i) {
        Instruction *phi = pairs[i].first;
        Value *src = pairs[i].second;
        int tmp = phiTempBase_ + static_cast<int>(i) * 8;
        SlotKind kind = slotKindOf(phi);
        if (kind == SlotKind::Float) {
            loadFloat(src, scratch::fp0());
            emitStoreSlot(scratch::fp0(), tmp, SlotKind::Float);
        } else if (kind == SlotKind::Pointer) {
            loadAddr(src, scratch::int0());
            emitStoreSlot(scratch::int0(), tmp, SlotKind::Pointer);
        } else {
            loadInt(src, scratch::int0());
            emitStoreSlot(scratch::int0(), tmp, SlotKind::Int32);
        }
    }
    // 第二遍：从临时槽写入各 phi 结果槽。
    for (size_t i = 0; i < pairs.size(); ++i) {
        Instruction *phi = pairs[i].first;
        int tmp = phiTempBase_ + static_cast<int>(i) * 8;
        SlotKind kind = slotKindOf(phi);
        if (kind == SlotKind::Float) {
            emitLoadSlot(scratch::fp0(), tmp, SlotKind::Float);
            storeResult(phi, scratch::fp0());
        } else if (kind == SlotKind::Pointer) {
            emitLoadSlot(scratch::int0(), tmp, SlotKind::Pointer);
            storeResult(phi, scratch::int0());
        } else {
            emitLoadSlot(scratch::int0(), tmp, SlotKind::Int32);
            storeResult(phi, scratch::int0());
        }
    }
}
