#include "../../../include/mid/hira/polyhedral/vectorizationAnalysis.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <algorithm>
#include <sstream>

namespace hira::polyhedral {
namespace {

std::optional<AffineVariable> innermostDimension(
    const ScheduleCandidate &candidate) {
    const ScheduleTreeNode *largest = nullptr;
    for (const ScheduleTreeNode &node :
         candidate.tree.nodes())
        if (node.kind == ScheduleTreeNodeKind::Band &&
            (!largest ||
             node.band.dimensions.size() >
                 largest->band.dimensions.size()))
            largest = &node;
    if (!largest || largest->band.dimensions.empty())
        return std::nullopt;
    return largest->band.dimensions.back();
}

Type *accessElementType(const PolyhedralModel &model,
                        const AccessRelation &access) {
    if (access.statement >= model.statements().size())
        return nullptr;
    const HiraNode *node =
        model.statements()[access.statement].node;
    if (auto *load =
            dynamic_cast<const HiraLoad *>(node))
        return load->results().empty()
                   ? nullptr
                   : load->results().front()->type();
    if (auto *store =
            dynamic_cast<const HiraStore *>(node))
        return store->value()->type();
    return nullptr;
}

std::uint32_t lanesFor(
    Type *type, const target::A53TargetModel &target) {
    if (!type)
        return 1;
    if (type->tid_ == Type::FloatTyID)
        return target.float32Lanes;
    auto *integer = dynamic_cast<IntegerType *>(type);
    if (!integer || integer->num_bits_ != 32)
        return 1;
    return target.neonBits / 32;
}

bool containsDimension(const PolyhedralStatement &statement,
                       AffineVariable dimension) {
    return std::find(statement.dimensions.begin(),
                     statement.dimensions.end(),
                     dimension) != statement.dimensions.end();
}

bool provesEqual(const DependenceRelation &relation,
                 AffineVariable dimension) {
    for (const DirectionComponent &component :
         relation.directionVector)
        if (component.dimension == dimension)
            return component.direction ==
                   DependenceDirection::Equal;
    return false;
}

const IterationDomain *findDomain(
    const PolyhedralModel &model,
    AffineVariable dimension) {
    for (const IterationDomain &domain : model.domains())
        if (domain.dimension == dimension)
            return &domain;
    return nullptr;
}

bool containsConditional(const HiraSequence &sequence) {
    for (const auto &owner : sequence.nodes()) {
        if (dynamic_cast<const HiraIf *>(owner.get()))
            return true;
        if (auto *loop =
                dynamic_cast<const HiraLoop *>(owner.get()))
            if (containsConditional(loop->body()))
                return true;
    }
    return false;
}

bool isSupportedVectorCompute(
    const HiraComputeOp &compute) {
    switch (compute.computeKind()) {
    case ComputeKind::Add:
    case ComputeKind::Sub:
    case ComputeKind::Mul:
    case ComputeKind::FAdd:
    case ComputeKind::FSub:
    case ComputeKind::FMul:
    case ComputeKind::GetElementPtr:
        return true;
    default:
        return false;
    }
}

const char *reasonName(VectorizationReason reason) {
    switch (reason) {
    case VectorizationReason::None:
        return "none";
    case VectorizationReason::MissingInnerDimension:
        return "missing-inner-dimension";
    case VectorizationReason::NoVaryingAccess:
        return "no-varying-access";
    case VectorizationReason::NonContiguousAccess:
        return "non-contiguous-access";
    case VectorizationReason::LoopCarriedDependence:
        return "loop-carried-dependence";
    case VectorizationReason::OpaqueControl:
        return "opaque-control";
    case VectorizationReason::UnsupportedElementType:
        return "unsupported-element-type";
    case VectorizationReason::UnsupportedOperation:
        return "unsupported-operation";
    }
    return "unknown";
}

bool same(const ScheduleVectorization &left,
          const ScheduleVectorization &right) {
    return left.schedule == right.schedule &&
           left.kind == right.kind &&
           left.reason == right.reason &&
           left.dimension == right.dimension &&
           left.lanes == right.lanes &&
           left.blockers == right.blockers;
}

} // namespace

VectorizationAnalysisResult analyzeVectorization(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target) {
    VectorizationAnalysisResult result;
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        ScheduleVectorization vectorization;
        vectorization.schedule = candidate.id;
        vectorization.dimension =
            innermostDimension(candidate);
        if (!vectorization.dimension) {
            result.schedules_.push_back(vectorization);
            continue;
        }

        const IterationDomain *vectorDomain =
            findDomain(model, *vectorization.dimension);
        if (!vectorDomain || !vectorDomain->loop) {
            vectorization.reason =
                VectorizationReason::MissingInnerDimension;
            result.schedules_.push_back(vectorization);
            continue;
        }
        if (containsConditional(
                vectorDomain->loop->body())) {
            vectorization.reason =
                VectorizationReason::OpaqueControl;
            result.schedules_.push_back(vectorization);
            continue;
        }

        const HiraComputeOp *inductionUpdate = nullptr;
        if (auto control =
                analyzeCanonicalLoopControl(
                    *vectorDomain->loop))
            inductionUpdate = control->inductionUpdate;
        bool unsupportedOperation = false;
        for (const PolyhedralStatement &statement :
             model.statements()) {
            if (!containsDimension(
                    statement,
                    *vectorization.dimension))
                continue;
            auto *compute =
                dynamic_cast<const HiraComputeOp *>(
                    statement.node);
            if (compute && compute != inductionUpdate &&
                !isSupportedVectorCompute(*compute)) {
                unsupportedOperation = true;
                break;
            }
        }
        if (unsupportedOperation) {
            vectorization.reason =
                VectorizationReason::UnsupportedOperation;
            result.schedules_.push_back(vectorization);
            continue;
        }

        for (std::size_t index = 0;
             index < dependences.relations().size(); ++index) {
            if (feasibility.relations()[index].kind ==
                DependenceFeasibilityKind::ProvenEmpty)
                continue;
            const DependenceRelation &relation =
                dependences.relations()[index];
            if (!relation.sourceStatement ||
                !relation.sinkStatement)
                continue;
            const PolyhedralStatement &source =
                model.statements()[
                    *relation.sourceStatement];
            const PolyhedralStatement &sink =
                model.statements()[
                    *relation.sinkStatement];
            if (containsDimension(
                    source, *vectorization.dimension) &&
                containsDimension(
                    sink, *vectorization.dimension) &&
                !provesEqual(
                    relation,
                    *vectorization.dimension))
                vectorization.blockers.push_back(
                    relation.id);
        }
        if (!vectorization.blockers.empty()) {
            vectorization.reason =
                VectorizationReason::
                    LoopCarriedDependence;
            result.schedules_.push_back(vectorization);
            continue;
        }

        bool varying = false;
        bool nonContiguous = false;
        std::uint32_t lanes = target.neonBits;
        for (const AccessRelation &access : model.accesses()) {
            auto stride = analyzeLinearAccessStride(
                model, access, *vectorization.dimension);
            auto elementSize =
                analyzeAccessElementSize(model, access);
            if (!stride || !elementSize) {
                nonContiguous = true;
                break;
            }
            if (*stride == 0)
                continue;
            varying = true;
            nonContiguous |= *stride != *elementSize;
            lanes = std::min(
                lanes,
                lanesFor(accessElementType(model, access),
                         target));
        }
        if (!varying) {
            vectorization.reason =
                VectorizationReason::NoVaryingAccess;
        } else if (nonContiguous) {
            vectorization.reason =
                VectorizationReason::NonContiguousAccess;
        } else if (lanes <= 1) {
            vectorization.reason =
                VectorizationReason::
                    UnsupportedElementType;
        } else {
            vectorization.kind =
                VectorizationKind::Vectorizable;
            vectorization.reason =
                VectorizationReason::None;
            vectorization.lanes = lanes;
        }
        result.schedules_.push_back(
            std::move(vectorization));
    }
    return result;
}

bool verifyVectorizationAnalysis(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target,
    const VectorizationAnalysisResult &result,
    std::string &detail) {
    VectorizationAnalysisResult expected =
        analyzeVectorization(
            model, dependences, feasibility,
            schedules, target);
    if (result.schedules().size() !=
        expected.schedules().size()) {
        detail = "incomplete-vectorization-analysis";
        return false;
    }
    for (std::size_t index = 0;
         index < result.schedules().size(); ++index)
        if (!same(result.schedules()[index],
                  expected.schedules()[index])) {
            detail = "invalid-vectorization-analysis";
            return false;
        }
    return true;
}

std::string printVectorizationAnalysis(
    const VectorizationAnalysisResult &result) {
    std::ostringstream out;
    out << "polyhedral.vectorization target=a53 {\n";
    for (const ScheduleVectorization &vectorization :
         result.schedules()) {
        out << "  C" << vectorization.schedule << " = "
            << (vectorization.kind ==
                        VectorizationKind::Vectorizable
                    ? "vectorizable"
                    : "scalar");
        if (vectorization.dimension)
            out << " dimension=d"
                << vectorization.dimension->position;
        out << " lanes=" << vectorization.lanes;
        if (vectorization.reason !=
            VectorizationReason::None)
            out << " reason="
                << reasonName(vectorization.reason);
        if (!vectorization.blockers.empty()) {
            out << " blockers=[";
            for (std::size_t index = 0;
                 index < vectorization.blockers.size();
                 ++index) {
                if (index)
                    out << ", ";
                out << "D"
                    << vectorization.blockers[index];
            }
            out << "]";
        }
        out << "\n";
    }
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
