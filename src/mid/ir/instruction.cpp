// 所有 IR 指令的 print()、静态查找表、辅助打印函数

#include "../../include/mid/ir/instruction.hpp"
#include "../../include/mid/ir/globalVariable.hpp"
#include "../../include/mid/ir/constant.hpp"

#include <cassert>
#include <map>
#include <string>

// OpID → IR 助记符
std::map<Instruction::OpID, std::string> instr_id2string_ = {
    {Instruction::Ret, "ret"},
    {Instruction::Br, "br"},
    {Instruction::FNeg, "fneg"},
    {Instruction::Add, "add"},
    {Instruction::Sub, "sub"},
    {Instruction::Mul, "mul"},
    {Instruction::SDiv, "sdiv"},
    {Instruction::SRem, "srem"},
    {Instruction::UDiv, "udiv"},
    {Instruction::URem, "urem"},
    {Instruction::FAdd, "fadd"},
    {Instruction::FSub, "fsub"},
    {Instruction::FMul, "fmul"},
    {Instruction::FDiv, "fdiv"},
    {Instruction::Shl, "shl"},
    {Instruction::LShr, "lshr"},
    {Instruction::AShr, "ashr"},
    {Instruction::And, "and"},
    {Instruction::Or, "or"},
    {Instruction::Xor, "xor"},
    {Instruction::Alloca, "alloca"},
    {Instruction::Load, "load"},
    {Instruction::Store, "store"},
    {Instruction::Select, "select"},
    {Instruction::GetElementPtr, "getelementptr"},
    {Instruction::ZExt, "zext"},
    {Instruction::FPtoSI, "fptosi"},
    {Instruction::SItoFP, "sitofp"},
    {Instruction::BitCast, "bitcast"},
    {Instruction::Clz, "clz"},
    {Instruction::InsertElement, "insertelement"},
    {Instruction::ICmp, "icmp"},
    {Instruction::FCmp, "fcmp"},
    {Instruction::PHI, "phi"},
    {Instruction::Call, "call"}
};

// ICmp 条件码 → ARM 条件后缀
const std::map<ICmpInst::ICmpOp, std::string> ICmpInst::ICmpOpName = {
    {ICmpInst::ICmpOp::ICMP_EQ, "eq"},
    {ICmpInst::ICmpOp::ICMP_NE, "ne"},
    {ICmpInst::ICmpOp::ICMP_UGT, "hi"},
    {ICmpInst::ICmpOp::ICMP_UGE, "cs"},
    {ICmpInst::ICmpOp::ICMP_ULT, "cc"},
    {ICmpInst::ICmpOp::ICMP_ULE, "ls"},
    {ICmpInst::ICmpOp::ICMP_SGT, "gt"},
    {ICmpInst::ICmpOp::ICMP_SGE, "ge"},
    {ICmpInst::ICmpOp::ICMP_SLT, "lt"},
    {ICmpInst::ICmpOp::ICMP_SLE, "le"}
};

// FCmp 条件码 → ARM 条件后缀
const std::map<FCmpInst::FCmpOp, std::string> FCmpInst::FCmpOpName = {
    {FCmpInst::FCmpOp::FCMP_FALSE, "nv"},
    {FCmpInst::FCmpOp::FCMP_OEQ, "eq"},
    {FCmpInst::FCmpOp::FCMP_OGT, "gt"},
    {FCmpInst::FCmpOp::FCMP_OGE, "ge"},
    {FCmpInst::FCmpOp::FCMP_OLT, "cc"},
    {FCmpInst::FCmpOp::FCMP_OLE, "ls"},
    {FCmpInst::FCmpOp::FCMP_ONE, "ne"},
    {FCmpInst::FCmpOp::FCMP_ORD, "vc"},
    {FCmpInst::FCmpOp::FCMP_UNO, "vs"},
    {FCmpInst::FCmpOp::FCMP_UEQ, "eq"},
    {FCmpInst::FCmpOp::FCMP_UGT, "hi"},
    {FCmpInst::FCmpOp::FCMP_UGE, "cs"},
    {FCmpInst::FCmpOp::FCMP_ULT, "lt"},
    {FCmpInst::FCmpOp::FCMP_ULE, "le"},
    {FCmpInst::FCmpOp::FCMP_UNE, "ne"},
    {FCmpInst::FCmpOp::FCMP_TRUE, "al"}
};

// 将 Value 按操作数格式打印：GlobalVar/Function → @name，Constant → 字面量，其他 → %name
std::string print_as_op(Value* v, bool print_ty) {
    std::string op_ir;
    if (print_ty) {
        op_ir += v->type_->print();
        op_ir += " ";
    }

    if (dynamic_cast<GlobalVariable*>(v)) {
        op_ir += "@" + v->name_;
    } else if (dynamic_cast<Function*>(v)) {
        op_ir += "@" + v->name_;
    } else if (dynamic_cast<Constant*>(v)) {
        op_ir += v->print();
    } else {
        op_ir += "%" + v->name_;
    }

    return op_ir;
}

// ICmpOp → IR 条件码字符串
std::string print_cmp_type(ICmpInst::ICmpOp op) {
    switch (op) {
        case ICmpInst::ICMP_SGE: return "sge";
        case ICmpInst::ICMP_SGT: return "sgt";
        case ICmpInst::ICMP_SLE: return "sle";
        case ICmpInst::ICMP_SLT: return "slt";
        case ICmpInst::ICMP_UGE: return "uge";
        case ICmpInst::ICMP_UGT: return "ugt";
        case ICmpInst::ICMP_ULE: return "ule";
        case ICmpInst::ICMP_ULT: return "ult";
        case ICmpInst::ICMP_EQ:  return "eq";
        case ICmpInst::ICMP_NE:  return "ne";
        default: break;
    }
    return "wrong cmpop";
}

// FCmpOp → IR 条件码字符串
std::string print_fcmp_type(FCmpInst::FCmpOp op) {
    switch (op) {
        case FCmpInst::FCMP_UGE: return "uge";
        case FCmpInst::FCMP_UGT: return "ugt";
        case FCmpInst::FCMP_ULE: return "ule";
        case FCmpInst::FCMP_ULT: return "ult";
        case FCmpInst::FCMP_UEQ: return "ueq";
        case FCmpInst::FCMP_UNE: return "une";
        default: break;
    }
    return "wrong fcmpop";
}

//============================ 各指令 print() ============================

// %v = add i32 %a, %b
std::string BinaryInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->operands_[0]->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += ", ";
    assert(this->get_operand(0)->type_->tid_ == this->get_operand(1)->type_->tid_);
    instr_ir += print_as_op(this->get_operand(1), false);
    return instr_ir;
}

// %v = zext i1 %a to i32 / fptosi / sitofp / fneg / bitcast
std::string UnaryInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->operands_[0]->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    switch (this->op_id_) {
        case Instruction::ZExt:
            assert(this->type_->tid_ == Type::IntegerTyID);
            instr_ir += " to i32";
            break;
        case Instruction::FPtoSI:
            assert(this->type_->tid_ == Type::IntegerTyID);
            instr_ir += " to i32";
            break;
        case Instruction::SItoFP:
            assert(this->type_->tid_ == Type::FloatTyID);
            instr_ir += " to float";
            break;
        case Instruction::FNeg:
            // fneg float %x  (no "to" suffix; result type == operand type)
            break;
        case Instruction::BitCast:
            assert(this->type_->tid_ == Type::IntegerTyID ||
                   this->type_->tid_ == Type::FloatTyID ||
                   this->type_->tid_ == Type::PointerTyID);
            instr_ir += " to ";
            instr_ir += this->type_->print();
            break;
        case Instruction::Clz:
            // clz i32 %x  (no "to" suffix; result type == operand type)
            break;
        default:
            assert(0 && "UnaryInst opID invalid!");
            break;
    }
    return instr_ir;
}

// %v = icmp eq i32 %a, %b
std::string ICmpInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += print_cmp_type(this->icmp_op_);
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += ", ";
    if (this->get_operand(0)->type_->tid_ == this->get_operand(1)->type_->tid_) {
        instr_ir += print_as_op(this->get_operand(1), false);
    } else {
        instr_ir += print_as_op(this->get_operand(1), true);
    }
    return instr_ir;
}

// %v = fcmp olt float %a, %b
std::string FCmpInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += print_fcmp_type(this->fcmp_op_);
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += ", ";
    if (this->get_operand(0)->type_->tid_ == this->get_operand(1)->type_->tid_) {
        instr_ir += print_as_op(this->get_operand(1), false);
    } else {
        instr_ir += print_as_op(this->get_operand(1), true);
    }
    return instr_ir;
}

// %v = select i1 %cond, %true_val, %false_val
std::string SelectInst::print() {
    std::string instr_ir = "%" + this->name_ + " = select i1 "
        + print_as_op(this->get_operand(0), false) + ", "
        + print_as_op(this->get_operand(1), true) + ", "
        + print_as_op(this->get_operand(2), true);
    return instr_ir;
}

// %v = call i32 @func(i32 %a, i32 %b)
std::string CallInst::print() {
    std::string instr_ir;
    if (!(this->type_->tid_ == Type::VoidTyID)) {
        instr_ir += "%";
        instr_ir += this->name_;
        instr_ir += " = ";
    }
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    unsigned int numops = this->num_ops_;
    instr_ir += static_cast<FunctionType*>(this->get_operand(numops - 1)->type_)->result_->print();

    instr_ir += " ";
    assert(dynamic_cast<Function*>(this->get_operand(numops - 1)) && "Wrong call operand function");

    instr_ir += print_as_op(this->get_operand(numops - 1), false);
    instr_ir += "(";
    for (unsigned int i = 0; i < numops - 1; i++) {
        if (i > 0) instr_ir += ", ";
        instr_ir += this->get_operand(i)->type_->print();
        instr_ir += " ";
        instr_ir += print_as_op(this->get_operand(i), false);
    }
    instr_ir += ")";
    return instr_ir;
}

// br label %target    或    br i1 %cond, label %true, label %false
std::string BranchInst::print() {
    std::string instr_ir;
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), true);

    if (this->num_ops_ == 3) {
        instr_ir += ", ";
        instr_ir += print_as_op(this->get_operand(1), true);
        instr_ir += ", ";
        instr_ir += print_as_op(this->get_operand(2), true);
    }
    return instr_ir;
}

// ret <ty> <val>  或   ret void
std::string ReturnInst::print() {
    std::string instr_ir;
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    if (this->num_ops_ != 0) {
        instr_ir += this->get_operand(0)->type_->print();
        instr_ir += " ";
        instr_ir += print_as_op(this->get_operand(0), false);
    } else {
        instr_ir += "void";
    }
    return instr_ir;
}

// %v = getelementptr [5 x [4 x i32]], [5 x [4 x i32]]* @a, i32 0, i32 2, i32 3
std::string GetElementPtrInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    assert(this->get_operand(0)->type_->tid_ == Type::PointerTyID);
    instr_ir += static_cast<PointerType*>(this->get_operand(0)->type_)->contained_->print();
    instr_ir += ", ";
    for (unsigned int i = 0; i < this->num_ops_; i++) {
        if (i > 0) instr_ir += ", ";
        instr_ir += this->get_operand(i)->type_->print();
        instr_ir += " ";
        instr_ir += print_as_op(this->get_operand(i), false);
    }
    return instr_ir;
}

// store <ty> <value>, <ty>* <ptr>
std::string StoreInst::print() {
    std::string instr_ir;
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += ", ";
    instr_ir += print_as_op(this->get_operand(1), true);
    return instr_ir;
}

// %v = load <ty>, <ty>* <ptr>
std::string LoadInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    assert(this->get_operand(0)->type_->tid_ == Type::PointerTyID);
    instr_ir += static_cast<PointerType*>(this->get_operand(0)->type_)->contained_->print();
    instr_ir += ",";
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), true);
    return instr_ir;
}

// %v = alloca i32
std::string AllocaInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += alloca_ty_->print();
    return instr_ir;
}

std::string ZextInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += " to ";
    instr_ir += this->dest_ty_->print();
    return instr_ir;
}

std::string FpToSiInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += " to ";
    instr_ir += this->dest_ty_->print();
    return instr_ir;
}

std::string SiToFpInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += " to ";
    instr_ir += this->dest_ty_->print();
    return instr_ir;
}

std::string Bitcast::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    instr_ir += print_as_op(this->get_operand(0), false);
    instr_ir += " to ";
    instr_ir += this->dest_ty_->print();
    return instr_ir;
}

std::string InsertElementInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->type_->print();
    instr_ir += " ";
    if (this->get_operand(0)->type_->tid_ == Type::VectorTyID)
        instr_ir += print_as_op(this->get_operand(0), false);
    else
        instr_ir += "undef";
    instr_ir += ", ";
    instr_ir += print_as_op(this->get_operand(1), true);
    instr_ir += ", ";
    instr_ir += print_as_op(this->get_operand(2), true);
    return instr_ir;
}

// %v = phi i32 [ val1, %bb1 ], [ val2, %bb2 ]
// 若有缺失的前驱 BB，补 "undef"
std::string PhiInst::print() {
    std::string instr_ir;
    instr_ir += "%";
    instr_ir += this->name_;
    instr_ir += " = ";
    instr_ir += instr_id2string_[this->op_id_];
    instr_ir += " ";
    instr_ir += this->get_operand(0)->type_->print();
    instr_ir += " ";
    for (int i = 0; i < this->num_ops_ / 2; i++) {
        if (i > 0) instr_ir += ", ";
        instr_ir += "[ ";
        instr_ir += print_as_op(this->get_operand(2 * i), false);
        instr_ir += ", ";
        instr_ir += print_as_op(this->get_operand(2 * i + 1), false);
        instr_ir += " ]";
    }
    if (this->num_ops_ / 2 < this->parent_->pre_bbs_.size()) {
        for (auto pre_bb : this->parent_->pre_bbs_) {
            if (std::find(this->operands_.begin(), this->operands_.end(), static_cast<Value*>(pre_bb)) == this->operands_.end()) {
                instr_ir += ", [ undef, " + print_as_op(pre_bb, false) + " ]";
            }
        }
    }
    return instr_ir;
}

PhiInst* PhiInst::create_phi(Type* ty, BasicBlock* bb) {
    std::vector<Value*> vals;
    std::vector<BasicBlock*> val_bbs;
    return new PhiInst(Instruction::PHI, vals, val_bbs, ty, bb);
}

void PhiInst::add_phi_pair_operand(Value* val, Value* pre_bb) {
    this->add_operand(val);
    this->add_operand(pre_bb);
}
