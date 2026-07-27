#include "../../../include/mid/hira/polyhedral/vectorizationAnalysis.hpp"

#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/accessStrideAnalysis.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <algorithm>
#include <sstream>

namespace hira::polyhedral {
namespace {

bool containsDimension(const PolyhedralStatement &statement,
                       AffineVariable dimension) {
    return std::find(statement.dimensions.begin(),
                     statement.dimensions.end(),
                     dimension) != statement.dimensions.end();
}

std::optional<AffineVariable> innermostBandDimension(
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

std::size_t iterationDepth(
    const StatementSchedule &statement) {
    std::size_t depth = 0;
    for (const ScheduleComponent &component :
         statement.components)
        depth += component.kind ==
                 ScheduleComponentKind::Iteration;
    return depth;
}

std::vector<AffineVariable> vectorizationDimensions(
    const PolyhedralModel &model,
    const ScheduleCandidate &candidate) {
    std::vector<AffineVariable> dimensions;
    const bool bandAware =
        candidate.kind !=
            ScheduleCandidateKind::Identity &&
        !candidate.originalDimensions.empty();
    const StatementSchedule *deepest = nullptr;
    std::size_t bestDepth = 0;
    for (const StatementSchedule &statement :
         candidate.statements) {
        if (statement.statement >= model.statements().size())
            continue;
        const PolyhedralStatement &polyhedral =
            model.statements()[statement.statement];
        if (bandAware) {
            bool usesBand = false;
            for (AffineVariable dimension :
                 candidate.originalDimensions)
                if (containsDimension(polyhedral, dimension)) {
                    usesBand = true;
                    break;
                }
            if (!usesBand)
                continue;
        }
        const std::size_t depth = iterationDepth(statement);
        if (!deepest || depth > bestDepth) {
            deepest = &statement;
            bestDepth = depth;
        }
    }
    if (deepest) {
        for (auto component = deepest->components.rbegin();
             component != deepest->components.rend();
             ++component)
            if (component->kind ==
                ScheduleComponentKind::Iteration) {
                if (std::find(dimensions.begin(),
                              dimensions.end(),
                              component->dimension) ==
                    dimensions.end())
                    dimensions.push_back(
                        component->dimension);
            }
        return dimensions;
    }
    if (auto fallback = innermostBandDimension(candidate))
        dimensions.push_back(*fallback);
    return dimensions;
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

bool provesEqual(const DependenceRelation &relation,
                 AffineVariable dimension) {
    for (const DirectionComponent &component :
         relation.directionVector)
        if (component.dimension == dimension)
            return component.direction ==
                   DependenceDirection::Equal;
    return false;
}

bool isVectorizableReductionDependence(
    const PolyhedralModel &model,
    const DependenceRelation &relation,
    AffineVariable dimension) {
    if (relation.kind != DependenceKind::RecurrenceCarried ||
        !relation.sourceRecurrence)
        return false;
    const ScalarRecurrence *recurrence = nullptr;
    for (const ScalarRecurrence &candidate :
         model.scalarRecurrences())
        if (candidate.id == *relation.sourceRecurrence) {
            recurrence = &candidate;
            break;
        }
    if (!recurrence || recurrence->dimension != dimension)
        return false;
    auto *update =
        recurrence->yielded
            ? dynamic_cast<const HiraComputeOp *>(
                  recurrence->yielded->definingNode())
            : nullptr;
    if (!update || update->operands().size() != 2)
        return false;
    return update->computeKind() == ComputeKind::Add ||
           update->computeKind() == ComputeKind::FAdd;
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
    case ComputeKind::ICmp:
    case ComputeKind::Select:
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

bool hasNestedChildLoop(const PolyhedralModel &model,
                        AffineVariable dimension) {
    const IterationDomain *domain = findDomain(model, dimension);
    if (!domain)
        return true;
    const std::size_t depth = domain->dimensions.size();
    for (const IterationDomain &other : model.domains()) {
        if (other.dimension == dimension)
            continue;
        if (other.dimensions.size() <= depth)
            continue;
        if (std::find(other.dimensions.begin(),
                      other.dimensions.end(),
                      dimension) != other.dimensions.end())
            return true;
    }
    return false;
}

ScheduleVectorization analyzeVectorizationForDimension(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidate &candidate,
    AffineVariable dimension,
    const target::A53TargetModel &target) {
    ScheduleVectorization vectorization;
    vectorization.schedule = candidate.id;
    vectorization.dimension = dimension;

    const IterationDomain *vectorDomain =
        findDomain(model, dimension);
    if (!vectorDomain || !vectorDomain->loop) {
        vectorization.reason =
            VectorizationReason::MissingInnerDimension;
        return vectorization;
    }
    if (containsConditional(vectorDomain->loop->body())) {
        vectorization.reason =
            VectorizationReason::OpaqueControl;
        return vectorization;
    }
    if (hasNestedChildLoop(model, dimension)) {
        vectorization.reason =
            VectorizationReason::OpaqueControl;
        return vectorization;
    }

    const HiraComputeOp *inductionUpdate = nullptr;
    if (auto control =
            analyzeCanonicalLoopControl(*vectorDomain->loop))
        inductionUpdate = control->inductionUpdate;
    else if (vectorDomain->loop->carriedValues().size() == 1)
        inductionUpdate =
            findInductionUpdate(*vectorDomain->loop);
    for (const PolyhedralStatement &statement :
         model.statements()) {
        if (!containsDimension(statement, dimension))
            continue;
        auto *compute = dynamic_cast<const HiraComputeOp *>(
            statement.node);
        if (compute && compute != inductionUpdate &&
            !isSupportedVectorCompute(*compute)) {
            vectorization.reason =
                VectorizationReason::UnsupportedOperation;
            return vectorization;
        }
    }

    for (std::size_t index = 0;
         index < dependences.relations().size(); ++index) {
        if (feasibility.relations()[index].kind ==
            DependenceFeasibilityKind::ProvenEmpty)
            continue;
        const DependenceRelation &relation =
            dependences.relations()[index];
        if (!relation.sourceStatement || !relation.sinkStatement)
            continue;
        const PolyhedralStatement &source =
            model.statements()[*relation.sourceStatement];
        const PolyhedralStatement &sink =
            model.statements()[*relation.sinkStatement];
        if (containsDimension(source, dimension) &&
            containsDimension(sink, dimension) &&
            !provesEqual(relation, dimension) &&
            !isVectorizableReductionDependence(
                model, relation, dimension))
            vectorization.blockers.push_back(relation.id);
    }
    if (!vectorization.blockers.empty()) {
        vectorization.reason =
            VectorizationReason::LoopCarriedDependence;
        return vectorization;
    }

    bool varying = false;
    bool nonContiguous = false;
    std::uint32_t lanes = target.neonBits;
    for (const AccessRelation &access : model.accesses()) {
        auto stride =
            analyzeLinearAccessStride(model, access, dimension);
        auto elementSize =
            analyzeAccessElementSize(model, access);
        if (!stride || !elementSize) {
            nonContiguous = true;
            break;
        }
        if (*stride == 0)
            continue;
        if (access.kind == MemoryAccessKind::Write) {
            const HiraNode *node =
                model.statements()[access.statement].node;
            auto *store =
                dynamic_cast<const HiraStore *>(node);
            const HiraValue *value =
                store ? store->value() : nullptr;
            const bool invariantFill =
                value &&
                (value->kind() == ValueKind::IntegerConstant ||
                 value->kind() == ValueKind::FloatConstant ||
                 value->kind() == ValueKind::Parameter);
            if (!invariantFill)
                continue;
        }
        varying = true;
        nonContiguous |= *stride != *elementSize;
        lanes = std::min(
            lanes,
            lanesFor(accessElementType(model, access), target));
    }
    if (!varying) {
        vectorization.reason =
            VectorizationReason::NoVaryingAccess;
    } else if (nonContiguous) {
        vectorization.reason =
            VectorizationReason::NonContiguousAccess;
    } else if (lanes <= 1) {
        vectorization.reason =
            VectorizationReason::UnsupportedElementType;
    } else {
        vectorization.kind = VectorizationKind::Vectorizable;
        vectorization.reason = VectorizationReason::None;
        vectorization.lanes = lanes;
    }
    return vectorization;
}

} // namespace

std::vector<AffineVariable> vectorizationCandidateDimensions(
    const PolyhedralModel &model,
    const ScheduleCandidate &candidate) {
    return vectorizationDimensions(model, candidate);
}

VectorizationAnalysisResult analyzeVectorization(
    const PolyhedralModel &model,
    const DependenceSet &dependences,
    const DependenceFeasibilityResult &feasibility,
    const ScheduleCandidateSet &schedules,
    const target::A53TargetModel &target) {
    VectorizationAnalysisResult result;
    for (const ScheduleCandidate &candidate :
         schedules.candidates()) {
        const std::vector<AffineVariable> dimensions =
            vectorizationDimensions(model, candidate);
        if (dimensions.empty()) {
            ScheduleVectorization vectorization;
            vectorization.schedule = candidate.id;
            result.schedules_.push_back(vectorization);
            continue;
        }

        // Vectorization profitability depends on the candidate
        // schedule's innermost dimension, not on any other
        // dimension that remains vectorizable in the source IR.
        result.schedules_.push_back(
            analyzeVectorizationForDimension(
                model, dependences, feasibility, candidate,
                dimensions.front(), target));
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
