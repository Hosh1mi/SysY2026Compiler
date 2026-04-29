#include "../../include/arm/arm.hpp"
#include <algorithm>
#include <cstdint>
#include <iomanip>

static std::string opReg(const std::string& r) { return r; }

void ArmBuilder::emit(const std::string& s) { out_ << s; }
void ArmBuilder::emitLine(const std::string& s) { out_ << s << '\n'; }

bool ArmBuilder::fitsImm12(int64_t v) const { return v >= 0 && v < 4096; }
bool ArmBuilder::isPow2(int64_t v) const { return v > 0 && (v & (v - 1)) == 0; }

std::string ArmBuilder::escapeLabel(const std::string& s) const { return s; }

int ArmBuilder::typeSize(Type* ty) const {
    if (!ty) return 8;
    switch (ty->tid_) {
        case Type::IntegerTyID: return static_cast<IntegerType*>(ty)->num_bits_ == 1 ? 1 : 4;
        case Type::FloatTyID: return 4;
        case Type::PointerTyID: return 8;
        case Type::ArrayTyID: return static_cast<ArrayType*>(ty)->num_elements_ * typeSize(static_cast<ArrayType*>(ty)->contained_);
        default: return 8;
    }
}

int ArmBuilder::stackSlotForType(Type* ty) const { return ((typeSize(ty) + 7) / 8) * 8; }

int ArmBuilder::stackSlot(Value* v, int size) {
    auto& info = locations_[v];
    if (info.stack_offset < 0) {
        next_stack_offset_ = ((next_stack_offset_ + size + 7) / 8) * 8;
        info.stack_offset = next_stack_offset_;
    }
    return info.stack_offset;
}

std::string ArmBuilder::regFor(Value* v) { return regFor(v, "x9"); }

std::string ArmBuilder::regFor(Value* v, const std::string& preferred) {
    auto& info = locations_[v];
    if (info.in_reg) return info.reg;
    info.in_reg = true;
    info.reg = preferred;
    if (info.stack_offset < 0) stackSlot(v, 8);
    return info.reg;
}

std::string ArmBuilder::lowerConstantInt(ConstantInt* c, const std::string& preferred) {
    auto reg = preferred;
    if (c->value_ == 0) {
        emitLine("mov " + reg + ", xzr");
        return reg;
    }
    if (fitsImm12(c->value_)) {
        emitLine("mov " + reg + ", #" + std::to_string(c->value_));
        return reg;
    }
    uint32_t u = static_cast<uint32_t>(c->value_);
    emitLine("movz " + reg + ", #" + std::to_string(u & 0xffff));
    if (u >> 16) emitLine("movk " + reg + ", #" + std::to_string((u >> 16) & 0xffff) + ", lsl #16");
    return reg;
}

std::string ArmBuilder::lowerValue(Value* v, const std::string& preferred) {
    if (auto c = dynamic_cast<ConstantInt*>(v)) return lowerConstantInt(c, preferred);
    if (auto c = dynamic_cast<ConstantZero*>(v)) { emitLine("mov " + preferred + ", xzr"); return preferred; }
    if (locations_.count(v) && locations_[v].in_reg) return locations_[v].reg;
    auto reg = regFor(v, preferred);
    auto off = stackSlot(v, 8);
    emitLine("ldr " + reg + ", [sp, #" + std::to_string(off) + "]");
    return reg;
}

void ArmBuilder::spillAll() {
    for (auto it = locations_.begin(); it != locations_.end(); ++it) {
        auto v = it->first;
        auto& info = it->second;
        if (info.in_reg) {
            emitLine("str " + info.reg + ", [sp, #" + std::to_string(stackSlot(v, 8)) + "]");
            info.in_reg = false;
        }
    }
}

void ArmBuilder::emitPrologue(int stack_size) {
    emitLine("stp x29, x30, [sp, #-16]!");
    emitLine("mov x29, sp");
    if (stack_size > 0) emitLine("sub sp, sp, #" + std::to_string(stack_size));
}

void ArmBuilder::emitEpilogue() {
    emitLine("mov sp, x29");
    emitLine("ldp x29, x30, [sp], #16");
    emitLine("ret");
}

std::string ArmBuilder::lowerCmpCond(ICmpInst::ICmpOp op) {
    switch (op) {
        case ICmpInst::ICMP_EQ: return "eq";
        case ICmpInst::ICMP_NE: return "ne";
        case ICmpInst::ICMP_SGT: return "gt";
        case ICmpInst::ICMP_SGE: return "ge";
        case ICmpInst::ICMP_SLT: return "lt";
        case ICmpInst::ICMP_SLE: return "le";
        default: return "eq";
    }
}

std::string ArmBuilder::lowerFcmpCond(FCmpInst::FCmpOp op) { return lowerCmpCond(ICmpInst::ICMP_EQ); }

void ArmBuilder::lowerInstr(Instruction* inst) {
    if (auto alloca = dynamic_cast<AllocaInst*>(inst)) {
        stackSlot(inst, stackSlotForType(alloca->alloca_ty_));
        return;
    }
    if (auto load = dynamic_cast<LoadInst*>(inst)) {
        auto dst = regFor(inst, "x9");
        auto ptr = lowerValue(load->get_operand(0), "x10");
        emitLine("ldr " + dst + ", [" + ptr + "]");
        return;
    }
    if (auto store = dynamic_cast<StoreInst*>(inst)) {
        auto val = lowerValue(store->get_operand(0), "x9");
        auto ptr = lowerValue(store->get_operand(1), "x10");
        emitLine("str " + val + ", [" + ptr + "]");
        return;
    }
    if (inst->is_add() || inst->is_sub() || inst->is_mul() || inst->is_div()) {
        auto dst = regFor(inst, "x9");
        auto lhs = lowerValue(inst->get_operand(0), "x10");
        auto rhs = lowerValue(inst->get_operand(1), "x11");
        if (inst->is_add()) emitLine("add " + dst + ", " + lhs + ", " + rhs);
        else if (inst->is_sub()) emitLine("sub " + dst + ", " + lhs + ", " + rhs);
        else if (inst->is_mul()) emitLine("mul " + dst + ", " + lhs + ", " + rhs);
        else emitLine("sdiv " + dst + ", " + lhs + ", " + rhs);
        return;
    }
    if (auto cmp = dynamic_cast<ICmpInst*>(inst)) {
        auto lhs = lowerValue(cmp->get_operand(0), "x10");
        auto rhs = lowerValue(cmp->get_operand(1), "x11");
        emitLine("cmp " + lhs + ", " + rhs);
        auto dst = regFor(inst, "w9");
        emitLine("cset " + dst + ", " + lowerCmpCond(cmp->icmp_op_));
        return;
    }
    if (auto call = dynamic_cast<CallInst*>(inst)) {
        spillAll();
        auto callee = dynamic_cast<Function*>(call->get_operand(call->num_ops_ - 1));
        for (unsigned i = 0; i + 1 < call->num_ops_ && i < 8; ++i) {
            auto argreg = lowerValue(call->get_operand(i), "x" + std::to_string(i));
            if (argreg != "x" + std::to_string(i)) emitLine("mov x" + std::to_string(i) + ", " + argreg);
        }
        emitLine("bl " + callee->name_);
        if (!inst->is_void()) {
            auto dst = regFor(inst, "x9");
            emitLine("mov " + dst + ", x0");
        }
        return;
    }
    if (auto br = dynamic_cast<BranchInst*>(inst)) {
        if (br->num_ops_ == 1) {
            auto target = dynamic_cast<BasicBlock*>(br->get_operand(0));
            emitLine("b " + bb_labels_[target]);
        } else {
            auto cond = lowerValue(br->get_operand(0), "w9");
            emitLine("cmp " + cond + ", #0");
            auto t = dynamic_cast<BasicBlock*>(br->get_operand(1));
            auto f = dynamic_cast<BasicBlock*>(br->get_operand(2));
            emitLine("b.ne " + bb_labels_[t]);
            emitLine("b " + bb_labels_[f]);
        }
        return;
    }
    if (auto ret = dynamic_cast<ReturnInst*>(inst)) {
        if (ret->num_ops_ == 1) {
            auto rv = lowerValue(ret->get_operand(0), "x0");
            if (rv != "x0") emitLine("mov x0, " + rv);
        }
        emitEpilogue();
        return;
    }
    if (auto gep = dynamic_cast<GetElementPtrInst*>(inst)) {
        auto dst = regFor(inst, "x9");
        auto base = lowerValue(gep->get_operand(0), "x10");
        emitLine("mov " + dst + ", " + base);
        return;
    }
}

void ArmBuilder::lowerBlock(BasicBlock* bb) {
    emitLine(bb_labels_[bb] + ":");
    for (auto inst : bb->instr_list_) lowerInstr(inst);
}

void ArmBuilder::emitGlobal(GlobalVariable* g) {
    emitLine(".data");
    emitLine(".global " + g->name_);
    emitLine(g->name_ + ":");
    if (auto init = dynamic_cast<ConstantInt*>(g->init_val_)) {
        emitLine(".word " + std::to_string(init->value_));
    } else {
        emitLine(".word 0");
    }
}

void ArmBuilder::emitFunction(Function* f) {
    locations_.clear();
    next_stack_offset_ = 0;
    bb_labels_.clear();
    for (auto bb : f->basic_blocks_) bb_labels_[bb] = escapeLabel(bb->name_);
    emitLine(".text");
    emitLine(".global " + f->name_);
    emitLine(f->name_ + ":");
    int stack_size = 256;
    emitPrologue(stack_size);
    for (auto bb : f->basic_blocks_) lowerBlock(bb);
}

std::string ArmBuilder::build(Module* m) {
    module_ = m;
    out_.str("");
    out_.clear();
    for (auto g : m->global_list_) emitGlobal(g);
    for (auto f : m->function_list_) if (!f->is_declaration()) emitFunction(f);
    return out_.str();
}
