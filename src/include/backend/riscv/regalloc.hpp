#pragma once
// RISC-V 图着色寄存器分配器（Chaitin-Briggs 乐观着色）。
//
// 算法移植自 arm64 后端（Arm64RegAlloc），核心（活跃区间、干涉图、phi 合并、
// 简化/选择、溢出代价）与目标无关；差异仅在寄存器集合与命名。
//
// 本分配器只分配 callee-saved 物理寄存器（整型 s1-s11，浮点 fs0-fs11）：
//   - 调用边界（传参 a0-a7/fa0-fa7、返回 a0/fa0）与被分配寄存器(s/fs)不相交，
//     消除了入参/出参的并行拷贝撞号风险；
//   - 任何被分配的值天然跨调用安全，无需 caller-saved 保存逻辑。
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
                   const std::vector<std::string> &colorToReg, bool isFloat,
                   const std::map<Value *, double> &spillCost,
                   const std::map<Value *, std::set<Value *>> &phiAffinity,
                   const std::function<bool(Value *, Value *)> &trulyInterferes);

    Function *func_;
    std::map<Value *, std::string> assignedRegs_;
};
