#pragma once
#include "../../mid/ir/ir.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

/// Chaitin-Briggs optimistic graph-coloring register allocator.
/// Standalone class that takes a Function* and produces a mapping
/// of SSA Value* → physical register name (e.g. "w19", "s8", "x20").
class Arm64RegAlloc {
public:
    explicit Arm64RegAlloc(Function *f);

    /// Run the allocation algorithm.
    void allocate();

    /// Result: Value* → register name.
    const std::map<Value*, std::string> &assignedRegs() const;

    /// 循环内多指令常量提升结果：32 位常量值 → 持有它的物理寄存器。
    /// 叶函数可使用 callee-saved；非叶函数使用 caller-saved 并在 call 后重物化。
    const std::map<int, std::string> &promotedConsts() const;

    /// Convenience accessors on the result set.
    bool hasAssignedReg(Value *v) const;
    std::string assignedReg(Value *v, bool asAddress = false) const;

private:
    bool canAssignRegister(Value *v) const;

    /// 计算逆后序(RPO)块序,并填充每个块的前驱表。
    std::vector<BasicBlock*> computeBlockOrder(
        std::map<BasicBlock*, std::vector<BasicBlock*>> &preds) const;

    /// 基于支配关系计算每个块的循环嵌套深度。
    std::map<BasicBlock*, int> computeLoopDepth(
        const std::vector<BasicBlock*> &blocksOrder,
        std::map<BasicBlock*, std::vector<BasicBlock*>> &preds) const;

    struct Interval {
        Value *value;
        int start;
        int end;
        bool isFloat;
        bool isPtr;
        bool isNEON;
        bool crossesCall;
    };

    void colorPool(const std::vector<Interval> &pool,
                   const std::vector<int> &colorToReg, bool isFloat,
                   const std::set<int> &callerSavedRegs,
                   const std::map<Value*, double> &spillCost,
                   const std::map<Value*, std::set<Value*>> &phiAffinity,
                   const std::function<bool(Value*, Value*)> &trulyInterferes);

    Function *func_;
    std::map<Value*, std::string> assignedRegs_;
    std::map<int, std::string> promotedConsts_;
};
