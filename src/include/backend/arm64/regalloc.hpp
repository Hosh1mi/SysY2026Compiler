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

    /// 指令编号：为每个可分配值填充定义点/最后使用点，并记录每个块的编号范围。
    void computeInstructionNumbers(
        const std::vector<BasicBlock*> &blocksOrder,
        std::map<Value*, int> &defPos,
        std::map<Value*, int> &lastUse,
        std::map<BasicBlock*, int> &blockStart,
        std::map<BasicBlock*, int> &blockEnd) const;

    /// 数据流分析：计算每个块的 LiveIn/LiveOut，并标记跨越 call 的值。
    void computeLiveness(
        const std::vector<BasicBlock*> &blocksOrder,
        std::map<BasicBlock*, std::set<Value*>> &liveIn,
        std::map<BasicBlock*, std::set<Value*>> &liveOut,
        std::set<Value*> &crossesCallValues) const;

    /// 由 defPos/lastUse/liveOut 构建所有值的活跃区间。
    std::vector<Interval> buildIntervals(
        const std::vector<BasicBlock*> &blocksOrder,
        const std::map<Value*, int> &defPos,
        const std::map<Value*, int> &lastUse,
        const std::map<BasicBlock*, int> &blockEnd,
        const std::map<BasicBlock*, std::set<Value*>> &liveOut,
        const std::set<Value*> &crossesCallValues) const;

    /// 构建 phi 亲和组：phi 与其所有 incoming 值双向关联。
    std::map<Value*, std::set<Value*>> buildPhiAffinity(
        const std::vector<BasicBlock*> &blocksOrder) const;

    /// 每个区间的 spill 代价(按循环深度加权的使用次数),并沿 phi 亲和组对齐。
    std::map<Value*, double> computeSpillCost(
        const std::vector<Interval> &intervals,
        std::map<BasicBlock*, int> &loopDepth,
        const std::map<Value*, std::set<Value*>> &phiAffinity) const;

    /// 循环内多指令大常量提升：将高权重 32 位常量预物化到空闲寄存器。
    void promoteLoopConstants(
        const std::vector<BasicBlock*> &blocksOrder,
        const std::map<BasicBlock*, int> &loopDepth,
        bool isLeaf);

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
