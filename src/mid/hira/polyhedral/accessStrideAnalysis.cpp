#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <limits>
#include <vector>

namespace hira::polyhedral {
namespace {

using WideInt = __int128;
using WideUInt = unsigned __int128;

constexpr WideInt kWideMax =
    static_cast<WideInt>((static_cast<WideUInt>(1) << 127) - 1);
constexpr WideInt kWideMin = -kWideMax - 1;
constexpr WideInt kMaxSupportedSize =
    std::numeric_limits<std::int64_t>::max();

bool checkedAdd(WideInt &value, WideInt increment) {
    if ((increment > 0 && value > kWideMax - increment) ||
        (increment < 0 && value < kWideMin - increment))
        return false;
    value += increment;
    return true;
}

std::optional<WideInt> scaleTypeSize(
    WideInt elementSize, unsigned count) {
    if (count &&
        elementSize >
            kMaxSupportedSize / static_cast<WideInt>(count))
        return std::nullopt;
    return elementSize * count;
}

std::optional<WideInt> typeSize(Type *type) {
    if (!type)
        return std::nullopt;
    switch (type->tid_) {
    case Type::IntegerTyID: {
        auto *integer = static_cast<IntegerType *>(type);
        return (integer->num_bits_ + 7) / 8;
    }
    case Type::FloatTyID:
        return 4;
    case Type::PointerTyID:
        return 8;
    case Type::ArrayTyID: {
        auto *array = static_cast<ArrayType *>(type);
        auto element = typeSize(array->contained_);
        if (!element)
            return std::nullopt;
        return scaleTypeSize(*element, array->num_elements_);
    }
    case Type::VectorTyID: {
        auto *vector = static_cast<VectorType *>(type);
        auto element = typeSize(vector->contained_);
        if (!element)
            return std::nullopt;
        return scaleTypeSize(*element, vector->num_elements_);
    }
    default:
        return std::nullopt;
    }
}

std::optional<std::vector<WideInt>> subscriptWeights(
    const MemoryObject &object, std::size_t subscriptCount) {
    auto *pointer =
        object.base
            ? dynamic_cast<PointerType *>(object.base->type())
            : nullptr;
    if (!pointer || !subscriptCount)
        return std::nullopt;

    Type *current = pointer->contained_;
    std::vector<WideInt> weights;
    weights.reserve(subscriptCount);
    for (std::size_t index = 0; index < subscriptCount;
         ++index) {
        if (index) {
            if (auto *array =
                    dynamic_cast<ArrayType *>(current))
                current = array->contained_;
            else if (auto *nestedPointer =
                         dynamic_cast<PointerType *>(current))
                current = nestedPointer->contained_;
            else
                return std::nullopt;
        }
        auto size = typeSize(current);
        if (!size)
            return std::nullopt;
        weights.push_back(*size);
    }
    return weights;
}

} // namespace

std::optional<std::int64_t> analyzeLinearAccessStride(
    const PolyhedralModel &model,
    const AccessRelation &access,
    AffineVariable dimension) {
    if (access.object >= model.memoryObjects().size())
        return std::nullopt;
    auto weights = subscriptWeights(
        model.memoryObjects()[access.object],
        access.subscripts.size());
    if (!weights ||
        weights->size() != access.subscripts.size())
        return std::nullopt;

    WideInt stride = 0;
    for (std::size_t index = 0;
         index < access.subscripts.size(); ++index) {
        auto coefficient =
            access.subscripts[index].coefficients().find(
                dimension);
        if (coefficient !=
            access.subscripts[index].coefficients().end()) {
            WideInt contribution =
                static_cast<WideInt>(coefficient->second) *
                (*weights)[index];
            if (!checkedAdd(stride, contribution))
                return std::nullopt;
        }
    }
    WideUInt magnitude =
        stride < 0
            ? static_cast<WideUInt>(-(stride + 1)) + 1
            : static_cast<WideUInt>(stride);
    if (magnitude >
        static_cast<WideUInt>(
            std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(magnitude);
}

} // namespace hira::polyhedral
