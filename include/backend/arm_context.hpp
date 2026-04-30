#pragma once

#include "../mid/ir/ir.hpp"
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// 后端在函数级别保存的上下文，负责记录栈槽、PHI 边搬运以及当前块信息。
struct ArmFuncContext {
    struct StackSlot {
        int offset = 0;
        size_t size = 0;
        bool is_spill = false;
    };

    struct PhiMove {
        Value *dst = nullptr;
        Value *src = nullptr;
    };

    Function *func = nullptr;
    std::ostringstream text;
    std::ostringstream prologue;
    std::map<Value *, StackSlot> slots;
    std::map<BasicBlock *, std::string> bb_labels;
    std::map<BasicBlock *, std::vector<PhiMove>> edge_moves;
    std::map<BasicBlock *, std::vector<BasicBlock *>> preds;
    int frame_size = 0;
    int max_call_args = 0;
    BasicBlock *current_bb = nullptr;
};
