// Target-aware cost estimates shared by the loop and basic-block vectorizers.
// Values are relative scheduling costs, not claimed cycle-accurate timings.

#include "../../include/mid/analysis/vectorizationCostModel.hpp"
#include "../../include/mid/ir/intrinsics.hpp"

#include <algorithm>

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
