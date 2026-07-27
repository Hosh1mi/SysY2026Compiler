#include "../../../include/mid/hira/polyhedral/reductionAnalysis.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <limits>
#include <sstream>

namespace hira::polyhedral {
namespace {

std::optional<ReductionOperator> reductionOperator(
    ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::FAdd:
        return ReductionOperator::Add;
    case ComputeKind::Mul:
    case ComputeKind::FMul:
        return ReductionOperator::Multiply;
    case ComputeKind::And:
        return ReductionOperator::BitAnd;
    case ComputeKind::Or:
        return ReductionOperator::BitOr;
    case ComputeKind::Xor:
        return ReductionOperator::BitXor;
    default:
        return std::nullopt;
    }
}

std::optional<std::int64_t> identityFor(
    ReductionOperator operation, Type *type) {
    auto *integer = dynamic_cast<IntegerType *>(type);
    if (!integer || !integer->num_bits_ ||
        integer->num_bits_ > 64)
        return std::nullopt;
    switch (operation) {
    case ReductionOperator::Add:
    case ReductionOperator::BitOr:
    case ReductionOperator::BitXor:
        return 0;
    case ReductionOperator::Multiply:
        return 1;
    case ReductionOperator::BitAnd:
        if (integer->num_bits_ == 64)
            return -1;
        return static_cast<std::int64_t>(
            (std::uint64_t{1} << integer->num_bits_) - 1);
    }
    return std::nullopt;
}

ReductionParallelSemantics parallelSemantics(
    ReductionOperator operation, Type *type) {
    // Bitwise integer reductions are exactly associative.  Arithmetic
    // reductions remain ordered until the source language or IR provides a
    // no-overflow/reassociation contract; floating-point reductions are
    // likewise never reassociated implicitly.
    if (dynamic_cast<IntegerType *>(type) &&
        (operation == ReductionOperator::BitAnd ||
         operation == ReductionOperator::BitOr ||
         operation == ReductionOperator::BitXor))
        return ReductionParallelSemantics::Exact;
    return ReductionParallelSemantics::SequentialOnly;
}

const char *operatorName(ReductionOperator operation) {
    switch (operation) {
    case ReductionOperator::Add:
        return "add";
    case ReductionOperator::Multiply:
        return "mul";
    case ReductionOperator::BitAnd:
        return "and";
    case ReductionOperator::BitOr:
        return "or";
    case ReductionOperator::BitXor:
        return "xor";
    }
    return "unknown";
}

bool same(const ReductionAnalysisResult &left,
          const ReductionAnalysisResult &right) {
    if (left.nonReductions() != right.nonReductions() ||
        left.scalarReductions().size() !=
            right.scalarReductions().size())
        return false;
    for (std::size_t index = 0;
         index < left.scalarReductions().size(); ++index) {
        const ScalarReduction &a =
            left.scalarReductions()[index];
        const ScalarReduction &b =
            right.scalarReductions()[index];
        if (a.recurrence != b.recurrence ||
            a.operation != b.operation ||
            a.parallelSemantics != b.parallelSemantics ||
            a.input != b.input ||
            a.integerIdentity != b.integerIdentity)
            return false;
    }
    return true;
}

} // namespace

ReductionAnalysisResult analyzeReductions(
    const PolyhedralModel &model) {
    ReductionAnalysisResult result;
    for (const ScalarRecurrence &recurrence :
         model.scalarRecurrences()) {
        auto *update = recurrence.yielded
                           ? dynamic_cast<HiraComputeOp *>(
                                 recurrence.yielded
                                     ->definingNode())
                           : nullptr;
        auto operation =
            update ? reductionOperator(
                         update->computeKind())
                   : std::nullopt;
        if (!update || !operation ||
            update->operands().size() != 2) {
            result.nonReductions_.push_back(recurrence.id);
            continue;
        }
        HiraValue *input = nullptr;
        if (update->operands()[0] ==
            recurrence.iteration)
            input = update->operands()[1];
        else if (update->operands()[1] ==
                 recurrence.iteration)
            input = update->operands()[0];
        if (!input) {
            result.nonReductions_.push_back(recurrence.id);
            continue;
        }

        ScalarReduction reduction;
        reduction.recurrence = recurrence.id;
        reduction.operation = *operation;
        reduction.parallelSemantics =
            parallelSemantics(
                *operation,
                recurrence.iteration->type());
        reduction.input = input;
        reduction.integerIdentity = identityFor(
            *operation, recurrence.iteration->type());
        result.scalarReductions_.push_back(reduction);
    }
    return result;
}

bool verifyReductionAnalysis(
    const PolyhedralModel &model,
    const ReductionAnalysisResult &result,
    std::string &detail) {
    ReductionAnalysisResult expected =
        analyzeReductions(model);
    if (!same(result, expected) ||
        result.scalarReductions().size() +
                result.nonReductions().size() !=
            model.scalarRecurrences().size()) {
        detail = "invalid-reduction-analysis";
        return false;
    }
    return true;
}

std::string printReductionAnalysis(
    const ReductionAnalysisResult &result) {
    std::ostringstream out;
    out << "polyhedral.reductions {\n";
    for (const ScalarReduction &reduction :
         result.scalarReductions()) {
        out << "  R" << reduction.recurrence << " "
            << operatorName(reduction.operation)
            << " semantics="
            << (reduction.parallelSemantics ==
                        ReductionParallelSemantics::Exact
                    ? "parallel-exact"
                    : "sequential-only");
        if (reduction.integerIdentity)
            out << " identity="
                << *reduction.integerIdentity;
        out << "\n";
    }
    for (ScalarRecurrenceId recurrence :
         result.nonReductions())
        out << "  R" << recurrence
            << " non-reduction\n";
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
