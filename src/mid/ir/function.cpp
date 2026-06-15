// Function / Argument 的 print() 及基本块管理方法

#include "../../include/mid/ir/function.hpp"
#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <functional>

Function::~Function() {}

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
    invalidateDominatorInfo();
}

// 获取函数的唯一 return 基本块
BasicBlock* Function::getRetBB() {
    for (auto bb : basic_blocks_) {
        if (bb->get_terminator()->is_ret()) {
            return bb;
        }
    }
    return nullptr;
}

// 统一为所有匿名 Value（参数/BB/指令）分配 IR 名称，便于输出。
// 对于已有名称的值，检查冲突并添加后缀以保证唯一性。
void Function::set_instr_name() {
    std::map<Value*, int> seq;
    std::set<std::string> used_names;

    auto uniquify = [&](std::string &name) {
        if (used_names.count(name)) {
            std::string base = name;
            int suffix = 1;
            do {
                name = base + "." + std::to_string(suffix++);
            } while (used_names.count(name));
        }
        used_names.insert(name);
    };

    for (auto arg : this->arguments_) {
        if (!seq.count(arg)) {
            auto seq_num = seq.size() + seq_cnt_;
            if (arg->name_ == "") {
                arg->name_ = "arg_" + std::to_string(seq_num);
                seq.insert({arg, seq_num});
            }
            uniquify(arg->name_);
        }
    }
    for (auto bb : basic_blocks_) {
        if (!seq.count(bb)) {
            auto seq_num = seq.size() + seq_cnt_;
            if (bb->name_.length() <= 6 || bb->name_.substr(0, 6) != "label_") {
                bb->name_ = "label_" + std::to_string(seq_num);
                seq.insert({bb, seq_num});
            }
            uniquify(bb->name_);
        }
        for (auto instr : bb->instr_list_) {
            if (instr->type_->tid_ != Type::VoidTyID && !seq.count(instr)) {
                auto seq_num = seq.size() + seq_cnt_;
                if (instr->name_ == "") {
                    if (instr->is_gep()) {
                        if (gep_cnt_ == 0)
                            instr->name_ = "arrayidx";
                        else
                            instr->name_ = "arrayidx" + std::to_string(gep_cnt_);
                        gep_cnt_++;
                    } else {
                        instr->name_ = "v" + std::to_string(seq_num);
                    }
                    seq.insert({instr, seq_num});
                }
                uniquify(instr->name_);
            }
        }
    }
    seq_cnt_ += seq.size();
}

// 沿 idom 链向上查找，判断 a 是否支配 b
bool DominatorInfo::dominates(BasicBlock *a, BasicBlock *b) const {
    while (b) {
        if (b == a) return true;
        auto it = idom.find(b);
        if (it == idom.end()) return false;
        b = it->second;
    }
    return false;
}

// 失效缓存（CFG 变更时调用）
void Function::invalidateDominatorInfo() {
    if (domInfo_) domInfo_->valid = false;
}

// 惰性获取支配树信息
DominatorInfo &Function::getDominatorInfo() {
    if (!domInfo_ || !domInfo_->valid)
        computeDominatorInfo();
    return *domInfo_;
}

// 计算支配树：RPO → idom（迭代数据流）→ domChildren → domFront
void Function::computeDominatorInfo() {
    DominatorInfo di;
    di.valid = true;

    BasicBlock *entry = basic_blocks_.front();
    if (!entry) return;

    // 1. RPO（DFS 后序遍历逆序）
    std::vector<BasicBlock *> postorder;
    std::set<BasicBlock *> visited;
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb) {
        visited.insert(bb);
        for (auto succ : bb->succ_bbs_)
            if (!visited.count(succ)) dfs(succ);
        postorder.push_back(bb);
    };
    dfs(entry);

    std::vector<BasicBlock *> rpo(postorder.rbegin(), postorder.rend());

    std::map<BasicBlock *, int> rpoIdx;
    for (int i = 0; i < (int)rpo.size(); i++)
        rpoIdx[rpo[i]] = i;

    // 2. 迭代 idom 计算（Cooper's algorithm）
    auto intersect = [&](BasicBlock *a, BasicBlock *b) -> BasicBlock * {
        while (a != b) {
            while (rpoIdx[a] > rpoIdx[b]) a = di.idom[a];
            while (rpoIdx[b] > rpoIdx[a]) b = di.idom[b];
        }
        return a;
    };

    for (auto bb : basic_blocks_)
        di.idom[bb] = nullptr;
    di.idom[entry] = entry;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto bb : rpo) {
            if (bb == entry) continue;
            BasicBlock *newIdom = nullptr;
            for (auto pred : bb->pre_bbs_) {
                if (!di.idom[pred]) continue;  // 尚未收敛的前驱跳过
                newIdom = newIdom ? intersect(newIdom, pred) : pred;
            }
            if (newIdom && di.idom[bb] != newIdom) {
                di.idom[bb] = newIdom;
                changed = true;
            }
        }
    }
    di.idom[entry] = nullptr;  // entry 没有立即支配者

    // 3. 支配树子节点
    for (auto bb : basic_blocks_) {
        auto it = di.idom.find(bb);
        if (it != di.idom.end() && it->second)
            di.domChildren[it->second].push_back(bb);
    }

    // 4. 支配边界
    for (auto &kv : di.idom) {
        BasicBlock *b = kv.first;
        if (b == entry) continue;  // entry 无支配边界
        for (auto pred : b->pre_bbs_) {
            BasicBlock *runner = pred;
            while (runner && runner != di.idom[b]) {
                di.domFront[runner].insert(b);
                runner = di.idom[runner];
            }
        }
    }

    domInfo_ = std::make_unique<DominatorInfo>(std::move(di));
}
