#pragma once

#include "dependenceAnalysis.hpp"

#include <string>
#include <vector>

namespace hira::polyhedral {

enum class DependenceFeasibilityKind {
    Required,
    MayExist,
    ProvenEmpty,
};

enum class DependenceEmptyReason {
    None,
    AffineEqualityConflict,
    DisjointDomains,
    IdentityOrderConflict,
};

struct DependenceFeasibility {
    DependenceId dependence = 0;
    DependenceFeasibilityKind kind =
        DependenceFeasibilityKind::MayExist;
    DependenceEmptyReason reason = DependenceEmptyReason::None;
};

class DependenceFeasibilityResult {
public:
    const std::vector<DependenceFeasibility> &relations() const {
        return relations_;
    }

private:
    friend DependenceFeasibilityResult
    analyzeDependenceFeasibility(const PolyhedralModel &model,
                                 const DependenceSet &dependences);

    std::vector<DependenceFeasibility> relations_;
};

DependenceFeasibilityResult
analyzeDependenceFeasibility(const PolyhedralModel &model,
                             const DependenceSet &dependences);
bool verifyDependenceFeasibility(
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &result,
    std::string &detail);
std::string printDependenceFeasibility(
    const DependenceFeasibilityResult &result);

} // namespace hira::polyhedral
