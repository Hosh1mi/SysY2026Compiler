#include "../../../include/mid/hira/transform/canonicalize.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/instruction.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace hira {
namespace {

unsigned integerWidth(const HiraValue *value) {
    if (!value)
        return 0;
    auto *integer = dynamic_cast<IntegerType *>(value->type());
    return integer ? integer->num_bits_ : 0;
}

std::uint64_t bitMask(unsigned width) {
    if (width == 0 || width > 64)
        return 0;
    return width == 64 ? ~std::uint64_t{0}
                       : (std::uint64_t{1} << width) - 1;
}

std::uint64_t unsignedBits(std::int64_t value, unsigned width) {
    return static_cast<std::uint64_t>(value) & bitMask(width);
}

std::int64_t signedBits(std::uint64_t value, unsigned width) {
    value &= bitMask(width);
    if (width == 0 || width >= 64)
        return static_cast<std::int64_t>(value);
    const std::uint64_t sign = std::uint64_t{1} << (width - 1);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

bool isIntegerConstant(const HiraValue *value) {
    return value && value->kind() == ValueKind::IntegerConstant;
}

bool isCommutative(ComputeKind kind) {
    switch (kind) {
    case ComputeKind::Add:
    case ComputeKind::Mul:
    case ComputeKind::And:
    case ComputeKind::Or:
    case ComputeKind::Xor:
        return true;
    default:
        return false;
    }
}

int swappedPredicate(int predicate) {
    switch (static_cast<ICmpInst::ICmpOp>(predicate)) {
    case ICmpInst::ICMP_EQ:
    case ICmpInst::ICMP_NE:
        return predicate;
    case ICmpInst::ICMP_UGT:
        return ICmpInst::ICMP_ULT;
    case ICmpInst::ICMP_UGE:
        return ICmpInst::ICMP_ULE;
    case ICmpInst::ICMP_ULT:
        return ICmpInst::ICMP_UGT;
    case ICmpInst::ICMP_ULE:
        return ICmpInst::ICMP_UGE;
    case ICmpInst::ICMP_SGT:
        return ICmpInst::ICMP_SLT;
    case ICmpInst::ICMP_SGE:
        return ICmpInst::ICMP_SLE;
    case ICmpInst::ICMP_SLT:
        return ICmpInst::ICMP_SGT;
    case ICmpInst::ICMP_SLE:
        return ICmpInst::ICMP_SGE;
    }
    return predicate;
}

bool canonicalizeOperandOrder(HiraComputeOp &compute) {
    if (compute.operands().size() != 2 ||
        !isIntegerConstant(compute.operands()[0]) ||
        isIntegerConstant(compute.operands()[1]))
        return false;

    if (isCommutative(compute.computeKind())) {
        compute.swapOperands(0, 1);
        return true;
    }
    if (compute.computeKind() == ComputeKind::ICmp) {
        compute.swapOperands(0, 1);
        compute.setPredicate(swappedPredicate(compute.predicate()));
        return true;
    }
    return false;
}

std::optional<std::int64_t>
foldIntegerCompute(const HiraComputeOp &compute) {
    const auto &operands = compute.operands();
    if (compute.computeKind() == ComputeKind::Select) {
        if (operands.size() != 3 || !isIntegerConstant(operands[0]))
            return std::nullopt;
        HiraValue *selected =
            operands[0]->integerValue() != 0 ? operands[1] : operands[2];
        if (!isIntegerConstant(selected))
            return std::nullopt;
        return selected->integerValue();
    }
    if (compute.computeKind() == ComputeKind::ZExt) {
        if (operands.size() != 1 || !isIntegerConstant(operands[0]))
            return std::nullopt;
        unsigned sourceWidth = integerWidth(operands[0]);
        unsigned resultWidth = integerWidth(compute.results().front());
        if (!sourceWidth || !resultWidth)
            return std::nullopt;
        return signedBits(
            unsignedBits(operands[0]->integerValue(), sourceWidth),
            resultWidth);
    }
    if (operands.size() != 2 ||
        !isIntegerConstant(operands[0]) ||
        !isIntegerConstant(operands[1]))
        return std::nullopt;

    unsigned operandWidth = integerWidth(operands[0]);
    unsigned resultWidth = integerWidth(compute.results().front());
    if (!operandWidth || !resultWidth ||
        operands[1]->type() != operands[0]->type())
        return std::nullopt;

    std::uint64_t left =
        unsignedBits(operands[0]->integerValue(), operandWidth);
    std::uint64_t right =
        unsignedBits(operands[1]->integerValue(), operandWidth);
    std::uint64_t result = 0;
    switch (compute.computeKind()) {
    case ComputeKind::Add:
        result = left + right;
        break;
    case ComputeKind::Sub:
        result = left - right;
        break;
    case ComputeKind::Mul:
        result = left * right;
        break;
    case ComputeKind::And:
        result = left & right;
        break;
    case ComputeKind::Or:
        result = left | right;
        break;
    case ComputeKind::Xor:
        result = left ^ right;
        break;
    case ComputeKind::Shl:
        if (right >= operandWidth)
            return std::nullopt;
        result = left << right;
        break;
    case ComputeKind::LShr:
        if (right >= operandWidth)
            return std::nullopt;
        result = left >> right;
        break;
    case ComputeKind::AShr:
        if (right >= operandWidth)
            return std::nullopt;
        result = left >> right;
        if (right != 0 &&
            (left & (std::uint64_t{1} << (operandWidth - 1))))
            result |= bitMask(operandWidth) ^
                      (bitMask(operandWidth) >> right);
        break;
    case ComputeKind::ICmp: {
        bool comparison = false;
        switch (static_cast<ICmpInst::ICmpOp>(compute.predicate())) {
        case ICmpInst::ICMP_EQ:
            comparison = left == right;
            break;
        case ICmpInst::ICMP_NE:
            comparison = left != right;
            break;
        case ICmpInst::ICMP_UGT:
            comparison = left > right;
            break;
        case ICmpInst::ICMP_UGE:
            comparison = left >= right;
            break;
        case ICmpInst::ICMP_ULT:
            comparison = left < right;
            break;
        case ICmpInst::ICMP_ULE:
            comparison = left <= right;
            break;
        case ICmpInst::ICMP_SGT:
            comparison = signedBits(left, operandWidth) >
                         signedBits(right, operandWidth);
            break;
        case ICmpInst::ICMP_SGE:
            comparison = signedBits(left, operandWidth) >=
                         signedBits(right, operandWidth);
            break;
        case ICmpInst::ICMP_SLT:
            comparison = signedBits(left, operandWidth) <
                         signedBits(right, operandWidth);
            break;
        case ICmpInst::ICMP_SLE:
            comparison = signedBits(left, operandWidth) <=
                         signedBits(right, operandWidth);
            break;
        }
        result = comparison ? 1 : 0;
        break;
    }
    case ComputeKind::Select:
    case ComputeKind::GetElementPtr:
    case ComputeKind::ZExt:
        return std::nullopt;
    }
    return signedBits(result, resultWidth);
}

HiraValue *identityReplacement(const HiraComputeOp &compute) {
    const auto &operands = compute.operands();
    if (compute.computeKind() == ComputeKind::Select &&
        operands.size() == 3 && isIntegerConstant(operands[0]))
        return operands[0]->integerValue() != 0 ? operands[1]
                                                : operands[2];
    if (compute.computeKind() == ComputeKind::ZExt &&
        operands.size() == 1 &&
        operands[0]->type() == compute.results().front()->type())
        return operands[0];
    if (operands.size() != 2 || !isIntegerConstant(operands[1]))
        return nullptr;
    const std::int64_t constant = operands[1]->integerValue();
    switch (compute.computeKind()) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Or:
    case ComputeKind::Xor:
        return constant == 0 ? operands[0] : nullptr;
    case ComputeKind::Mul:
        return constant == 1 ? operands[0] : nullptr;
    case ComputeKind::And: {
        unsigned width = integerWidth(operands[1]);
        return width &&
                       unsignedBits(constant, width) == bitMask(width)
                   ? operands[0]
                   : nullptr;
    }
    default:
        return nullptr;
    }
}

class RegionCanonicalizer {
public:
    explicit RegionCanonicalizer(HiraRegion &region) : region_(region) {}

    bool run() {
        canonicalizeSequence(region_.rootSequence());
        if (changed_)
            region_.markModified();
        return changed_;
    }

private:
    void replaceAndErase(HiraSequence &sequence, HiraComputeOp &compute,
                         HiraValue *replacement) {
        HiraValue *result = compute.results().front();
        region_.replaceAllUses(result, replacement);
        region_.sourceMapping().unmapNode(&compute);
        region_.sourceMapping().unmapValue(result);
        sequence.remove(&compute);
        changed_ = true;
    }

    bool isRegionResult(const HiraComputeOp &compute) const {
        HiraValue *result = compute.results().front();
        const auto &regionResults = region_.results();
        return std::find(regionResults.begin(), regionResults.end(),
                         result) != regionResults.end();
    }

    void canonicalizeSequence(HiraSequence &sequence) {
        std::size_t index = 0;
        while (index < sequence.nodes().size()) {
            HiraNode *node = sequence.nodes()[index].get();
            if (auto *loop = dynamic_cast<HiraLoop *>(node)) {
                canonicalizeSequence(loop->body());
                ++index;
                continue;
            }
            if (auto *condition = dynamic_cast<HiraIf *>(node)) {
                canonicalizeSequence(condition->thenSequence());
                canonicalizeSequence(condition->elseSequence());
                ++index;
                continue;
            }
            auto *compute = dynamic_cast<HiraComputeOp *>(node);
            if (!compute) {
                ++index;
                continue;
            }

            changed_ |= canonicalizeOperandOrder(*compute);
            if (isRegionResult(*compute)) {
                ++index;
                continue;
            }
            if (HiraValue *replacement =
                    identityReplacement(*compute)) {
                replaceAndErase(sequence, *compute, replacement);
                continue;
            }
            if (std::optional<std::int64_t> folded =
                    foldIntegerCompute(*compute)) {
                HiraValue *constant = region_.createIntegerConstant(
                    compute->results().front()->type(), *folded);
                replaceAndErase(sequence, *compute, constant);
                continue;
            }
            ++index;
        }
    }

    HiraRegion &region_;
    bool changed_ = false;
};

} // namespace

bool canonicalizeHiraRegion(HiraRegion &region) {
    return RegionCanonicalizer(region).run();
}

} // namespace hira
