#pragma once
// Shared target cost model for deciding whether existing scalar IR should be
// widened to the compiler's fixed-width i32/f32 vector representation.

#include "../ir/instruction.hpp"

#include <cstddef>

class VectorizationCostModel {
public:
    static constexpr int VectorWidth = 4;

    int scalarInstructionCost(const Instruction *inst) const;
    int vectorInstructionCost(const Instruction *inst) const;
    int extractElementCost() const { return 6; }
    int insertElementCost() const { return 6; }
    int shuffleCost(const std::vector<int> &mask) const;

    int setupCost(std::size_t splats, std::size_t runtimeChecks,
                  std::size_t addressGroups) const;
    int vectorLoopControlCost() const { return 2; }
    int maximumUnrolledLiveVectors() const { return 30; }

    int minimumProfitableTripCount(int scalarLaneCost,
                                   int vectorPartCost,
                                   int setup,
                                   int unrollFactor) const;
};
