#pragma once

#include "machine_ir.hpp"

#include <set>
#include <unordered_map>
#include <vector>

namespace backend::aarch64 {

struct LiveInterval {
    VReg reg = 0;
    RegClass regClass = RegClass::Invalid;
    unsigned start = 0;
    unsigned end = 0;
    double weight = 0.0;
    bool crossesCall = false;

    bool overlaps(const LiveInterval &other) const {
        return start < other.end && other.start < end;
    }
};

struct LivenessResult {
    std::vector<LiveInterval> intervals;
    std::unordered_map<MachineBasicBlock *, std::set<VReg>> blockLiveOut;
};

class MachineLiveness {
public:
    LivenessResult run(MachineFunction &function) const;
};

class PhiElimination {
public:
    bool run(MachineFunction &function) const;
};

/// Chaitin-Briggs optimistic global graph-coloring allocator over typed
/// Machine virtual registers.  COPY affinities bias color selection; spill
/// candidates are chosen only during simplify and are retried optimistically
/// during select before spill code is inserted.
class GraphColoringRegisterAllocator {
public:
    void run(MachineFunction &function) const;

private:
    bool colorOnce(
        MachineFunction &function, const LivenessResult &liveness,
        std::unordered_map<VReg, PhysReg> &assignments,
        std::vector<VReg> &spills) const;
    void insertSpills(MachineFunction &function,
                      const std::vector<VReg> &spills,
                      std::unordered_map<VReg, int> &spillSlots) const;
    void rewriteVirtualRegisters(
        MachineFunction &function,
        const std::unordered_map<VReg, PhysReg> &assignments) const;
};

class PostRAParallelCopyResolver {
public:
    bool run(MachineFunction &function) const;
};

} // namespace backend::aarch64
