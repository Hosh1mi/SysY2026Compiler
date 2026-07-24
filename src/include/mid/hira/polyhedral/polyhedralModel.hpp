#pragma once

#include "affineExpr.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hira {

class HiraLoop;
class HiraRegion;
class HiraValue;

namespace polyhedral {

class PolyhedralModelBuilder;
struct PolyhedralBuildResult;

class AffineSpace {
public:
    AffineVariable addDimension(const HiraValue *value);
    AffineVariable addSymbol(const HiraValue *value);

    std::optional<AffineVariable>
    variableFor(const HiraValue *value) const;
    const HiraValue *source(AffineVariable variable) const;

    const std::vector<const HiraValue *> &dimensions() const {
        return dimensions_;
    }
    const std::vector<const HiraValue *> &symbols() const {
        return symbols_;
    }

private:
    std::vector<const HiraValue *> dimensions_;
    std::vector<const HiraValue *> symbols_;
    std::map<const HiraValue *, AffineVariable> variables_;
};

enum class AffineRelation {
    GreaterEqualZero,
    EqualZero,
};

struct AffineConstraint {
    AffineExpr expression;
    AffineRelation relation = AffineRelation::GreaterEqualZero;
};

struct IterationDomain {
    const HiraLoop *loop = nullptr;
    AffineVariable dimension;
    std::vector<AffineVariable> dimensions;
    std::vector<AffineConstraint> constraints;
};

enum class ProofObligationKind {
    NoSignedWrap,
};

struct ProofObligation {
    ProofObligationKind kind = ProofObligationKind::NoSignedWrap;
    const HiraLoop *loop = nullptr;
    AffineVariable dimension;
};

class PolyhedralModel {
public:
    const AffineSpace &space() const { return space_; }
    const std::vector<IterationDomain> &domains() const {
        return domains_;
    }
    const std::vector<ProofObligation> &proofObligations() const {
        return proofObligations_;
    }

private:
    friend class PolyhedralModelBuilder;

    AffineSpace space_;
    std::vector<IterationDomain> domains_;
    std::vector<ProofObligation> proofObligations_;
};

enum class PolyhedralBuildError {
    None,
    NonAffineLowerBound,
    NonAffineUpperBound,
    ConstraintOverflow,
};

struct PolyhedralBuildResult {
    std::unique_ptr<PolyhedralModel> model;
    PolyhedralBuildError error = PolyhedralBuildError::None;
    std::string detail;

    bool succeeded() const {
        return model && error == PolyhedralBuildError::None;
    }
};

const char *polyhedralBuildErrorName(PolyhedralBuildError error);
PolyhedralBuildResult buildPolyhedralModel(const HiraRegion &region);
std::string printPolyhedralModel(const PolyhedralModel &model);

} // namespace polyhedral
} // namespace hira
