#pragma once
// IR 基本块：线性指令序列，以终止指令（br/ret）结尾，维护 CFG 前驱/后继关系和支配树信息

#include "value.hpp"
#include "module.hpp"
#include "function.hpp"

#include <algorithm>
#include <list>
#include <set>
#include <string>
#include <vector>

class Instruction;

class BasicBlock : public Value {
public:
    explicit BasicBlock(Module* m, const std::string& name, Function* parent)
        : Value(m->label_ty_, name), parent_(parent) { parent_->add_basic_block(this); }

    // 在末尾/开头/终止指令前/指定指令前/指定指令后插入指令
    bool add_instruction(Instruction* instr);
    bool add_instruction_front(Instruction* instr);
    bool add_instruction_before_terminator(Instruction* instr);
    bool add_instruction_before_inst(Instruction* new_inst, Instruction* inst);
    bool add_instruction_after_inst(Instruction* new_inst, Instruction* inst);

    void add_pre_basic_block(BasicBlock* bb) {
        if (std::find(pre_bbs_.begin(), pre_bbs_.end(), bb) == pre_bbs_.end())
            pre_bbs_.push_back(bb);
    }
    void add_succ_basic_block(BasicBlock* bb) {
        if (std::find(succ_bbs_.begin(), succ_bbs_.end(), bb) == succ_bbs_.end())
            succ_bbs_.push_back(bb);
    }
    void remove_pre_basic_block(BasicBlock* bb) {
        pre_bbs_.erase(std::remove(pre_bbs_.begin(), pre_bbs_.end(), bb), pre_bbs_.end());
    }
    void remove_succ_basic_block(BasicBlock* bb) {
        succ_bbs_.erase(std::remove(succ_bbs_.begin(), succ_bbs_.end(), bb), succ_bbs_.end());
    }

    // 判断 this 是否支配 bb2（沿 idom 链向上查找，不依赖块名）
    int isDominate(BasicBlock* bb2) {
        if (!bb2 || this->parent_ != bb2->parent_)
            return -1;
        if (this == bb2)
            return 1;
        while (bb2->idom_) {
            if (bb2->idom_ == this)
                return 1;
            bb2 = bb2->idom_;
        }
        return 0;
    }

    Instruction* get_terminator();   // 获取终止指令（br 或 ret）
    bool delete_instr(Instruction* instr);  // 删除指令并清除 use 关系
    bool remove_instr(Instruction* instr);  // 移出指令但保留 use 关系（用于跨 BB 移动）
    virtual std::string print() override;

    std::list<Instruction*> instr_list_;  // 指令链表（支持高效插入删除）
    Function* parent_;
    // CFG 信息
    std::vector<BasicBlock*> pre_bbs_;
    std::vector<BasicBlock*> succ_bbs_;
    // 支配树信息
    std::set<BasicBlock*> dom_frontier_;
    std::set<BasicBlock*> rdom_frontier_;
    std::set<BasicBlock*> rdoms_;
    BasicBlock* idom_;
    // 活跃变量分析
    std::set<Value*> live_in;
    std::set<Value*> live_out;
};
