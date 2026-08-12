// Function / Argument 的 print() 及基本块管理方法

#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <set>
#include <string>

void Function::add_basic_block(BasicBlock *bb) {
    basic_blocks_.push_back(bb);
    basic_block_names_.insert(bb->name_);
}

std::string Function::uniqueBasicBlockName(const std::string &base) {
    unsigned &suffix = basic_block_suffixes_[base];
    for (;;) {
        std::string candidate = suffix == 0
                                    ? base
                                    : base + std::to_string(suffix);
        ++suffix;
        if (basic_block_names_.insert(candidate).second)
            return candidate;
    }
}

// define/declare <ret_ty> @<name>(<args>) { <body> }
std::string Function::print() {
    set_instr_name();  // 先统一命名，保证输出一致
    std::string func_ir;

    // 语义属性以前置注释行输出（保持 define/参数表本身合法、可见即可 round-trip）
    {
        std::string sem;
        if (this->hasSemFlag(SemFlag::FnPure)) sem += " pure";
        else if (this->hasSemFlag(SemFlag::FnReadOnly)) sem += " readonly";
        for (auto *arg : this->arguments_) {
            std::string a;
            if (arg->hasSemFlag(SemFlag::ArgReadOnly)) a += " readonly";
            if (arg->hasSemFlag(SemFlag::ArgNoCapture)) a += " nocapture";
            if (!a.empty()) sem += "; %" + arg->name_ + ":" + a;
        }
        if (!sem.empty()) func_ir += "; sem:" + sem + "\n";
    }

    if (this->is_declaration())
        func_ir += "declare ";
    else
        func_ir += "define ";

    func_ir += this->get_return_type()->print();
    func_ir += " ";
    func_ir += print_as_op(this, false);
    func_ir += "(";

    if (this->is_declaration()) {
        for (size_t i = 0; i < this->arguments_.size(); i++) {
            if (i) func_ir += ", ";
            func_ir += static_cast<FunctionType*>(this->type_)->args_[i]->print();
        }
        if (static_cast<FunctionType*>(this->type_)->is_variadic_) {
            if (!this->arguments_.empty()) func_ir += ", ";
            func_ir += "...";
        }
    } else {
        for (auto arg = this->arguments_.begin(); arg != arguments_.end(); arg++) {
            if (arg != this->arguments_.begin()) {
                func_ir += ", ";
            }
            func_ir += static_cast<Argument*>(*arg)->print();
        }
    }
    func_ir += ")";

    if (!this->is_declaration()) {
        func_ir += " {";
        func_ir += "\n";
        for (auto bb : this->basic_blocks_) {
            func_ir += bb->print();
        }
        func_ir += "}";
    }

    return func_ir;
}

// <ty> %<name>
std::string Argument::print() {
    std::string arg_ir;
    arg_ir += this->type_->print();
    arg_ir += " %";
    arg_ir += this->name_;
    return arg_ir;
}

// 从函数中移除基本块，并清理 CFG 引用
void Function::remove_bb(BasicBlock* bb) {
    basic_blocks_.erase(std::remove(basic_blocks_.begin(), basic_blocks_.end(), bb), basic_blocks_.end());
    for (auto pre : bb->pre_bbs_) {
        pre->remove_succ_basic_block(bb);
    }
    for (auto succ : bb->succ_bbs_) {
        succ->remove_pre_basic_block(bb);
    }
}

// 获取函数的唯一 return 基本块
BasicBlock* Function::getRetBB() {
    for (auto bb : basic_blocks_) {
        auto *terminator = bb->get_terminator();
        if (terminator && terminator->is_ret()) {
            return bb;
        }
    }
    return nullptr;
}

// 统一为所有匿名 Value（参数/指令）分配 IR 名称。
// 已命名的值（alloca 源名、%retval、%cmp 等）做冲突消解；其余按出现顺序编号为 %0,%1,...
void Function::set_instr_name() {
    std::set<std::string> used_names;
    unsigned next_num = 0;

    auto uniquify = [&](std::string &name) {
        if (!used_names.count(name)) {
            used_names.insert(name);
            return;
        }
        std::string base = name;
        int suffix = 1;
        do {
            name = base + std::to_string(suffix++);
        } while (used_names.count(name));
        used_names.insert(name);
    };

    auto assign_numeric = [&](Value *v) {
        std::string name;
        do {
            name = std::to_string(next_num++);
        } while (used_names.count(name));
        v->name_ = name;
        used_names.insert(name);
    };

    // 与常见前端一致的助记名；冲突时由 uniquify 加后缀（cmp → cmp1）
    auto mnemonic = [](Instruction *instr) -> std::string {
        if (instr->is_gep()) return "arrayidx";
        if (instr->is_cmp() || instr->is_fcmp()) return "cmp";
        if (instr->is_add() || instr->is_fadd()) return "add";
        if (instr->is_sub() || instr->is_fsub()) return "sub";
        if (instr->is_mul() || instr->is_fmul()) return "mul";
        if (instr->is_div() || instr->is_fdiv()) return "div";
        if (instr->is_rem()) return "rem";
        if (instr->is_zext()) return "zext";
        if (instr->is_sitofp()) return "sitofp";
        if (instr->is_fptosi()) return "fptosi";
        if (instr->is_phi()) return "phi";
        return "";
    };

    for (auto *arg : this->arguments_) {
        if (arg->name_.empty())
            assign_numeric(arg);
        else
            uniquify(arg->name_);
    }

    for (auto *bb : basic_blocks_) {
        // 保留前端/优化给的语义名（entry、if.then 等）；空名再编号
        if (bb->name_.empty())
            assign_numeric(bb);
        else
            uniquify(bb->name_);

        for (auto *instr : bb->instr_list_) {
            if (instr->type_->tid_ == Type::VoidTyID)
                continue;
            if (!instr->name_.empty()) {
                uniquify(instr->name_);
                continue;
            }
            std::string hint = mnemonic(instr);
            if (!hint.empty()) {
                instr->name_ = hint;
                uniquify(instr->name_);
            } else {
                assign_numeric(instr);
            }
        }
    }
}
