#include "../../../include/mid/hira/polyhedral/affineExpr.hpp"

namespace hira::polyhedral {
namespace {

bool checkedAdd(std::int64_t left, std::int64_t right,
                std::int64_t &result) {
    return !__builtin_add_overflow(left, right, &result);
}

bool checkedMultiply(std::int64_t left, std::int64_t right,
                     std::int64_t &result) {
    return !__builtin_mul_overflow(left, right, &result);
}

} // namespace

AffineExpr AffineExpr::invalid() {
    AffineExpr expression;
    expression.valid_ = false;
    return expression;
}

AffineExpr AffineExpr::constant(std::int64_t value) {
    AffineExpr expression;
    expression.constant_ = value;
    return expression;
}

AffineExpr AffineExpr::variable(AffineVariable variable) {
    AffineExpr expression;
    expression.coefficients_[variable] = 1;
    return expression;
}

AffineExpr AffineExpr::add(const AffineExpr &other) const {
    if (!valid_ || !other.valid_)
        return invalid();

    AffineExpr result = *this;
    if (!checkedAdd(result.constant_, other.constant_,
                    result.constant_))
        return invalid();

    for (const auto &[variable, coefficient] : other.coefficients_) {
        std::int64_t sum = 0;
        if (!checkedAdd(result.coefficients_[variable], coefficient,
                        sum))
            return invalid();
        result.coefficients_[variable] = sum;
    }
    result.canonicalize();
    return result;
}

AffineExpr AffineExpr::subtract(const AffineExpr &other) const {
    return add(other.scale(-1));
}

AffineExpr AffineExpr::scale(std::int64_t factor) const {
    if (!valid_)
        return invalid();

    AffineExpr result;
    if (!checkedMultiply(constant_, factor, result.constant_))
        return invalid();
    for (const auto &[variable, coefficient] : coefficients_) {
        std::int64_t product = 0;
        if (!checkedMultiply(coefficient, factor, product))
            return invalid();
        if (product)
            result.coefficients_[variable] = product;
    }
    return result;
}

void AffineExpr::canonicalize() {
    for (auto iterator = coefficients_.begin();
         iterator != coefficients_.end();) {
        if (iterator->second == 0)
            iterator = coefficients_.erase(iterator);
        else
            ++iterator;
    }
}

} // namespace hira::polyhedral
