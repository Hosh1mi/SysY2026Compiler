#include "../../../include/mid/hira/ir/hiraVerifier.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <set>
#include <string>

namespace hira {
namespace {

bool isIntegerWidth(const HiraValue *value, unsigned width) {
    if (!value)
        return false;
    auto *integer = dynamic_cast<IntegerType *>(value->type());
    return integer && integer->num_bits_ == width;
}

bool isVectorMaskFor(const HiraValue *mask,
                     const HiraValue *value) {
    auto *maskType =
        mask ? dynamic_cast<VectorType *>(mask->type())
             : nullptr;
    auto *valueType =
        value ? dynamic_cast<VectorType *>(value->type())
              : nullptr;
    auto *element =
        maskType
            ? dynamic_cast<IntegerType *>(
                  maskType->contained_)
            : nullptr;
    return maskType && valueType && element &&
           element->num_bits_ == 32 &&
           maskType->num_elements_ ==
               valueType->num_elements_;
}

bool isConstant(const HiraValue *value) {
    return value &&
           (value->kind() == ValueKind::IntegerConstant ||
            value->kind() == ValueKind::FloatConstant);
}

bool isBinaryCompute(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::SDiv:
    case ComputeKind::SRem:
    case ComputeKind::UDiv:
    case ComputeKind::URem:
    case ComputeKind::FAdd:
    case ComputeKind::FSub:
    case ComputeKind::FMul:
    case ComputeKind::FDiv:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
    case ComputeKind::Shl:
    case ComputeKind::LShr:
    case ComputeKind::AShr:
        return true;
    default:
        return false;
    }
}

bool isValidPredicate(int predicate) {
    return predicate >= ICmpInst::ICMP_EQ &&
           predicate <= ICmpInst::ICMP_SLE;
}

class RegionVerifier {
public:
    explicit RegionVerifier(const HiraRegion &region) : region_(region) {}

    HiraVerificationResult run() {
        std::set<const HiraValue *> available;
        for (const HiraValue *parameter : region_.parameters()) {
            if (!parameter || !parameter->type() ||
                parameter->kind() != ValueKind::Parameter ||
                parameter->definingNode())
                return fail(HiraVerifyError::InvalidBoundary,
                            "invalid-parameter");
            if (!definedValues_.insert(parameter).second)
                return fail(HiraVerifyError::InvalidBoundary,
                            "duplicate-parameter");
            available.insert(parameter);
        }

        for (const HiraValue *scratch : region_.scratches()) {
            auto *pointer =
                scratch
                    ? dynamic_cast<PointerType *>(scratch->type())
                    : nullptr;
            if (!pointer ||
                scratch->kind() != ValueKind::Scratch ||
                !scratch->allocatedType() ||
                pointer->contained_ != scratch->allocatedType() ||
                scratch->definingNode())
                return fail(HiraVerifyError::InvalidBoundary,
                            "invalid-scratch");
            if (!definedValues_.insert(scratch).second)
                return fail(HiraVerifyError::InvalidBoundary,
                            "duplicate-scratch");
            available.insert(scratch);
        }

        if (!verifySequence(region_.rootSequence(), available, nullptr))
            return failure_;
        for (const HiraValue *result : region_.results())
            if (!isAvailable(result, available))
                return fail(HiraVerifyError::InvalidBoundary,
                            "unavailable-region-result");
        return {};
    }

private:
    HiraVerificationResult fail(HiraVerifyError error,
                                std::string detail) {
        failure_.error = error;
        failure_.detail = std::move(detail);
        return failure_;
    }

    bool reject(HiraVerifyError error, std::string detail) {
        fail(error, std::move(detail));
        return false;
    }

    bool isAvailable(
        const HiraValue *value,
        const std::set<const HiraValue *> &available) const {
        return value && value->type() &&
               (isConstant(value) || available.count(value));
    }

    bool requireOperands(
        const HiraNode &node,
        const std::set<const HiraValue *> &available) {
        for (const HiraValue *operand : node.operands())
            if (!isAvailable(operand, available))
                return reject(HiraVerifyError::ScopeViolation,
                              "unavailable-operand");
        return true;
    }

    bool defineResult(const HiraNode &node, const HiraValue *value,
                      std::set<const HiraValue *> &available) {
        if (!value || !value->type() || value->definingNode() != &node)
            return reject(HiraVerifyError::InvalidResult,
                          "invalid-definition");
        if (!definedValues_.insert(value).second)
            return reject(HiraVerifyError::InvalidResult,
                          "duplicate-definition");
        available.insert(value);
        return true;
    }

    bool verifyCompute(const HiraComputeOp &compute,
                       std::set<const HiraValue *> &available) {
        if (!requireOperands(compute, available))
            return false;
        if (compute.results().size() != 1)
            return reject(HiraVerifyError::InvalidNode,
                          "invalid-compute-interface");

        std::size_t expectedOperands = 2;
        if (compute.computeKind() == ComputeKind::Select)
            expectedOperands = 3;
        else if (compute.computeKind() == ComputeKind::ZExt ||
                 compute.computeKind() == ComputeKind::BitCast ||
                 compute.computeKind() == ComputeKind::Splat)
            expectedOperands = 1;
        else if (compute.computeKind() ==
                 ComputeKind::ExtractElement)
            expectedOperands = 2;
        else if (compute.computeKind() == ComputeKind::GetElementPtr)
            expectedOperands = compute.operands().size();
        if (compute.operands().size() != expectedOperands ||
            (compute.computeKind() == ComputeKind::GetElementPtr &&
             expectedOperands < 2))
            return reject(HiraVerifyError::InvalidNode,
                          "invalid-compute-arity");

        HiraValue *result = compute.results().front();
        if (compute.computeKind() == ComputeKind::ICmp) {
            auto *operandVector =
                dynamic_cast<VectorType *>(
                    compute.operands()[0]->type());
            auto *resultVector =
                dynamic_cast<VectorType *>(
                    result->type());
            if (!isValidPredicate(compute.predicate()) ||
                compute.operands()[0]->type() !=
                    compute.operands()[1]->type() ||
                ((!operandVector &&
                  !isIntegerWidth(result, 1)) ||
                 (operandVector &&
                  (!resultVector ||
                   !isVectorMaskFor(
                       result,
                       compute.operands()[0])))))
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-icmp-types");
        } else if (compute.computeKind() == ComputeKind::Select) {
            const bool validCondition =
                isIntegerWidth(compute.operands()[0], 1) ||
                isVectorMaskFor(compute.operands()[0],
                                result);
            if (!validCondition ||
                result->type() != compute.operands()[1]->type() ||
                result->type() != compute.operands()[2]->type())
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-select-types");
        } else if (isBinaryCompute(compute.computeKind())) {
            if (result->type() != compute.operands()[0]->type() ||
                result->type() != compute.operands()[1]->type())
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-binary-types");
        } else if (compute.computeKind() == ComputeKind::ZExt) {
            auto *source =
                dynamic_cast<IntegerType *>(compute.operands()[0]->type());
            auto *destination =
                dynamic_cast<IntegerType *>(result->type());
            if (!source || !destination ||
                source->num_bits_ >= destination->num_bits_)
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-zext-types");
        } else if (compute.computeKind() ==
                   ComputeKind::BitCast) {
            if (!dynamic_cast<PointerType *>(
                    compute.operands()[0]->type()) ||
                !dynamic_cast<PointerType *>(result->type()))
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-bitcast-types");
        } else if (compute.computeKind() ==
                   ComputeKind::Splat) {
            auto *vector =
                dynamic_cast<VectorType *>(result->type());
            if (!vector || vector->num_elements_ <= 1 ||
                vector->contained_ !=
                    compute.operands()[0]->type())
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-splat-types");
        } else if (compute.computeKind() ==
                   ComputeKind::ExtractElement) {
            auto *vector =
                dynamic_cast<VectorType *>(
                    compute.operands()[0]->type());
            if (!vector ||
                result->type() != vector->contained_ ||
                compute.operands()[1]->kind() !=
                    ValueKind::IntegerConstant)
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-extract-types");
        }
        return defineResult(compute, result, available);
    }

    bool verifyIf(const HiraIf &condition,
                  std::set<const HiraValue *> &available) {
        if (condition.operands().size() !=
                condition.resultBindings().size() * 2 + 1 ||
            condition.results().size() !=
                condition.resultBindings().size() ||
            !isAvailable(condition.condition(), available) ||
            !isIntegerWidth(condition.condition(), 1))
            return reject(HiraVerifyError::InvalidNode,
                          "invalid-if-interface");
        auto thenAvailable = available;
        if (!verifySequence(condition.thenSequence(), thenAvailable,
                            nullptr))
            return false;
        auto elseAvailable = available;
        if (!verifySequence(condition.elseSequence(), elseAvailable,
                            nullptr))
            return false;
        for (std::size_t index = 0;
             index < condition.resultBindings().size(); ++index) {
            const HiraIf::ResultBinding &binding =
                condition.resultBindings()[index];
            if (condition.results()[index] != binding.result ||
                !isAvailable(binding.thenValue, thenAvailable) ||
                !isAvailable(binding.elseValue, elseAvailable) ||
                !binding.result ||
                binding.result->type() !=
                    binding.thenValue->type() ||
                binding.result->type() !=
                    binding.elseValue->type())
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-if-result");
            if (!defineResult(condition, binding.result,
                              available))
                return false;
        }
        return true;
    }

    bool verifyLoop(const HiraLoop &loop,
                    std::set<const HiraValue *> &available) {
        if (loop.operands().size() !=
                loop.carriedValues().size() + 3 ||
            loop.results().size() !=
                loop.carriedValues().size() * 2 + 1 ||
            !isAvailable(loop.lowerBound(), available) ||
            !isAvailable(loop.upperBound(), available) ||
            !isConstant(loop.step()) ||
            loop.step()->kind() != ValueKind::IntegerConstant ||
            loop.step()->integerValue() <= 0 ||
            !loop.induction() || !loop.induction()->type() ||
            !dynamic_cast<IntegerType *>(loop.induction()->type()) ||
            loop.lowerBound()->type() != loop.induction()->type() ||
            loop.upperBound()->type() != loop.induction()->type() ||
            loop.step()->type() != loop.induction()->type())
            return reject(HiraVerifyError::InvalidLoop,
                          "non-canonical-loop-control");

        auto bodyAvailable = available;
        if (loop.results().front() != loop.induction())
            return reject(HiraVerifyError::InvalidLoop,
                          "invalid-induction-result");
        if (!defineResult(loop, loop.induction(), bodyAvailable))
            return false;

        for (std::size_t index = 0;
             index < loop.carriedValues().size(); ++index) {
            const auto &binding = loop.carriedValues()[index];
            if (!isAvailable(binding.initial, available) ||
                !binding.iteration || !binding.result ||
                binding.initial->type() != binding.iteration->type() ||
                binding.initial->type() != binding.result->type() ||
                loop.results()[index * 2 + 1] != binding.iteration ||
                loop.results()[index * 2 + 2] != binding.result)
                return reject(HiraVerifyError::InvalidLoop,
                              "invalid-carried-binding");
            if (!defineResult(loop, binding.iteration, bodyAvailable) ||
                !defineResult(loop, binding.result, available))
                return false;
        }

        if (!verifySequence(loop.body(), bodyAvailable, &loop))
            return false;
        return true;
    }

    bool verifyOrdinaryNode(
        const HiraNode &node,
        std::set<const HiraValue *> &available) {
        if (auto *compute = dynamic_cast<const HiraComputeOp *>(&node))
            return verifyCompute(*compute, available);
        if (dynamic_cast<const HiraLoad *>(&node)) {
            if (!requireOperands(node, available))
                return false;
            if (node.operands().size() != 1 ||
                node.results().size() != 1)
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-load-interface");
            return defineResult(node, node.results().front(), available);
        }
        if (dynamic_cast<const HiraStore *>(&node)) {
            if (!requireOperands(node, available))
                return false;
            if (node.operands().size() != 2 ||
                !node.results().empty())
                return reject(HiraVerifyError::InvalidNode,
                              "invalid-store-interface");
            return true;
        }
        return reject(HiraVerifyError::InvalidNode,
                      "unsupported-node");
    }

    bool verifyYield(
        const HiraYield &yield, const HiraLoop &loop,
        const std::set<const HiraValue *> &available) {
        if (!requireOperands(yield, available))
            return false;
        if (!yield.results().empty() ||
            yield.operands().size() != loop.yieldValues().size() ||
            loop.yieldValues().size() !=
                loop.carriedValues().size() + 1)
            return reject(HiraVerifyError::InvalidYield,
                          "invalid-yield-interface");
        for (std::size_t index = 0;
             index < yield.operands().size(); ++index)
            if (yield.operands()[index] != loop.yieldValues()[index])
                return reject(HiraVerifyError::InvalidYield,
                              "mismatched-yield-order");
        for (std::size_t index = 0;
             index < loop.carriedValues().size(); ++index)
            if (loop.carriedValues()[index].yielded !=
                    yield.operands()[index + 1] ||
                loop.carriedValues()[index].iteration->type() !=
                    yield.operands()[index + 1]->type())
                return reject(HiraVerifyError::InvalidYield,
                              "mismatched-carried-yield");
        return yield.operands().front()->type() ==
                       loop.induction()->type() ||
               reject(HiraVerifyError::InvalidYield,
                      "invalid-induction-yield");
    }

    bool verifySequence(
        const HiraSequence &sequence,
        std::set<const HiraValue *> &available,
        const HiraLoop *ownerLoop) {
        const auto &nodes = sequence.nodes();
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const HiraNode &node = *nodes[index];
            if (node.parent() != &sequence)
                return reject(HiraVerifyError::InvalidParent,
                              "incorrect-parent-sequence");

            if (auto *yield = dynamic_cast<const HiraYield *>(&node)) {
                if (!ownerLoop || index + 1 != nodes.size())
                    return reject(HiraVerifyError::InvalidYield,
                                  "misplaced-yield");
                if (!verifyYield(*yield, *ownerLoop, available))
                    return false;
                continue;
            }
            if (auto *loop = dynamic_cast<const HiraLoop *>(&node)) {
                if (!verifyLoop(*loop, available))
                    return false;
                continue;
            }
            if (auto *condition = dynamic_cast<const HiraIf *>(&node)) {
                if (!verifyIf(*condition, available))
                    return false;
                continue;
            }
            if (!verifyOrdinaryNode(node, available))
                return false;
        }
        if (ownerLoop &&
            (nodes.empty() ||
             !dynamic_cast<const HiraYield *>(nodes.back().get())))
            return reject(HiraVerifyError::InvalidYield,
                          "missing-loop-yield");
        return true;
    }

    const HiraRegion &region_;
    std::set<const HiraValue *> definedValues_;
    HiraVerificationResult failure_;
};

} // namespace

const char *hiraVerifyErrorName(HiraVerifyError error) {
    switch (error) {
    case HiraVerifyError::None:
        return "none";
    case HiraVerifyError::InvalidBoundary:
        return "invalid-boundary";
    case HiraVerifyError::InvalidParent:
        return "invalid-parent";
    case HiraVerifyError::InvalidNode:
        return "invalid-node";
    case HiraVerifyError::InvalidOperand:
        return "invalid-operand";
    case HiraVerifyError::InvalidResult:
        return "invalid-result";
    case HiraVerifyError::InvalidLoop:
        return "invalid-loop";
    case HiraVerifyError::InvalidYield:
        return "invalid-yield";
    case HiraVerifyError::ScopeViolation:
        return "scope-violation";
    }
    return "unknown";
}

HiraVerificationResult verifyHiraRegion(const HiraRegion &region) {
    return RegionVerifier(region).run();
}

} // namespace hira
