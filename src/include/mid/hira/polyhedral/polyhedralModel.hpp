#pragma once

#include "affineExpr.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hira {

class HiraLoop;
class HiraNode;
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

using StatementId = std::uint32_t;
using MemoryObjectId = std::uint32_t;

enum class StatementKind {
    Compute,
    Load,
    Store,
    Yield,
};

struct PolyhedralStatement {
    StatementId id = 0;
    StatementKind kind = StatementKind::Compute;
    const HiraNode *node = nullptr;
    std::vector<AffineVariable> dimensions;
    std::vector<AffineConstraint> constraints;
};

struct MemoryObject {
    MemoryObjectId id = 0;
    const HiraValue *base = nullptr;
};

enum class MemoryAliasKind {
    MustAlias,
    MayAlias,
    NoAlias,
};

enum class MemoryAccessKind {
    Read,
    Write,
};

struct AccessRelation {
    StatementId statement = 0;
    MemoryAccessKind kind = MemoryAccessKind::Read;
    MemoryObjectId object = 0;
    std::vector<AffineExpr> subscripts;
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
    const std::vector<PolyhedralStatement> &statements() const {
        return statements_;
    }
    const std::vector<MemoryObject> &memoryObjects() const {
        return memoryObjects_;
    }
    const std::vector<AccessRelation> &accesses() const {
        return accesses_;
    }
    MemoryAliasKind aliasRelation(MemoryObjectId first,
                                  MemoryObjectId second) const;
    const std::vector<ProofObligation> &proofObligations() const {
        return proofObligations_;
    }

private:
    friend class PolyhedralModelBuilder;

    AffineSpace space_;
    std::vector<IterationDomain> domains_;
    std::vector<PolyhedralStatement> statements_;
    std::vector<MemoryObject> memoryObjects_;
    std::vector<AccessRelation> accesses_;
    std::vector<ProofObligation> proofObligations_;
};

enum class PolyhedralBuildError {
    None,
    NonAffineLowerBound,
    NonAffineUpperBound,
    NonAffineCondition,
    NonAffineAccess,
    UnsupportedAddress,
    UnsupportedStatement,
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
