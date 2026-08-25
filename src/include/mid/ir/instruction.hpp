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
#include <optional>
#include <string>
#include <vector>

// 可执行 IR 节点的基类。operands_ 保存正向 def-use 引用，use_pos_ 保存每个引用在
// operand->use_list_ 中的位置；两者必须始终等长且一一对应。
class Instruction : public Value {
public:
    // 中端 opcode。枚举值只用于分类，具体类型和操作数布局由派生类定义。
    enum OpID {
        Ret = 11, Br,          // 终止指令
        FNeg,                  // 浮点取负
        Add, Sub, Mul, SDiv, SRem, UDiv, URem,  // 整数二元运算
        FAdd, FSub, FMul, FDiv,                 // 浮点二元运算
        Shl, LShr, AShr, And, Or, Xor,           // 位运算
        Alloca, Load, Store,                     // 内存操作
        Select,                                  // select i1 cond, T val1, F val2
        GetElementPtr,                           // 地址计算
        ZExt, SExt, Trunc, FPtoSI, SItoFP, BitCast, Clz, // 类型转换 + 内建
        InsertElement,                            // insertelement <4 x i32> %vec, i32 %val, i32 %idx
        ExtractElement,                           // extractelement <4 x i32> %vec, i32 %idx
        ShuffleVector,                            // shufflevector <4 x i32> %v1, <4 x i32> %v2, <4 x i32> <mask>
        ICmp, FCmp, PHI, Call                    // 比较、phi、调用
    };

    // 创建并立即追加到 parent；随后派生构造函数必须用 set_operand 填满操作数。
    Instruction(Type* ty, OpID id, unsigned num_ops, BasicBlock* parent)
        : Value(ty, ""), parent_(parent), op_id_(id) {
        operands_.resize(num_ops, nullptr);
        use_pos_.resize(num_ops);
        parent_->add_instruction(this);
    }
    // 仅创建、不插入；供需要精确插入位置的变换使用，之后必须由 BasicBlock 挂接。
    Instruction(Type* ty, OpID id, unsigned num_ops)
        : Value(ty, ""), parent_(nullptr), op_id_(id) {
        operands_.resize(num_ops, nullptr);
        use_pos_.resize(num_ops);
    }
    // 返回当前操作数个数；PHI 可在构造后增长或收缩。
    unsigned num_ops() const { return operands_.size(); }
    // 返回第 i 个操作数，不做边界检查。
    Value* get_operand(unsigned i) const { return operands_[i]; }
    virtual ~Instruction() = default;

    // 设置第 i 个操作数：先移除旧反向 use，再向 v 注册新 use。
    void set_operand(unsigned i, Value* v) {
        if (operands_[i] == v)
            return;
        if (operands_[i]) {
            operands_[i]->remove_use(use_pos_[i]);
        }
        operands_[i] = v;
        use_pos_[i] = v->add_use(this, i);
    }
    // 在尾部增加操作数并建立反向 use，主要供 PHI 追加 incoming pair。
    void add_operand(Value* v) {
        unsigned operand_index = num_ops();
        operands_.push_back(v);
        use_pos_.emplace_back(v->add_use(this, operand_index));
    }
    // 从所有 operand 的 use-list 删除本指令；只解除边，不清空 operands_。
    void remove_use_of_ops() {
        for (int i = 0; i < operands_.size(); i++) {
            operands_[i]->remove_use(use_pos_[i]);
        }
    }
    // 删除闭区间 [index1,index2] 的操作数，并修正后续 Use::operand_index_。
    void remove_operands(int index1, int index2) {
        for (int i = index1; i <= index2; i++) {
            operands_[i]->remove_use(use_pos_[i]);
        }
        for (int i = index2 + 1; i < operands_.size(); i++) {
            use_pos_[i]->operand_index_ -= index2 - index1 + 1;
        }
        operands_.erase(operands_.begin() + index1, operands_.begin() + index2 + 1);
        use_pos_.erase(use_pos_.begin() + index1, use_pos_.begin() + index2 + 1);
    }

    // 以下查询只检查结果类型或 opcode，不验证操作数类型与结构。
    bool is_void() const { return type_->tid_ == Type::VoidTyID; }
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
    bool is_sext() { return op_id_ == SExt; }
    bool is_trunc() { return op_id_ == Trunc; }
    bool is_fptosi() { return op_id_ == FPtoSI; }
    bool is_sitofp() { return op_id_ == SItoFP; }
    bool is_int_binary() { return is_add() || is_sub() || is_mul() || is_div() || is_rem(); }
    bool is_float_binary() { return is_fadd() || is_fsub() || is_fmul() || is_fdiv(); }
    bool is_binary() { return is_int_binary() || is_float_binary(); }
    bool isTerminator() { return is_br() || is_ret(); }

    // 输出一条完整 IR 指令；有结果的指令同时打印 %name =。
    virtual std::string print() = 0;
    BasicBlock* parent_;            // 所属块；未挂接指令为 nullptr
    OpID op_id_;                    // 指令类别
    std::vector<Value*> operands_;  // 有序操作数列表
    std::vector<std::list<Use>::iterator> use_pos_; // 每个操作数对应的反向 use 位置
    std::optional<std::list<Instruction*>::iterator> pos_in_bb_; // 已挂接时的链表位置
};

// 二元运算：add, sub, mul, sdiv, srem, fadd, fsub, fmul, fdiv, shl, ashr, and, or, xor
class BinaryInst : public Instruction {
public:
    // 构造二元指令并追加到 bb；结果类型由调用方显式给出。
    BinaryInst(Type* ty, OpID op, Value* v1, Value* v2, BasicBlock* bb) : Instruction(ty, op, 2, bb) {
        set_operand(0, v1);
        set_operand(1, v2);
    }
    // 仅创建不插入；末尾 bool 只是重载标签，不表达运行时选项。
    BinaryInst(Type* ty, OpID op, Value* v1, Value* v2, BasicBlock* bb, bool flag) : Instruction(ty, op, 2) {
        set_operand(0, v1);
        set_operand(1, v2);
        this->parent_ = bb;
    }
    // 输出 opcode、整数语义标记和两个同类型操作数。
    virtual std::string print() override;
};

// 一元运算：zext, fptosi, sitofp, fneg, bitcast
class UnaryInst : public Instruction {
public:
    // 构造单操作数指令并追加到 bb。
    UnaryInst(Type* ty, OpID op, Value* val, BasicBlock* bb) : Instruction(ty, op, 1, bb) { set_operand(0, val); }
    // 仅创建不插入；调用方选择具体挂接位置。
    UnaryInst(Type* ty, OpID op, Value* val, BasicBlock* bb, bool) : Instruction(ty, op, 1) {
        set_operand(0, val);
        this->parent_ = bb;
    }
    // 按 op_id_ 输出转换、fneg 或 clz 等单操作数形式。
    virtual std::string print() override;
};

// 整数比较：eq, ne, sgt, sge, slt, sle
class ICmpInst : public Instruction {
public:
    // 同时包含有符号和无符号谓词；结果为标量 i1 或逐 lane i32 向量掩码。
    enum ICmpOp {
        ICMP_EQ = 32, ICMP_NE, ICMP_UGT, ICMP_UGE, ICMP_ULT, ICMP_ULE,
        ICMP_SGT, ICMP_SGE, ICMP_SLT, ICMP_SLE
    };
    static const std::map<ICmpInst::ICmpOp, std::string> ICmpOpName; // 谓词文本表
    // 根据首操作数推导比较结果类型；bb 用于取得模块驻留类型。
    static Type *infer_result_type(Value *v1, BasicBlock *bb) {
        auto *vector = dynamic_cast<VectorType *>(v1->type_);
        if (vector)
            return bb->parent_->parent_->get_vector_type(
                bb->parent_->parent_->int32_ty_,
                vector->num_elements_);
        return bb->parent_->parent_->int1_ty_;
    }
    // 构造整数比较并追加到 bb。
    ICmpInst(ICmpOp op, Value* v1, Value* v2, BasicBlock* bb)
        : Instruction(infer_result_type(v1, bb), Instruction::ICmp, 2, bb), icmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
    }
    // 仅创建不插入，供变换放到指定支配位置。
    ICmpInst(ICmpOp op, Value* v1, Value* v2, BasicBlock* bb, bool)
        : Instruction(infer_result_type(v1, bb), Instruction::ICmp, 2), icmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
        this->parent_ = bb;
    }
    // 输出 icmp 谓词及两个整数操作数。
    virtual std::string print() override;
    ICmpOp icmp_op_; // 本指令采用的整数比较谓词
};

// 浮点比较：oeq, ogt, oge, olt, ole, one, ord, ueq, ugt, uge, ult, ule, une
class FCmpInst : public Instruction {
public:
    // ordered/unordered 浮点谓词，明确规定 NaN 时的结果。
    enum FCmpOp {
        FCMP_FALSE = 10, FCMP_OEQ, FCMP_OGT, FCMP_OGE, FCMP_OLT, FCMP_OLE, FCMP_ONE, FCMP_ORD,
        FCMP_UNO, FCMP_UEQ, FCMP_UGT, FCMP_UGE, FCMP_ULT, FCMP_ULE, FCMP_UNE, FCMP_TRUE
    };
    static const std::map<FCmpInst::FCmpOp, std::string> FCmpOpName; // 谓词文本表
    // 标量比较返回 i1，向量比较返回等 lane 数的 i32 掩码。
    static Type *infer_result_type(Value *v1, BasicBlock *bb) {
        auto *vector = dynamic_cast<VectorType *>(v1->type_);
        if (vector)
            return bb->parent_->parent_->get_vector_type(
                bb->parent_->parent_->int32_ty_, vector->num_elements_);
        return bb->parent_->parent_->int1_ty_;
    }
    // 构造浮点比较并追加到 bb。
    FCmpInst(FCmpOp op, Value* v1, Value* v2, BasicBlock* bb)
        : Instruction(infer_result_type(v1, bb), Instruction::FCmp, 2, bb), fcmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
    }
    // 仅创建不插入，供变换选择挂接位置。
    FCmpInst(FCmpOp op, Value* v1, Value* v2, BasicBlock* bb, bool)
        : Instruction(infer_result_type(v1, bb), Instruction::FCmp, 2), fcmp_op_(op) {
        set_operand(0, v1);
        set_operand(1, v2);
        this->parent_ = bb;
    }
    // 输出 fcmp 谓词及两个浮点操作数。
    virtual std::string print() override;
    FCmpOp fcmp_op_; // 本指令采用的浮点比较谓词
};

// select i1 %cond, T %true_val, F %false_val  →  backend: csel
class SelectInst : public Instruction {
public:
    // 选择结果类型取自 tv；cond、tv、fv 分别位于操作数 0、1、2。
    SelectInst(Value *cond, Value *tv, Value *fv, BasicBlock *bb)
        : Instruction(tv->type_, Instruction::Select, 3, bb) {
        set_operand(0, cond); set_operand(1, tv); set_operand(2, fv);
    }
    // 仅创建不插入；ty 由调用方给出，parent_ 保持为空直到挂接。
    SelectInst(Value *cond, Value *tv, Value *fv, Type *ty)
        : Instruction(ty, Instruction::Select, 3) {
        set_operand(0, cond); set_operand(1, tv); set_operand(2, fv);
    }
    // 输出 cond、true value 和 false value。
    virtual std::string print() override;
};

// 函数调用
class CallInst : public Instruction {
public:
    // 构造调用并追加到 bb；操作数布局为 [args..., callee]。
    CallInst(Function* func, const std::vector<Value*>& args, BasicBlock* bb)
        : Instruction(static_cast<FunctionType*>(func->type_)->result_, Instruction::Call, args.size() + 1, bb) {
        int num_ops = args.size() + 1;
        for (int i = 0; i < num_ops - 1; i++) {
            set_operand(i, args[i]);
        }
        set_operand(num_ops - 1, func);  // 最后一个操作数为被调用函数
    }
    // 仅创建不插入；no_insert 是重载标签。
    CallInst(Function* func, const std::vector<Value*>& args, BasicBlock* bb, bool no_insert)
        : Instruction(static_cast<FunctionType*>(func->type_)->result_, Instruction::Call, args.size() + 1) {
        int num_ops = args.size() + 1;
        for (int i = 0; i < num_ops - 1; i++) {
            set_operand(i, args[i]);
        }
        set_operand(num_ops - 1, func);
        parent_ = bb;
    }
    // 查询该调用是否已被证明处在尾位置。
    bool is_tail() const { return is_tail_; }
    // 设置或撤销尾调用提示；它不自行验证 ABI 和控制流条件。
    void set_tail(bool v = true) { is_tail_ = v; }
    // 输出 tail 提示、返回类型、callee 与实参列表。
    virtual std::string print() override;

private:
    // 尾调用提示：位于返回路径上，后端可在 ABI 允许时发 b 而非 bl
    bool is_tail_ = false;
};

// 分支跳转（条件/无条件）
class BranchInst : public Instruction {
public:
    // 条件分支：操作数为 [cond,true,false]；构造时同步两条 CFG 缓存边。
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
    // 无条件分支：唯一操作数为 target；构造时同步一条 CFG 缓存边。
    BranchInst(BasicBlock* if_true, BasicBlock* bb)
        : Instruction(if_true->parent_->parent_->void_ty_, Instruction::Br, 1, bb) {
        if_true->add_pre_basic_block(bb);
        bb->add_succ_basic_block(if_true);
        set_operand(0, if_true);
    }
    // 根据操作数个数输出条件或无条件 br。
    virtual std::string print() override;
};

// 返回指令：ret <ty> <val> 或 ret void
class ReturnInst : public Instruction {
public:
    // 构造带返回值的 ret 并追加到 bb。
    ReturnInst(Value* val, BasicBlock* bb) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Ret, 1, bb) { set_operand(0, val); }
    // 构造带返回值但不插入的 ret，供替换终止指令的变换使用。
    ReturnInst(Value* val, BasicBlock* bb, bool flag) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Ret, 1) {
        set_operand(0, val);
        this->parent_ = bb;
    }
    // 构造并追加 ret void。
    ReturnInst(BasicBlock* bb) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Ret, 0, bb) {}
    // 根据是否有值操作数输出 ret value 或 ret void。
    virtual std::string print() override;
};

// 地址计算：getelementptr [5 x [4 x i32]], [5 x [4 x i32]]* @a, i32 0, i32 2, i32 3
class GetElementPtrInst : public Instruction {
public:
    // 根据 ptr 和索引层数推导普通 GEP 的返回元素类型。
    // 保持现有语义：当 base 指向数组时，第一个索引通常只是“进入当前聚合对象”，
    // 真正消耗数组层数的是后续索引（例如 gep @a, 0, i）。
    // 返回最终所指元素类型，不含外层 PointerType；ptr 必须是指针。
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
    // 返回 suffix GEP 最终元素类型；与普通 GEP 的区别是从第 0 个索引开始降层。
    static Type* infer_split_suffix_GEP_return_type(Value* ptr, size_t idxs_size) {
        Type* ty = static_cast<PointerType*>(ptr->type_)->contained_;
        for (size_t i = 0; i < idxs_size; i++) {
            if (ty->tid_ != Type::ArrayTyID) break;
            ty = static_cast<ArrayType*>(ty)->contained_;
        }
        return ty;
    }

private:
    // 两种公开 GEP 入口共享的实现；result_elem_ty 已由各自规则推导。
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
    // 构造普通 GEP 并追加到 bb；操作数布局为 [base,index0,...]。
    GetElementPtrInst(Value* ptr, const std::vector<Value*>& idxs, BasicBlock* bb)
        : GetElementPtrInst(infer_GEP_return_type(ptr, idxs.size()), ptr, idxs, bb, false) {}

    // 普通 GEP 的仅创建版本；调用方应使用 BasicBlock 插入 API 安全挂接。
    GetElementPtrInst(Value* ptr, const std::vector<Value*>& idxs, BasicBlock* bb, bool no_insert)
        : GetElementPtrInst(infer_GEP_return_type(ptr, idxs.size()), ptr, idxs, bb, no_insert) {}

    // 构造拆分后缀 GEP；no_insert 控制是否立即挂接到 bb。
    static GetElementPtrInst* create_split_suffix_gep(Value* ptr,
                                                      const std::vector<Value*>& idxs,
                                                      BasicBlock* bb,
                                                      bool no_insert = false) {
        return new GetElementPtrInst(
            infer_split_suffix_GEP_return_type(ptr, idxs.size()),
            ptr, idxs, bb, no_insert);
    }

    // 输出源聚合类型、基址和全部索引。
    virtual std::string print() override;
};

// 存储：store <ty> <value>, <ty>* <ptr>
class StoreInst : public Instruction {
public:
    // 构造 store 并追加到 bb；断言 val 类型等于 ptr 所指类型。
    StoreInst(Value* val, Value* ptr, BasicBlock* bb) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Store, 2, bb) {
        assert(val->type_ == static_cast<PointerType*>(ptr->type_)->contained_);
        set_operand(0, val);
        set_operand(1, ptr);
    }
    // 构造 store 但不插入，保留同样的类型约束。
    StoreInst(Value* val, Value* ptr, BasicBlock* bb, bool) : Instruction(bb->parent_->parent_->void_ty_, Instruction::Store, 2) {
        assert(val->type_ == static_cast<PointerType*>(ptr->type_)->contained_);
        set_operand(0, val);
        set_operand(1, ptr);
        this->parent_ = bb;
    }
    // 输出 value 与目标 pointer。
    virtual std::string print() override;
};

// 加载：%val = load <ty>, <ty>* <ptr>
class LoadInst : public Instruction {
public:
    // 构造 load 并追加到 bb；结果类型由 ptr 的 contained_ 推导。
    LoadInst(Value* ptr, BasicBlock* bb) : Instruction(static_cast<PointerType*>(ptr->type_)->contained_, Instruction::Load, 1, bb) { set_operand(0, ptr); }
    // 构造 load 但不插入，供变换放置到精确位置。
    LoadInst(Value* ptr, BasicBlock* bb, bool) : Instruction(static_cast<PointerType*>(ptr->type_)->contained_, Instruction::Load, 1) {
        set_operand(0, ptr);
        this->parent_ = bb;
    }
    // 输出结果类型和来源 pointer。
    virtual std::string print() override;
};

// 栈上分配：%p = alloca i32
class AllocaInst : public Instruction {
public:
    // 记录 alloca 的结构用途；不能由名字推断这些语义。
    enum class Purpose {
        Generic,
        LoopExpansionScratch,
    };

    // 构造 ty 的栈槽并追加到 bb，指令结果类型是 ty*。
    AllocaInst(Type* ty, BasicBlock* bb)
        : Instruction(bb->parent_->parent_->get_pointer_type(ty),
                      Instruction::Alloca, 0, bb) {}
    // 构造栈槽但不插入；IRStmtBuilder 用它放到 entry 的 alloca 段。
    AllocaInst(Type* ty, BasicBlock* bb, bool)
        : Instruction(bb->parent_->parent_->get_pointer_type(ty),
                      Instruction::Alloca, 0) {
        this->parent_ = bb;
    }
    // 输出被分配对象的类型。
    virtual std::string print() override;
    // 返回栈槽中实际分配的对象类型。
    Type* allocated_type() const {
        return static_cast<PointerType*>(type_)->contained_;
    }
    Purpose purpose_ = Purpose::Generic;

    // 判断该槽是否为循环展开的临时存储。
    bool isLoopExpansionScratch() const {
        return purpose_ == Purpose::LoopExpansionScratch;
    }
    // 把槽标为循环展开临时存储，供后续分析区分普通源级对象。
    void markLoopExpansionScratch() {
        purpose_ = Purpose::LoopExpansionScratch;
    }
};

// i1 → i32 零扩展
class ZextInst : public Instruction {
public:
    // 构造 zext/sext/trunc 并追加到 bb；op 决定具体整数转换语义。
    ZextInst(OpID op, Value* val, Type* ty, BasicBlock* bb)
        : Instruction(ty, op, 1, bb) { set_operand(0, val); }
    // 构造整数转换但不插入。
    ZextInst(OpID op, Value* val, Type* ty, BasicBlock* bb, bool)
        : Instruction(ty, op, 1) {
        set_operand(0, val);
        this->parent_ = bb;
    }
    // 根据 op_id_ 输出 zext、sext 或 trunc。
    virtual std::string print() override;
};

// float → i32
class FpToSiInst : public Instruction {
public:
    // 构造浮点到有符号整数转换并追加到 bb。
    FpToSiInst(OpID op, Value* val, Type* ty, BasicBlock* bb)
        : Instruction(ty, op, 1, bb) { set_operand(0, val); }
    // 输出 fptosi 及目标整数类型。
    virtual std::string print() override;
};

// i32 → float
class SiToFpInst : public Instruction {
public:
    // 构造有符号整数到浮点转换并追加到 bb。
    SiToFpInst(OpID op, Value* val, Type* ty, BasicBlock* bb)
        : Instruction(ty, op, 1, bb) { set_operand(0, val); }
    // 输出 sitofp 及目标浮点类型。
    virtual std::string print() override;
};

// 位级重解释：bitcast [4 x [2 x i32]]* %2 to i32*
class Bitcast : public Instruction {
public:
    // 构造不改变比特的类型重解释并追加到 bb。
    Bitcast(OpID op, Value* val, Type* ty, BasicBlock* bb)
        : Instruction(ty, op, 1, bb) { set_operand(0, val); }
    // 构造 bitcast 但不插入。
    Bitcast(OpID op, Value* val, Type* ty, BasicBlock* bb, bool)
        : Instruction(ty, op, 1) {
        set_operand(0, val);
        parent_ = bb;
    }
    // 输出 bitcast 及目标类型。
    virtual std::string print() override;
};

// insertelement <4 x i32> %vec, i32 %val, i32 %idx
class InsertElementInst : public Instruction {
public:
    // 用 val 替换 vec 的 idx lane，产生同类型的新向量并追加到 bb。
    InsertElementInst(Value* vec, Value* val, Value* idx, BasicBlock* bb)
        : Instruction(vec->type_, Instruction::InsertElement, 3, bb) {
        set_operand(0, vec);
        set_operand(1, val);
        set_operand(2, idx);
    }
    // 构造 insertelement 但不插入。
    InsertElementInst(Value* vec, Value* val, Value* idx, BasicBlock* bb,
                      bool)
        : Instruction(vec->type_, Instruction::InsertElement, 3) {
        set_operand(0, vec);
        set_operand(1, val);
        set_operand(2, idx);
        parent_ = bb;
    }
    // 输出源向量、插入值和 lane 索引。
    virtual std::string print() override;
};

// 从定宽向量提取一个标量 lane。
class ExtractElementInst : public Instruction {
public:
    // 验证 vec 是 VectorType 并返回其 lane 类型。
    static Type *infer_element_type(Value *vec) {
        assert(vec->type_->tid_ == Type::VectorTyID);
        return static_cast<VectorType *>(vec->type_)->contained_;
    }

    // 提取 vec 的 idx lane 并追加到 bb。
    ExtractElementInst(Value *vec, Value *idx, BasicBlock *bb)
        : Instruction(infer_element_type(vec), Instruction::ExtractElement,
                      2, bb) {
        set_operand(0, vec);
        set_operand(1, idx);
    }
    // 构造 extractelement 但不插入。
    ExtractElementInst(Value *vec, Value *idx, BasicBlock *bb, bool)
        : Instruction(infer_element_type(vec), Instruction::ExtractElement, 2) {
        set_operand(0, vec);
        set_operand(1, idx);
        parent_ = bb;
    }
    // 输出源向量和 lane 索引。
    virtual std::string print() override;
};

// shufflevector <4 x i32> %v1, <4 x i32> %v2, <4 x i32> <mask0, mask1, ...>
// mask[i] 取值：0~N-1 选 v1 的 lane i，N~2N-1 选 v2 的 lane (i-N)
class ShuffleVectorInst : public Instruction {
public:
    // 构造并插入 shuffle；mask 同时保存为便于查询的整数数组和第 2 个常量操作数。
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
    // 构造但不插入；变换可把 shuffle 放到能支配被替换指令的位置。
    ShuffleVectorInst(Value *v1, Value *v2, std::vector<int> mask,
                      BasicBlock *bb, bool)
        : Instruction(v1->type_, Instruction::ShuffleVector, 3),
          mask_(std::move(mask)) {
        set_operand(0, v1);
        set_operand(1, v2);
        std::vector<Constant *> maskConsts;
        for (int m : mask_)
            maskConsts.push_back(new ConstantInt(
                bb->parent_->parent_->int32_ty_, m));
        auto *vecTy = static_cast<VectorType *>(v1->type_);
        auto *maskTy = bb->parent_->parent_->get_vector_type(
            bb->parent_->parent_->int32_ty_, vecTy->num_elements_);
        set_operand(2, new ConstantVector(maskTy, maskConsts));
        parent_ = bb;
    }
    // 返回 lane 选择表；引用只在本指令存活期间有效。
    const std::vector<int> &mask() const { return mask_; }
    // 输出两个源向量和常量 mask。
    virtual std::string print() override;
private:
    std::vector<int> mask_; // 便于变换查询的 mask 镜像
};

// phi 节点：%4 = phi i32 [ 1, %bb1 ], [ %6, %bb2 ]
class PhiInst : public Instruction {
public:
    // 构造未插入的 PHI；vals[i] 与 val_bbs[i] 组成一个 incoming pair。
    PhiInst(OpID op, const std::vector<Value*>& vals,
            const std::vector<BasicBlock*>& val_bbs, Type* ty, BasicBlock* bb)
        : Instruction(ty, op, 2 * vals.size()) {
        for (int i = 0; i < vals.size(); i++) {
            set_operand(2 * i, vals[i]);         // 偶数位：值
            set_operand(2 * i + 1, val_bbs[i]);  // 奇数位：来源 BB
        }
        this->parent_ = bb;
    }
    // 创建空 PHI 并插入 bb 的 PHI 区域，保证其位于普通指令之前。
    static PhiInst* create_phi(Type* ty, BasicBlock* bb);
    // 追加 [val,pre_bb] 操作数对并同步两条反向 use。
    void add_phi_pair_operand(Value* val, Value* pre_bb);
    // add_phi_pair_operand 的类型更明确别名。
    void addIncoming(Value* val, BasicBlock* bb) { this->add_phi_pair_operand(val, bb); }
    // 按 [value, predecessor] 对输出全部 incoming。
    virtual std::string print() override;
};
