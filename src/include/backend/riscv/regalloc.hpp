#pragma once
// RISC-V 图着色寄存器分配器（Chaitin-Briggs 乐观着色）。
//
// 算法移植自 arm64 后端（Arm64RegAlloc），核心（活跃区间、干涉图、phi 合并、
// 简化/选择、溢出代价）与目标无关；差异仅在寄存器集合与命名。
//
// 跨调用值使用 callee-saved；不跨调用且不参与 ABI 并行搬运的内部值可以使用
// caller-saved。指令选择固定使用的 t0-t6/ft0-ft2 永不进入色板。
// 未着色（溢出）的值由后端退回栈槽（朴素路径即溢出实现）。
// 被用到的 callee-saved 由前奏/收场负责保存/恢复。

#include "../../mid/ir/ir.hpp"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class RiscvRegAlloc {
public:
    explicit RiscvRegAlloc(Function *f) : func_(f) {}
    void allocate();

    const std::map<Value *, std::string> &assignedRegs() const { return assignedRegs_; }
    bool hasAssignedReg(Value *v) const { return assignedRegs_.count(v) > 0; }
    std::string assignedReg(Value *v) const {
        auto it = assignedRegs_.find(v);
        return it == assignedRegs_.end() ? std::string() : it->second;
    }

private:
    bool canAssignRegister(Value *v) const;

    struct Interval {
        Value *value;
        int start;
        int end;
        bool isFloat;
        bool crossesCall;
    };

    void colorPool(const std::vector<Interval> &pool,
                   const std::vector<std::string> &colorToReg,
                   int callerSavedColors,
                   const std::set<Value *> &requiresCalleeSaved,
                   const std::map<Value *, double> &spillCost,
                   const std::map<Value *, std::set<Value *>> &phiAffinity,
                   const std::function<bool(Value *, Value *)> &trulyInterferes);

    Function *func_;
    std::map<Value *, std::string> assignedRegs_;
};
