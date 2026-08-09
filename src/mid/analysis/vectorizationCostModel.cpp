// Target-aware cost estimates shared by the loop and basic-block vectorizers.
// Values are relative scheduling costs, not claimed cycle-accurate timings.

#include "../../include/mid/analysis/vectorizationCostModel.hpp"
#include "../../include/mid/ir/intrinsics.hpp"

#include <algorithm>
#include <array>

namespace {

int arithmeticCost(const Instruction *inst, bool vector) {
    auto *binary = dynamic_cast<const BinaryInst *>(inst);
    if (!binary) {
        auto *call = dynamic_cast<const CallInst *>(inst);
        auto *callee = call && call->num_ops_
                           ? dynamic_cast<Function *>(
                                 call->get_operand(call->num_ops_ - 1))
                           : nullptr;
        return callee && isSignedMinMaxIntrinsic(callee)
                   ? (vector ? 3 : 1)
                   : 1;
    }

    switch (binary->op_id_) {
    case Instruction::Mul:
    case Instruction::FMul:
        return vector ? 4 : 3;
    case Instruction::FDiv:
        return vector ? 14 : 14;
    case Instruction::FAdd:
    case Instruction::FSub:
        return vector ? 3 : 2;
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
        return vector ? 2 : 1;
    default:
        return vector ? 2 : 1;
    }
}

} // namespace

int VectorizationCostModel::shuffleCost(const std::vector<int> &mask) const {
    if (mask.size() != VectorWidth)
        return 24;

    const std::array<std::array<int, VectorWidth>, 10> native = {{
        {{0, 1, 2, 3}}, {{4, 5, 6, 7}},
        {{0, 4, 1, 5}}, {{2, 6, 3, 7}},
        {{0, 2, 4, 6}}, {{1, 3, 5, 7}},
        {{0, 4, 2, 6}}, {{1, 5, 3, 7}},
        {{1, 0, 3, 2}}, {{5, 4, 7, 6}},
    }};
    for (const auto &pattern : native) {
        if (std::equal(mask.begin(), mask.end(), pattern.begin()))
            return pattern == native[0] || pattern == native[1] ? 0 : 1;
    }

    if (std::all_of(mask.begin() + 1, mask.end(),
                    [&](int lane) { return lane == mask.front(); }) &&
        mask.front() >= 0 && mask.front() < 2 * VectorWidth)
        return 1;

    if (mask == std::vector<int>({3, 2, 1, 0}) ||
        mask == std::vector<int>({7, 6, 5, 4}))
        return 2;

    for (int start = 1; start < VectorWidth; ++start) {
        bool contiguous = true;
        for (int lane = 0; lane < VectorWidth; ++lane)
            contiguous &= mask[lane] == start + lane;
        if (contiguous)
            return 1;
    }

    // The current backend scalarizes uncommon two-source permutations.
    return 24;
}

int VectorizationCostModel::scalarInstructionCost(
    const Instruction *inst) const {
    if (inst->op_id_ == Instruction::Load) return 3;
    if (inst->op_id_ == Instruction::Store) return 2;
    return arithmeticCost(inst, false);
}

int VectorizationCostModel::vectorInstructionCost(
    const Instruction *inst) const {
    if (inst->op_id_ == Instruction::Load) return 4;
    if (inst->op_id_ == Instruction::Store) return 2;
    return arithmeticCost(inst, true);
}

int VectorizationCostModel::setupCost(std::size_t splats,
                                      std::size_t runtimeChecks,
                                      std::size_t addressGroups) const {
    // Entry comparison and vector-end construction are always present.  Each
    // alias check builds two range ends and two comparisons, while normalized
    // address groups need one initial pointer expression.
    return 4 + static_cast<int>(splats) * 2 +
           static_cast<int>(runtimeChecks) * 6 +
           static_cast<int>(addressGroups);
}

int VectorizationCostModel::minimumProfitableTripCount(
    int scalarLaneCost, int vectorPartCost, int setup,
    int unrollFactor) const {
    const int width = VectorWidth * unrollFactor;
    const int scalarChunk = scalarLaneCost * width;
    const int vectorChunk = vectorPartCost * unrollFactor +
                            vectorLoopControlCost();
    const int savings = scalarChunk - vectorChunk;
    if (savings <= 0) return 0;

    // Require two complete chunks even when setup is nominally free.  This
    // prevents a single vector iteration plus scalar epilogue from winning a
    // purely steady-state comparison.
    const int repaymentChunks = (setup + savings - 1) / savings;
    return std::max(2, repaymentChunks + 1) * width;
}
