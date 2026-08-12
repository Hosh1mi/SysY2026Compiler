#pragma once
// IR 构建器：提供工厂方法，在当前基本块中创建各类 IR 指令

#include "instruction.hpp"

// 带单一插入点的指令工厂。除 create_alloca 外，所有 create_* 都立即把新指令追加到
// BB_；调用前必须保证 BB_ 非空且尚未以 br/ret 终止。
class IRStmtBuilder {
public:
    BasicBlock* BB_;  // 当前插入点

    // 以 bb 作为初始插入块；允许先传 nullptr、使用前再 set_insert_point。
    explicit IRStmtBuilder(BasicBlock* bb) : BB_(bb) {}

    // 返回当前插入块，不改变状态。
    BasicBlock* get_insert_block() { return this->BB_; }
    // 后续 create_* 改为插入 bb，不会移动或补全旧块。
    void set_insert_point(BasicBlock* bb) { this->BB_ = bb; }

    // 创建同类型整数加法。
    BinaryInst* create_iadd(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::Add, v1, v2, this->BB_); }
    // 创建同类型整数减法。
    BinaryInst* create_isub(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::Sub, v1, v2, this->BB_); }
    // 创建同类型整数乘法。
    BinaryInst* create_imul(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::Mul, v1, v2, this->BB_); }
    // 创建有符号整数除法。
    BinaryInst* create_isdiv(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::SDiv, v1, v2, this->BB_); }
    // 创建有符号整数余数。
    BinaryInst* create_isrem(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::SRem, v1, v2, this->BB_); }

    // 创建整数相等比较。
    ICmpInst* create_icmp_eq(Value* v1, Value* v2) { return new ICmpInst(ICmpInst::ICMP_EQ, v1, v2, this->BB_); }
    // 创建整数不等比较。
    ICmpInst* create_icmp_ne(Value* v1, Value* v2) { return new ICmpInst(ICmpInst::ICMP_NE, v1, v2, this->BB_); }
    // 创建有符号大于比较。
    ICmpInst* create_icmp_gt(Value* v1, Value* v2) { return new ICmpInst(ICmpInst::ICMP_SGT, v1, v2, this->BB_); }
    // 创建有符号大于等于比较。
    ICmpInst* create_icmp_ge(Value* v1, Value* v2) { return new ICmpInst(ICmpInst::ICMP_SGE, v1, v2, this->BB_); }
    // 创建有符号小于比较。
    ICmpInst* create_icmp_lt(Value* v1, Value* v2) { return new ICmpInst(ICmpInst::ICMP_SLT, v1, v2, this->BB_); }
    // 创建有符号小于等于比较。
    ICmpInst* create_icmp_le(Value* v1, Value* v2) { return new ICmpInst(ICmpInst::ICMP_SLE, v1, v2, this->BB_); }

    // 创建同类型浮点加法。
    BinaryInst* create_fadd(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::FAdd, v1, v2, this->BB_); }
    // 创建同类型浮点减法。
    BinaryInst* create_fsub(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::FSub, v1, v2, this->BB_); }
    // 创建同类型浮点乘法。
    BinaryInst* create_fmul(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::FMul, v1, v2, this->BB_); }
    // 创建同类型浮点除法。
    BinaryInst* create_fdiv(Value* v1, Value* v2) { return new BinaryInst(v1->type_, Instruction::FDiv, v1, v2, this->BB_); }

    // 创建 unordered-or-equal 浮点比较，NaN 时结果为真。
    FCmpInst* create_fcmp_eq(Value* v1, Value* v2) { return new FCmpInst(FCmpInst::FCMP_UEQ, v1, v2, this->BB_); }
    // 创建 unordered-or-not-equal 浮点比较。
    FCmpInst* create_fcmp_ne(Value* v1, Value* v2) { return new FCmpInst(FCmpInst::FCMP_UNE, v1, v2, this->BB_); }
    // 创建 unordered-or-greater-than 浮点比较。
    FCmpInst* create_fcmp_gt(Value* v1, Value* v2) { return new FCmpInst(FCmpInst::FCMP_UGT, v1, v2, this->BB_); }
    // 创建 unordered-or-greater-or-equal 浮点比较。
    FCmpInst* create_fcmp_ge(Value* v1, Value* v2) { return new FCmpInst(FCmpInst::FCMP_UGE, v1, v2, this->BB_); }
    // 创建 unordered-or-less-than 浮点比较。
    FCmpInst* create_fcmp_lt(Value* v1, Value* v2) { return new FCmpInst(FCmpInst::FCMP_ULT, v1, v2, this->BB_); }
    // 创建 unordered-or-less-or-equal 浮点比较。
    FCmpInst* create_fcmp_le(Value* v1, Value* v2) { return new FCmpInst(FCmpInst::FCMP_ULE, v1, v2, this->BB_); }

    // 创建对 func 的调用；args 按函数参数顺序保存，返回值可直接作为 Value 使用。
    CallInst* create_call(Value* func, const std::vector<Value*>& args) {
#ifdef DEBUG
        assert(dynamic_cast<Function*>(func) && "func must be Function * type");
#endif
        return new CallInst(static_cast<Function*>(func), args, this->BB_);
    }

    // 创建无条件分支并同步当前块和目标块的 CFG 缓存。
    BranchInst* create_br(BasicBlock* if_true) { return new BranchInst(if_true, this->BB_); }
    // 创建条件分支；cond 应为 i1，两个目标的 CFG 缓存会同步更新。
    BranchInst* create_cond_br(Value* cond, BasicBlock* if_true, BasicBlock* if_false) {
        return new BranchInst(cond, if_true, if_false, this->BB_);
    }

    // 创建带值返回；val 类型应匹配当前函数返回类型。
    ReturnInst* create_ret(Value* val) { return new ReturnInst(val, this->BB_); }
    // 创建 void 返回。
    ReturnInst* create_void_ret() { return new ReturnInst(this->BB_); }

    // 创建普通 GEP；idxs 的聚合降层规则由 GetElementPtrInst 统一推导。
    GetElementPtrInst* create_gep(Value* ptr, const std::vector<Value*>& idxs) { return new GetElementPtrInst(ptr, idxs, this->BB_); }

    // 创建 store，要求 val 类型与 ptr 所指类型相同。
    StoreInst* create_store(Value* val, Value* ptr) { return new StoreInst(val, ptr, this->BB_); }
    // 创建 load，结果类型为 ptr 所指类型。
    LoadInst* create_load(Value* ptr) {
#ifdef DEBUG
        assert(ptr->get_type()->is_pointer_type() && "ptr must be pointer type");
#endif
        return new LoadInst(ptr, this->BB_);
    }

    // 创建栈槽：始终插入当前函数 entry 的 alloca 段末尾（与 BB_ 位置无关）。
    // 通过 Function::lastEntryAlloca_ 做 O(1) 追加；缓存失效时再线性扫描一次。
    AllocaInst* create_alloca(Type* ty) {
        Function* func = BB_->parent_;
        BasicBlock* entry = func->basic_blocks_.front();
        auto* inst = new AllocaInst(ty, entry, /*no_insert=*/true);

        AllocaInst* last = func->lastEntryAlloca_;
        if (last && last->parent_ == entry && last->is_alloca()) {
            entry->add_instruction_after_inst(inst, last);
        } else {
            Instruction* insertBefore = nullptr;
            for (auto* existing : entry->instr_list_) {
                if (!existing->is_alloca()) {
                    insertBefore = existing;
                    break;
                }
            }
            if (insertBefore)
                entry->add_instruction_before_inst(inst, insertBefore);
            else
                entry->add_instruction(inst);
        }
        func->lastEntryAlloca_ = inst;
        return inst;
    }

    // 创建零扩展整数转换。
    ZextInst* create_zext(Value* val, Type* ty) { return new ZextInst(Instruction::ZExt, val, ty, this->BB_); }
    // 创建符号扩展整数转换。
    ZextInst* create_sext(Value* val, Type* ty) { return new ZextInst(Instruction::SExt, val, ty, this->BB_); }
    // 创建整数截断转换。
    ZextInst* create_trunc(Value* val, Type* ty) { return new ZextInst(Instruction::Trunc, val, ty, this->BB_); }
    // 创建浮点到有符号整数转换。
    FpToSiInst* create_fptosi(Value* val, Type* ty) { return new FpToSiInst(Instruction::FPtoSI, val, ty, this->BB_); }
    // 创建有符号整数到浮点转换。
    SiToFpInst* create_sitofp(Value* val, Type* ty) { return new SiToFpInst(Instruction::SItoFP, val, ty, this->BB_); }
    // 创建保持比特不变的类型重解释。
    Bitcast* create_bitcast(Value* val, Type* ty) { return new Bitcast(Instruction::BitCast, val, ty, this->BB_); }
};
