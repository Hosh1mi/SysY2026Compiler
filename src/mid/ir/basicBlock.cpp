// BasicBlock 的方法：print、指令增删、终止指令获取、CFG 维护

#include "../../include/mid/ir/basicBlock.hpp"
#include "../../include/mid/ir/instruction.hpp"

#include <string>

// label_<name>:                          ; preds = %pre1, %pre2
//   <instr1>
//   <instr2>
std::string BasicBlock::print() {
    std::string bb_ir;
    bb_ir += this->name_;
    bb_ir += ":";
    if (this->hasSemFlag(SemFlag::MemsetIdiomLoop))
        bb_ir += "                                                ; memset_loop";
    if (!this->pre_bbs_.empty()) {
        bb_ir += "                                                ; preds = ";
    }
    for (auto bb : this->pre_bbs_) {
        if (bb != *this->pre_bbs_.begin())
            bb_ir += ", ";
        bb_ir += print_as_op(bb, false);
    }

    if (!this->parent_) {
        bb_ir += "\n";
        bb_ir += "; Error: Block without parent!";
    }
    bb_ir += "\n";
    for (auto instr : this->instr_list_) {
        bb_ir += "  ";
        bb_ir += instr->print();
        bb_ir += "\n";
    }

    return bb_ir;
}

// 获取 BB 的最后一条指令（仅当为 br 或 ret 时视为终止指令）
Instruction* BasicBlock::get_terminator() {
    if (instr_list_.empty())
        return nullptr;
    switch (instr_list_.back()->op_id_) {
        case Instruction::Ret:
        case Instruction::Br:
            return instr_list_.back();
        default:
            return nullptr;
    }
}

// 删除指令，清除所有 use 关系，释放指令为"自由身"
bool BasicBlock::delete_instr(Instruction* instr) {
    if ((!instr) || instr->pos_in_bb.size() != 1 || instr->parent_ != this)
        return false;
    this->instr_list_.erase(instr->pos_in_bb.back());
    instr->remove_use_of_ops();
    instr->pos_in_bb.clear();
    instr->parent_ = nullptr;
    return true;
}

// 尾部追加指令
bool BasicBlock::add_instruction(Instruction* instr) {
    if (instr->pos_in_bb.size() != 0) {  // 已插入某 BB，拒绝重复
        return false;
    } else {
        instr_list_.push_back(instr);
        std::list<Instruction*>::iterator tail = instr_list_.end();
        instr->pos_in_bb.emplace_back(--tail);
        instr->parent_ = this;
        return true;
    }
}

// 头部插入指令
bool BasicBlock::add_instruction_front(Instruction* instr) {
    if (instr->pos_in_bb.size() != 0) {
        return false;
    } else {
        instr_list_.push_front(instr);
        std::list<Instruction*>::iterator head = instr_list_.begin();
        instr->pos_in_bb.emplace_back(head);
        instr->parent_ = this;
        return true;
    }
}

// 插入到终止指令之前（即倒数第二位）
bool BasicBlock::add_instruction_before_terminator(Instruction* instr) {
    if (instr->pos_in_bb.size() != 0) {
        return false;
    } else if (instr_list_.empty()) {  // 无指令则无法确定插入位
        return false;
    } else {
        auto it = std::end(instr_list_);
        instr_list_.emplace(--it, instr);
        instr->pos_in_bb.emplace_back(--it);
        instr->parent_ = this;
        return true;
    }
}

// 在指定指令前插入新指令
bool BasicBlock::add_instruction_before_inst(Instruction* new_instr, Instruction* instr) {
    if ((!instr) || instr->pos_in_bb.size() != 1 || instr->parent_ != this)
        return false;
    if (new_instr->pos_in_bb.size() != 0)
        return false;
    else if (instr_list_.empty())
        return false;
    else {
        auto it = instr->pos_in_bb.back();
        instr_list_.emplace(it, new_instr);
        new_instr->pos_in_bb.emplace_back(--it);
        new_instr->parent_ = this;
        return true;
    }
}

// 从 BB 中移出指令（保留 use 关系，用于跨 BB 移动）
bool BasicBlock::remove_instr(Instruction* instr) {
    if ((!instr) || instr->pos_in_bb.size() != 1 || instr->parent_ != this)
        return false;
    this->instr_list_.erase(instr->pos_in_bb.back());
    instr->pos_in_bb.clear();
    instr->parent_ = nullptr;
    return true;
}
