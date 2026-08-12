#pragma once
// IR 基本块：线性指令序列，以终止指令（br/ret）结尾，维护 CFG 前驱/后继关系

#include "value.hpp"
#include "module.hpp"
#include "function.hpp"

#include <algorithm>
#include <list>
#include <string>
#include <vector>

class Instruction;

// 函数内的基本块。指令顺序以 instr_list_ 为真值；分支目标是 CFG 的真值，
// pre_bbs_/succ_bbs_ 是必须同步维护的查询缓存。
class BasicBlock : public Value {
public:
    // 创建 label Value，并立即追加到 parent 的块列表。
    explicit BasicBlock(Module* m, const std::string& name, Function* parent)
        : Value(m->label_ty_, name), parent_(parent) { parent_->add_basic_block(this); }

    // 在末尾挂接未插入的 instr，同时设置 parent_ 和 pos_in_bb_；重复插入返回 false。
    bool add_instruction(Instruction* instr);
    // 在块首挂接 instr；变换方仍须自行保证 PHI 必须连续位于最前面。
    bool add_instruction_front(Instruction* instr);
    // 插到末条指令之前；要求块非空且调用方已保证末条确为终止指令。
    bool add_instruction_before_terminator(Instruction* instr);
    // 在已属于本块的 inst 前挂接 new_inst。
    bool add_instruction_before_inst(Instruction* new_inst, Instruction* inst);
    // 在已属于本块的 inst 后挂接 new_inst。
    bool add_instruction_after_inst(Instruction* new_inst, Instruction* inst);

    // 增加一条 CFG 前驱缓存边；已有边不会重复加入。
    void add_pre_basic_block(BasicBlock* bb) {
        if (std::find(pre_bbs_.begin(), pre_bbs_.end(), bb) == pre_bbs_.end())
            pre_bbs_.push_back(bb);
    }
    // 增加一条 CFG 后继缓存边；已有边不会重复加入。
    void add_succ_basic_block(BasicBlock* bb) {
        if (std::find(succ_bbs_.begin(), succ_bbs_.end(), bb) == succ_bbs_.end())
            succ_bbs_.push_back(bb);
    }
    // 删除所有等于 bb 的前驱缓存项。
    void remove_pre_basic_block(BasicBlock* bb) {
        pre_bbs_.erase(std::remove(pre_bbs_.begin(), pre_bbs_.end(), bb), pre_bbs_.end());
    }
    // 删除所有等于 bb 的后继缓存项。
    void remove_succ_basic_block(BasicBlock* bb) {
        succ_bbs_.erase(std::remove(succ_bbs_.begin(), succ_bbs_.end(), bb), succ_bbs_.end());
    }

    // 若末条指令是 br/ret 则返回它，否则返回 nullptr。
    Instruction* get_terminator();
    // 解除 instr 的操作数 use、移出链表并清空归属；仍被使用的结果须由调用方先替换。
    bool delete_instr(Instruction* instr);
    // 只从链表解挂并清空归属，保留操作数 use；专用于随后重新插入的跨块移动。
    bool remove_instr(Instruction* instr);
    // 输出块标签和全部指令；函数入口块不重复打印显式标签。
    virtual std::string print() override;

    std::list<Instruction*> instr_list_;  // 稳定迭代器的线性指令序列
    Function* parent_;                    // 所属函数，不拥有
    std::vector<BasicBlock*> pre_bbs_;    // CFG 前驱缓存，不含重复边
    std::vector<BasicBlock*> succ_bbs_;   // CFG 后继缓存，不含重复边
};
