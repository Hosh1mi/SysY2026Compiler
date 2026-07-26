#pragma once

#include <cstdint>
#include <map>

namespace hira::polyhedral {

enum class AffineVariableKind {
    Dimension,
    Symbol,
};

struct AffineVariable {
    AffineVariableKind kind = AffineVariableKind::Dimension;
    std::uint32_t position = 0;

    bool operator<(const AffineVariable &other) const {
        if (kind != other.kind)
            return kind < other.kind;
        return position < other.position;
    }

    bool operator==(const AffineVariable &other) const {
        return kind == other.kind && position == other.position;
    }
    bool operator!=(const AffineVariable &other) const {
        return !(*this == other);
    }
};

class AffineExpr {
public:
    static AffineExpr invalid();
    static AffineExpr constant(std::int64_t value);
    static AffineExpr variable(AffineVariable variable);

    bool valid() const { return valid_; }
    bool isConstant() const { return valid_ && coefficients_.empty(); }
    std::int64_t constantTerm() const { return constant_; }
    const std::map<AffineVariable, std::int64_t> &coefficients() const {
        return coefficients_;
    }

    AffineExpr add(const AffineExpr &other) const;
    AffineExpr subtract(const AffineExpr &other) const;
    AffineExpr scale(std::int64_t factor) const;

private:
    void canonicalize();

    bool valid_ = true;
    std::int64_t constant_ = 0;
    std::map<AffineVariable, std::int64_t> coefficients_;
};

} // namespace hira::polyhedral
