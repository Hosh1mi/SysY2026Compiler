#pragma once
// IR 指令：所有 IR 指令的基类及具体子类（二元运算、一元运算、比较、分支、内存、转换等）

#include "value.hpp"
#include "basicBlock.hpp"
#include "function.hpp"
#include "module.hpp"
#include "constant.hpp"

#include <cassert>
#include <list>
#include <map>
#include <string>
#include <vector>

class Instruction : public Value {
public:
    enum OpID {
        Ret = 11, Br,          // 终止指令
        FNeg,                  // 浮点取负
        Add, Sub, Mul, SDiv, SRem, UDiv, URem,  // 整数二元运算
        FAdd, FSub, FMul, FDiv,                 // 浮点二元运算
        Shl, LShr, AShr, And, Or, Xor,           // 位运算
        Alloca, Load, Store,                     // 内存操作
        Select,                                  // select i1 cond, T val1, F val2
        GetElementPtr,                           // 地址计算
        ZExt, FPtoSI, SItoFP, BitCast, Clz,       // 类型转换 + 内建
        InsertElement,                            // insertelement <4 x i32> %vec, i32 %val, i32 %idx
        ExtractElement,                           // extractelement <4 x i32> %vec, i32 %idx
        ShuffleVector,                            // shufflevector <4 x i32> %v1, <4 x i32> %v2, <4 x i32> <mask>
        ICmp, FCmp, PHI, Call                    // 比较、phi、调用
    };

    // 创建并自动插入基本块
    Instruction(Type* ty, OpID id, unsigned num_ops, BasicBlock* parent) : Value(ty, ""), op_id_(id), num_ops_(num_ops), parent_(parent) {
        operands_.resize(num_ops_, nullptr);
        use_pos_.resize(num_ops_);
        parent_->add_instruction(this);
    }
    // 仅创建，不插入基本块
    Instruction(Type* ty, OpID id, unsigned num_ops) : Value(ty, ""), op_id_(id), num_ops_(num_ops), parent_(nullptr) {
        operands_.resize(num_ops_, nullptr);
        use_pos_.resize(num_ops_);
    }
    Value* get_operand(unsigned i) const { return operands_[i]; }
    virtual ~Instruction() = default;

    // 设置第 i 个操作数，自动维护 use-def 链
    void set_operand(unsigned i, Value* v) {
        if (operands_[i]) {
            operands_[i]->remove_use(use_pos_[i]);
        }
        operands_[i] = v;
        use_pos_[i] = v->add_use(this, i);
    }
    // 添加操作数（用于 phi 指令动态增加入边）
    void add_operand(Value* v) {
        operands_.push_back(v);
        use_pos_.emplace_back(v->add_use(this, num_ops_));
        num_ops_++;
    }
    // 清除所有操作数的 use 关系
    void remove_use_of_ops() {
        for (int i = 0; i < operands_.size(); i++) {
            operands_[i]->remove_use(use_pos_[i]);
        }
    }
    // 删除 phi 指令中指定范围的操作数对，并修正后续序号
    void remove_operands(int index1, int index2) {
        for (int i = index1; i <= index2; i++) {
            operands_[i]->remove_use(use_pos_[i]);
        }
        for (int i = index2 + 1; i < operands_.size(); i++) {
            use_pos_[i]->arg_no_ -= index2 - index1 + 1;
        }
        operands_.erase(operands_.begin() + index1, operands_.begin() + index2 + 1);
        use_pos_.erase(use_pos_.begin() + index1, use_pos_.begin() + index2 + 1);
        num_ops_ = operands_.size();
    }

    // 快速类型判断
    bool is_void() { return ((op_id_ == Ret) || (op_id_ == Br) || (op_id_ == Store) || (op_id_ == Call && this->type_->tid_ == Type::VoidTyID)); }
    bool is_phi() { return op_id_ == PHI; }
    bool is_store() { return op_id_ == Store; }
    bool is_alloca() { return op_id_ == Alloca; }
    bool is_ret() { return op_id_ == Ret; }
    bool is_load() { return op_id_ == Load; }
    bool is_br() { return op_id_ == Br; }
    bool is_add() { return op_id_ == Add; }
    bool is_sub() { return op_id_ == Sub; }
    bool is_mul() { return op_id_ == Mul; }
    bool is_div() { return op_id_ == SDiv; }
    bool is_rem() { return op_id_ == SRem; }
    bool is_fadd() { return op_id_ == FAdd; }
    bool is_fsub() { return op_id_ == FSub; }
    bool is_fmul() { return op_id_ == FMul; }
    bool is_fdiv() { return op_id_ == FDiv; }
    bool is_cmp() { return op_id_ == ICmp; }
    bool is_fcmp() { return op_id_ == FCmp; }
    bool is_call() { return op_id_ == Call; }
    bool is_gep() { return op_id_ == GetElementPtr; }
    bool is_zext() { return op_id_ == ZExt; }
    bool is_fptosi() { return op_id_ == FPtoSI; }
    bool is_sitofp() { return op_id_ == SItoFP; }
    bool is_int_binary() { return (is_add() || is_sub() || is_mul() || is_div() || is_rem()) && (num_ops_ == 2); }
    bool is_float_binary() { return (is_fadd() || is_fsub() || is_fmul() || is_fdiv()) && (num_ops_ == 2); }
    bool is_binary() { return is_int_binary() || is_float_binary(); }
    bool isTerminator() { return is_br() || is_ret(); }

    virtual std::string print() = 0;
    BasicBlock* parent_;
    OpID op_id_;
    unsigned num_ops_;
    std::vector<Value*> operands_;                         // 操作数列表
    std::vector<std::list<Use>::iterator> use_pos_;        // 对应操作数的 use_list 中与本指令相关的 use 迭代器
    std::vector<std::list<Instruction*>::iterator> pos_in_bb; // 在所属 BB 指令链表中的位置
};

// 二元运算：add, sub, mul, sdiv, srem, fadd, fsub, fmul, fdiv, shl, ashr, and, or, xor
class BinaryInst : public Instruction {
public:
    BinaryInst(Type* ty, OpID op, Value* v1, Value* v2, BasicBlock* bb) : Instruction(ty, op, 2, bb) {
        set_operand(0, v1);
        set_operand(1, v2);
    }
    // 仅创建不插入（bool 占位区分重载）
    BinaryInst(Type* ty, OpID op, Value* v1, Value* v2, BasicBlock* bb, bool flag) : Instruction(ty, op, 2) {
        set_operand(0, v1);
        set_operand(1, v2);
        this->parent_ = bb;
    }
    virtual std::string print() override;
};

// 一元运算：zext, fptosi, sitofp, fneg, bitcast
class UnaryInst : public Instruction {
public:
    UnaryInst(Type* ty, OpID op, Value* val, BasicBlock* bb) : Instruction(ty, op, 1, bb) { set_operand(0, val); }
    // no-insert variant (caller uses add_instruction_before_inst / add_instruction_after_inst)
    UnaryInst(Type* ty, OpID op, Value* val, BasicBlock* bb, bool) : Instruction(ty, op, 1) {
        set_operand(0, val);
        this->parent_ = bb;
    }
    virtual std::string print() override;
};

// 整数比较：eq, ne, sgt, sge, slt, sle
class ICmpInst : public Instruction {
public:
    enum ICmpOp {
        ICMP_EQ = 32, ICMP_NE, ICMP_UGT, ICMP_UGE, ICMP_ULT, ICMP_ULE,
        ICMP_SGT, ICMP_SGE, ICMP_SLT, ICMP_SLE
    };
    static const std::map<ICmpInst::ICmpOp, std::string> ICmpOpName;
    static Type *infer_result_type(Value *v1, BasicBlock *bb) {
        auto *vector = dynamic_cast<VectorType *>(v1->type_);
        if (vector)
            return bb->parent_->parent_->get_vector_type(
                bb->parent_->parent_->int32_ty_,
                vector->num_elements_);
        return bb->parent_->parent_->int1_ty_;
    }
    ICmpInst(ICmpOp op, Value* v1, Value* v2, BasicBlock* bb)
        : Instruction(infer_result_type(v1, bb), Instruction::ICmp, 2, bb), icmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
    }
    // no-insert constructor (caller uses add_instruction_before_inst)
    ICmpInst(ICmpOp op, Value* v1, Value* v2, BasicBlock* bb, bool)
        : Instruction(infer_result_type(v1, bb), Instruction::ICmp, 2), icmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
        this->parent_ = bb;
    }
    virtual std::string print() override;
    ICmpOp icmp_op_;
};

// 浮点比较：oeq, ogt, oge, olt, ole, one, ord, ueq, ugt, uge, ult, ule, une
class FCmpInst : public Instruction {
public:
    enum FCmpOp {
        FCMP_FALSE = 10, FCMP_OEQ, FCMP_OGT, FCMP_OGE, FCMP_OLT, FCMP_OLE, FCMP_ONE, FCMP_ORD,
        FCMP_UNO, FCMP_UEQ, FCMP_UGT, FCMP_UGE, FCMP_ULT, FCMP_ULE, FCMP_UNE, FCMP_TRUE
    };
    static const std::map<FCmpInst::FCmpOp, std::string> FCmpOpName;
    FCmpInst(FCmpOp op, Value* v1, Value* v2, BasicBlock* bb)
        : Instruction(bb->parent_->parent_->int1_ty_, Instruction::FCmp, 2, bb), fcmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
    }
    // no-insert constructor (caller uses add_instruction_before_inst)
    FCmpInst(FCmpOp op, Value* v1, Value* v2, BasicBlock* bb, bool)
        : Instruction(bb->parent_->parent_->int1_ty_, Instruction::FCmp, 2), fcmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
        this->parent_ = bb;
    }
    virtual std::string print() override;
    FCmpOp fcmp_op_;
};

// select i1 %cond, T %true_val, F %false_val  →  backend: csel
class SelectInst : public Instruction {
public:
    SelectInst(Value *cond, Value *tv, Value *fv, BasicBlock *bb)
        : Instruction(tv->type_, Instruction::Select, 3, bb) {
        set_operand(0, cond); set_operand(1, tv); set_operand(2, fv);
    }
    // no-insert constructor (caller adds to BB manually)
    SelectInst(Value *cond, Value *tv, Value *fv, Type *ty)
        : Instruction(ty, Instruction::Select, 3) {
        set_operand(0, cond); set_operand(1, tv); set_operand(2, fv);
    }
    virtual std::string print() override;
};

// 函数调用
class CallInst : public Instruction {
public:
    CallInst(Function* func, std::vector<Value*> args, BasicBlock* bb)
        : Instruction(static_cast<FunctionType*>(func->type_)->result_, Instruction::Call, args.size() + 1, bb) {
        int num_ops = args.size() + 1;
        for (int i = 0; i < num_ops - 1; i++) {
            set_operand(i, args[i]);
        }
        set_operand(num_ops - 1, func);  // 最后一个操作数为被调用函数
    }
    CallInst(Function* func, std::vector<Value*> args, BasicBlock* bb, bool no_insert)
        : Instruction(static_cast<FunctionType*>(func->type_)->result_, Instruction::Call, args.size() + 1) {
        int num_ops = args.size() + 1;
        for (int i = 0; i < num_ops - 1; i++) {
            set_operand(i, args[i]);
        }
        set_operand(num_ops - 1, func);
        parent_ = bb;
    }
    bool is_tail() const { return is_tail_; }
    void set_tail(bool v = true) { is_tail_ = v; }
    virtual std::string print() override;

private:
    // 尾调用提示：位于返回路径上，后端可在 ABI 允许时发 b 而非 bl
    bool is_tail_ = false;
};

// 分支跳转（条件/无条件）
class BranchInst : public Instruction {
public:
    // 条件分支：br i1 %cond, label %true, label %false
    BranchInst(Value* cond, BasicBlock* if_true, BasicBlock* if_false, BasicBlock* bb)
        : Instruction(if_true->parent_->parent_->void_ty_, Instruction::Br, 3, bb) {
        if_true->add_pre_basic_block(bb);
        if_false->add_pre_basic_block(bb);
        bb->add_succ_basic_block(if_false);
        bb->add_succ_basic_block(if_true);
        set_operand(0, cond);
        set_operand(1, if_true);
        set_operand(2, if_false);
    }
    // 无条件分支：br label %target
    BranchInst(BasicBlock* if_true, BasicBlock* bb)
        : Instruction(if_true->parent_->parent_->void_ty_, Instruction::Br, 1, bb) {
        if_true->add_pre_basic_block(bb);
        bb->add_succ_basic_block(if_true);
        set_operand(0, if_true);
    }
    virtual std::string print() override;
};

// 返回指令：ret <ty> <val> 或 ret void
class ReturnInst : public Instruction {
public:
    ReturnInst(Value* val, BasicBlock* bb) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Ret, 1, bb) { set_operand(0, val); }
    ReturnInst(Value* val, BasicBlock* bb, bool flag) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Ret, 1) {
        set_operand(0, val);
        this->parent_ = bb;
    }
    ReturnInst(BasicBlock* bb) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Ret, 0, bb) {}
    virtual std::string print() override;
};

// 地址计算：getelementptr [5 x [4 x i32]], [5 x [4 x i32]]* @a, i32 0, i32 2, i32 3
class GetElementPtrInst : public Instruction {
public:
    // 根据 ptr 和索引层数推导普通 GEP 的返回元素类型。
    // 保持现有语义：当 base 指向数组时，第一个索引通常只是“进入当前聚合对象”，
    // 真正消耗数组层数的是后续索引（例如 gep @a, 0, i）。
    static Type* infer_GEP_return_type(Value* ptr, size_t idxs_size) {
        Type* ty = static_cast<PointerType*>(ptr->type_)->contained_;
        if (ty->tid_ == Type::ArrayTyID) {
            ArrayType* arr_ty = static_cast<ArrayType*>(ty);
            for (size_t i = 1; i < idxs_size; i++) {
                ty = arr_ty->contained_;
                if (ty->tid_ == Type::ArrayTyID) {
                    arr_ty = static_cast<ArrayType*>(ty);
                }
            }
        }
        return ty;
    }

    // 专用于 split 后 suffix GEP 的返回元素类型推导。
    // 这时 base 已经是 prefix GEP 的结果指针，第一个索引就应当继续向下消耗一层。
    static Type* infer_split_suffix_GEP_return_type(Value* ptr, size_t idxs_size) {
        Type* ty = static_cast<PointerType*>(ptr->type_)->contained_;
        for (size_t i = 0; i < idxs_size; i++) {
            if (ty->tid_ != Type::ArrayTyID) break;
            ty = static_cast<ArrayType*>(ty)->contained_;
        }
        return ty;
    }

private:
    GetElementPtrInst(Type* result_elem_ty, Value* ptr,
                      const std::vector<Value*>& idxs,
                      BasicBlock* bb, bool no_insert)
        : Instruction(bb->parent_->parent_->get_pointer_type(result_elem_ty),
                      Instruction::GetElementPtr, idxs.size() + 1) {
        if (!no_insert) {
            bool ok = bb->add_instruction(this);
            assert(ok && "GetElementPtrInst inserted twice into BasicBlock");
        } else {
            this->parent_ = bb;
        }

        set_operand(0, ptr);
        for (size_t i = 0; i < idxs.size(); i++) {
            set_operand(i + 1, idxs[i]);
        }
    }

public:
    GetElementPtrInst(Value* ptr, std::vector<Value*> idxs, BasicBlock* bb)
        : GetElementPtrInst(infer_GEP_return_type(ptr, idxs.size()), ptr, idxs, bb, false) {}

    // 仅创建，不自动插入到 BB；调用方应使用 add_instruction_before_inst /
    // add_instruction_before_terminator / add_instruction 等 API 安全挂接。
    GetElementPtrInst(Value* ptr, std::vector<Value*> idxs, BasicBlock* bb, bool no_insert)
        : GetElementPtrInst(infer_GEP_return_type(ptr, idxs.size()), ptr, idxs, bb, no_insert) {}

    static GetElementPtrInst* create_split_suffix_gep(Value* ptr,
                                                      const std::vector<Value*>& idxs,
                                                      BasicBlock* bb,
                                                      bool no_insert = false) {
        return new GetElementPtrInst(
            infer_split_suffix_GEP_return_type(ptr, idxs.size()),
            ptr, idxs, bb, no_insert);
    }

    virtual std::string print() override;
};

// 存储：store <ty> <value>, <ty>* <ptr>
class StoreInst : public Instruction {
public:
    StoreInst(Value* val, Value* ptr, BasicBlock* bb) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Store, 2, bb) {
        assert(val->type_ == static_cast<PointerType*>(ptr->type_)->contained_);
        set_operand(0, val);
        set_operand(1, ptr);
    }
    StoreInst(Value* val, Value* ptr, BasicBlock* bb, bool) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Store, 2) {
        assert(val->type_ == static_cast<PointerType*>(ptr->type_)->contained_);
        set_operand(0, val);
        set_operand(1, ptr);
        this->parent_ = bb;
    }
    virtual std::string print() override;
};

// 加载：%val = load <ty>, <ty>* <ptr>
class LoadInst : public Instruction {
public:
    LoadInst(Value* ptr, BasicBlock* bb) : Instruction(static_cast<PointerType*>(ptr->type_)->contained_, Instruction::Load, 1, bb) { set_operand(0, ptr); }
    LoadInst(Value* ptr, BasicBlock* bb, bool) : Instruction(static_cast<PointerType*>(ptr->type_)->contained_, Instruction::Load, 1) {
        set_operand(0, ptr);
        this->parent_ = bb;
    }
    virtual std::string print() override;
};

// 栈上分配：%p = alloca i32
class AllocaInst : public Instruction {
public:
    enum class Purpose {
        Generic,
        LoopExpansionScratch,
    };

    AllocaInst(Type* ty, BasicBlock* bb) : Instruction(bb->parent_->parent_->get_pointer_type(ty), Instruction::Alloca, 0, bb), alloca_ty_(ty) {}
    AllocaInst(Type* ty, BasicBlock* bb, bool) : Instruction(bb->parent_->parent_->get_pointer_type(ty), Instruction::Alloca, 0), alloca_ty_(ty) { this->parent_ = bb; }
    virtual std::string print() override;
    Type* alloca_ty_;  // 分配的原始类型
    Purpose purpose_ = Purpose::Generic;

    bool isLoopExpansionScratch() const {
        return purpose_ == Purpose::LoopExpansionScratch;
    }
    void markLoopExpansionScratch() {
        purpose_ = Purpose::LoopExpansionScratch;
    }
};

// i1 → i32 零扩展
class ZextInst : public Instruction {
public:
    ZextInst(OpID op, Value* val, Type* ty, BasicBlock* bb) : Instruction(ty, op, 1, bb), dest_ty_(ty) { set_operand(0, val); }
    ZextInst(OpID op, Value* val, Type* ty, BasicBlock* bb, bool) : Instruction(ty, op, 1), dest_ty_(ty) {
        set_operand(0, val);
        this->parent_ = bb;
    }
    virtual std::string print() override;
    Type* dest_ty_;
};

// float → i32
class FpToSiInst : public Instruction {
public:
    FpToSiInst(OpID op, Value* val, Type* ty, BasicBlock* bb) : Instruction(ty, op, 1, bb), dest_ty_(ty) { set_operand(0, val); }
    virtual std::string print() override;
    Type* dest_ty_;
};

// i32 → float
class SiToFpInst : public Instruction {
public:
    SiToFpInst(OpID op, Value* val, Type* ty, BasicBlock* bb) : Instruction(ty, op, 1, bb), dest_ty_(ty) { set_operand(0, val); }
    virtual std::string print() override;
    Type* dest_ty_;
};

// 位级重解释：bitcast [4 x [2 x i32]]* %2 to i32*
class Bitcast : public Instruction {
public:
    Bitcast(OpID op, Value* val, Type* ty, BasicBlock* bb) : Instruction(ty, op, 1, bb), dest_ty_(ty) { set_operand(0, val); }
    Bitcast(OpID op, Value* val, Type* ty, BasicBlock* bb, bool)
        : Instruction(ty, op, 1), dest_ty_(ty) {
        set_operand(0, val);
        parent_ = bb;
    }
    virtual std::string print() override;
    Type* dest_ty_;
};

// insertelement <4 x i32> %vec, i32 %val, i32 %idx
class InsertElementInst : public Instruction {
public:
    InsertElementInst(Value* vec, Value* val, Value* idx, BasicBlock* bb)
        : Instruction(vec->type_, Instruction::InsertElement, 3, bb) {
        set_operand(0, vec);
        set_operand(1, val);
        set_operand(2, idx);
    }
    InsertElementInst(Value* vec, Value* val, Value* idx, BasicBlock* bb,
                      bool)
        : Instruction(vec->type_, Instruction::InsertElement, 3) {
        set_operand(0, vec);
        set_operand(1, val);
        set_operand(2, idx);
        parent_ = bb;
    }
    virtual std::string print() override;
};

// 从定宽向量提取一个标量 lane。
class ExtractElementInst : public Instruction {
public:
    static Type *infer_element_type(Value *vec) {
        assert(vec->type_->tid_ == Type::VectorTyID);
        return static_cast<VectorType *>(vec->type_)->contained_;
    }

    ExtractElementInst(Value *vec, Value *idx, BasicBlock *bb)
        : Instruction(infer_element_type(vec), Instruction::ExtractElement,
                      2, bb) {
        set_operand(0, vec);
        set_operand(1, idx);
    }
    ExtractElementInst(Value *vec, Value *idx, BasicBlock *bb, bool)
        : Instruction(infer_element_type(vec), Instruction::ExtractElement, 2) {
        set_operand(0, vec);
        set_operand(1, idx);
        parent_ = bb;
    }
    virtual std::string print() override;
};

// shufflevector <4 x i32> %v1, <4 x i32> %v2, <4 x i32> <mask0, mask1, ...>
// mask[i] 取值：0~N-1 选 v1 的 lane i，N~2N-1 选 v2 的 lane (i-N)
class ShuffleVectorInst : public Instruction {
public:
    ShuffleVectorInst(Value *v1, Value *v2, std::vector<int> mask, BasicBlock *bb)
        : Instruction(v1->type_, Instruction::ShuffleVector, 3, bb), mask_(std::move(mask)) {
        set_operand(0, v1);
        set_operand(1, v2);
        // mask as ConstantVector operand
        std::vector<Constant*> maskConsts;
        for (int m : mask_)
            maskConsts.push_back(new ConstantInt(
                bb->parent_->parent_->int32_ty_, m));
        auto *vecTy = static_cast<VectorType*>(v1->type_);
        auto *maskTy = bb->parent_->parent_->get_vector_type(
            bb->parent_->parent_->int32_ty_, vecTy->num_elements_);
        set_operand(2, new ConstantVector(maskTy, maskConsts));
    }
    const std::vector<int> &mask() const { return mask_; }
    virtual std::string print() override;
private:
    std::vector<int> mask_;
};

// phi 节点：%4 = phi i32 [ 1, %bb1 ], [ %6, %bb2 ]
class PhiInst : public Instruction {
public:
    PhiInst(OpID op, std::vector<Value*> vals, std::vector<BasicBlock*> val_bbs, Type* ty, BasicBlock* bb)
        : Instruction(ty, op, 2 * vals.size()) {
        for (int i = 0; i < vals.size(); i++) {
            set_operand(2 * i, vals[i]);         // 偶数位：值
            set_operand(2 * i + 1, val_bbs[i]);  // 奇数位：来源 BB
        }
        this->parent_ = bb;
    }
    static PhiInst* create_phi(Type* ty, BasicBlock* bb);
    void add_phi_pair_operand(Value* val, Value* pre_bb);
    void addIncoming(Value* val, BasicBlock* bb) { this->add_phi_pair_operand(val, bb); }
    virtual std::string print() override;
    Value* l_val_;  // mem2reg 用：记录被替换的 alloca
};
