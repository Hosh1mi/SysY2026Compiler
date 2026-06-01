#pragma once
#include "../../mid/ir/ir.hpp"
#include <map>
#include <string>

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

    /// Convenience accessors on the result set.
    bool hasAssignedReg(Value *v) const;
    std::string assignedReg(Value *v, bool asAddress = false) const;

private:
    bool canAssignRegister(Value *v) const;

    struct Interval {
        Value *value;
        int start;
        int end;
        bool isFloat;
        bool isPtr;
        bool isNEON;
    };

    void colorPool(const std::vector<Interval> &pool,
                   const std::vector<int> &colorToReg, bool isFloat,
                   const std::map<Value*, double> &spillCost,
                   const std::map<Value*, std::set<Value*>> &phiAffinity);

    Function *func_;
    std::map<Value*, std::string> assignedRegs_;
};
