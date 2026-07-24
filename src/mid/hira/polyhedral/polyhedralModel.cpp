#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <cstdint>
#include <set>
#include <sstream>
#include <utility>

namespace hira::polyhedral {
namespace {

bool isInteger(const HiraValue *value) {
    return value && dynamic_cast<IntegerType *>(value->type());
}

const char *variablePrefix(AffineVariableKind kind) {
    return kind == AffineVariableKind::Dimension ? "d" : "s";
}

std::string variableName(AffineVariable variable) {
    return std::string(variablePrefix(variable.kind)) +
           std::to_string(variable.position);
}

std::uint64_t magnitude(std::int64_t value) {
    if (value >= 0)
        return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

std::string printExpression(const AffineExpr &expression) {
    if (!expression.valid())
        return "<invalid>";

    std::ostringstream out;
    bool emitted = false;
    auto emitTerm = [&](std::int64_t coefficient,
                        const std::string &term) {
        if (!coefficient)
            return;
        if (emitted)
            out << (coefficient < 0 ? " - " : " + ");
        else if (coefficient < 0)
            out << "-";

        std::uint64_t absolute = magnitude(coefficient);
        if (absolute != 1 || term.empty())
            out << absolute;
        if (!term.empty()) {
            if (absolute != 1)
                out << "*";
            out << term;
        }
        emitted = true;
    };

    for (const auto &[variable, coefficient] :
         expression.coefficients())
        emitTerm(coefficient, variableName(variable));
    emitTerm(expression.constantTerm(), {});
    if (!emitted)
        out << "0";
    return out.str();
}

} // namespace

class PolyhedralModelBuilder {
public:
    explicit PolyhedralModelBuilder(const HiraRegion &region)
        : region_(region), model_(std::make_unique<PolyhedralModel>()) {}

    PolyhedralBuildResult run() {
        std::vector<AffineVariable> dimensions;
        std::vector<AffineConstraint> constraints;
        if (!visitSequence(region_.rootSequence(), dimensions,
                           constraints))
            return std::move(failure_);

        PolyhedralBuildResult result;
        result.model = std::move(model_);
        return result;
    }

private:
    bool reject(PolyhedralBuildError error, std::string detail) {
        failure_.error = error;
        failure_.detail = std::move(detail);
        return false;
    }

    AffineExpr analyze(const HiraValue *value) {
        if (!value || !isInteger(value))
            return AffineExpr::invalid();

        auto cached = expressions_.find(value);
        if (cached != expressions_.end())
            return cached->second;
        if (!activeExpressions_.insert(value).second)
            return AffineExpr::invalid();

        AffineExpr expression = AffineExpr::invalid();
        if (value->kind() == ValueKind::IntegerConstant) {
            expression = AffineExpr::constant(value->integerValue());
        } else if (value->kind() == ValueKind::Parameter) {
            expression = AffineExpr::variable(
                model_->space_.addSymbol(value));
        } else if (auto variable =
                       model_->space_.variableFor(value)) {
            expression = AffineExpr::variable(*variable);
        } else if (auto *compute =
                       dynamic_cast<const HiraComputeOp *>(
                           value->definingNode())) {
            expression = analyzeCompute(*compute, value);
        }

        activeExpressions_.erase(value);
        expressions_[value] = expression;
        return expression;
    }

    AffineExpr analyzeCompute(const HiraComputeOp &compute,
                              const HiraValue *result) {
        if (compute.results().size() != 1 ||
            compute.results().front() != result)
            return AffineExpr::invalid();

        if (compute.computeKind() == ComputeKind::Add ||
            compute.computeKind() == ComputeKind::Sub) {
            if (compute.operands().size() != 2)
                return AffineExpr::invalid();
            AffineExpr left = analyze(compute.operands()[0]);
            AffineExpr right = analyze(compute.operands()[1]);
            return compute.computeKind() == ComputeKind::Add
                       ? left.add(right)
                       : left.subtract(right);
        }

        if (compute.computeKind() != ComputeKind::Mul ||
            compute.operands().size() != 2)
            return AffineExpr::invalid();

        AffineExpr left = analyze(compute.operands()[0]);
        AffineExpr right = analyze(compute.operands()[1]);
        if (left.isConstant())
            return right.scale(left.constantTerm());
        if (right.isConstant())
            return left.scale(right.constantTerm());
        return AffineExpr::invalid();
    }

    bool visitLoop(
        const HiraLoop &loop,
        const std::vector<AffineVariable> &outerDimensions,
        const std::vector<AffineConstraint> &outerConstraints) {
        AffineExpr lower = analyze(loop.lowerBound());
        if (!lower.valid())
            return reject(PolyhedralBuildError::NonAffineLowerBound,
                          "loop-h" +
                              std::to_string(loop.induction()->id()));
        AffineExpr upper = analyze(loop.upperBound());
        if (!upper.valid())
            return reject(PolyhedralBuildError::NonAffineUpperBound,
                          "loop-h" +
                              std::to_string(loop.induction()->id()));

        AffineVariable dimension =
            model_->space_.addDimension(loop.induction());
        AffineExpr induction = AffineExpr::variable(dimension);
        AffineExpr lowerConstraint = induction.subtract(lower);
        AffineExpr upperConstraint =
            upper.subtract(induction).add(AffineExpr::constant(-1));
        if (!lowerConstraint.valid() || !upperConstraint.valid())
            return reject(PolyhedralBuildError::ConstraintOverflow,
                          "loop-h" +
                              std::to_string(loop.induction()->id()));

        IterationDomain domain;
        domain.loop = &loop;
        domain.dimension = dimension;
        domain.dimensions = outerDimensions;
        domain.dimensions.push_back(dimension);
        domain.constraints = outerConstraints;
        domain.constraints.push_back(
            {std::move(lowerConstraint),
             AffineRelation::GreaterEqualZero});
        domain.constraints.push_back(
            {std::move(upperConstraint),
             AffineRelation::GreaterEqualZero});
        model_->domains_.push_back(domain);
        model_->proofObligations_.push_back(
            {ProofObligationKind::NoSignedWrap, &loop, dimension});

        return visitSequence(loop.body(), domain.dimensions,
                             domain.constraints);
    }

    bool visitSequence(
        const HiraSequence &sequence,
        const std::vector<AffineVariable> &dimensions,
        const std::vector<AffineConstraint> &constraints) {
        for (const auto &node : sequence.nodes()) {
            if (auto *loop =
                    dynamic_cast<const HiraLoop *>(node.get())) {
                if (!visitLoop(*loop, dimensions, constraints))
                    return false;
                continue;
            }
            if (auto *condition =
                    dynamic_cast<const HiraIf *>(node.get())) {
                if (!visitSequence(condition->thenSequence(),
                                   dimensions, constraints) ||
                    !visitSequence(condition->elseSequence(),
                                   dimensions, constraints))
                    return false;
            }
        }
        return true;
    }

    const HiraRegion &region_;
    std::unique_ptr<PolyhedralModel> model_;
    std::map<const HiraValue *, AffineExpr> expressions_;
    std::set<const HiraValue *> activeExpressions_;
    PolyhedralBuildResult failure_;
};

namespace {

void printVariableList(std::ostringstream &out, const char *label,
                       AffineVariableKind kind,
                       const std::vector<const HiraValue *> &values) {
    out << "  " << label << "(";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index)
            out << ", ";
        out << variablePrefix(kind) << index << " = %h"
            << values[index]->id();
    }
    out << ")\n";
}

} // namespace

AffineVariable AffineSpace::addDimension(const HiraValue *value) {
    auto existing = variables_.find(value);
    if (existing != variables_.end())
        return existing->second;
    AffineVariable variable{
        AffineVariableKind::Dimension,
        static_cast<std::uint32_t>(dimensions_.size())};
    dimensions_.push_back(value);
    variables_[value] = variable;
    return variable;
}

AffineVariable AffineSpace::addSymbol(const HiraValue *value) {
    auto existing = variables_.find(value);
    if (existing != variables_.end())
        return existing->second;
    AffineVariable variable{
        AffineVariableKind::Symbol,
        static_cast<std::uint32_t>(symbols_.size())};
    symbols_.push_back(value);
    variables_[value] = variable;
    return variable;
}

std::optional<AffineVariable>
AffineSpace::variableFor(const HiraValue *value) const {
    auto iterator = variables_.find(value);
    if (iterator == variables_.end())
        return std::nullopt;
    return iterator->second;
}

const HiraValue *AffineSpace::source(AffineVariable variable) const {
    const auto &values =
        variable.kind == AffineVariableKind::Dimension
            ? dimensions_
            : symbols_;
    if (variable.position >= values.size())
        return nullptr;
    return values[variable.position];
}

const char *polyhedralBuildErrorName(PolyhedralBuildError error) {
    switch (error) {
    case PolyhedralBuildError::None:
        return "none";
    case PolyhedralBuildError::NonAffineLowerBound:
        return "non-affine-lower-bound";
    case PolyhedralBuildError::NonAffineUpperBound:
        return "non-affine-upper-bound";
    case PolyhedralBuildError::ConstraintOverflow:
        return "constraint-overflow";
    }
    return "unknown";
}

PolyhedralBuildResult
buildPolyhedralModel(const HiraRegion &region) {
    return PolyhedralModelBuilder(region).run();
}

std::string printPolyhedralModel(const PolyhedralModel &model) {
    std::ostringstream out;
    out << "polyhedral.model {\n";
    printVariableList(out, "dimensions",
                      AffineVariableKind::Dimension,
                      model.space().dimensions());
    printVariableList(out, "symbols", AffineVariableKind::Symbol,
                      model.space().symbols());

    for (const IterationDomain &domain : model.domains()) {
        out << "  domain " << variableName(domain.dimension) << "[";
        for (std::size_t index = 0;
             index < domain.dimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << variableName(domain.dimensions[index]);
        }
        out << "] {\n";
        for (const AffineConstraint &constraint :
             domain.constraints) {
            out << "    " << printExpression(constraint.expression)
                << (constraint.relation ==
                            AffineRelation::GreaterEqualZero
                        ? " >= 0"
                        : " = 0")
                << "\n";
        }
        out << "  }\n";
    }

    out << "  obligations(";
    for (std::size_t index = 0;
         index < model.proofObligations().size(); ++index) {
        if (index)
            out << ", ";
        const ProofObligation &obligation =
            model.proofObligations()[index];
        switch (obligation.kind) {
        case ProofObligationKind::NoSignedWrap:
            out << "no_signed_wrap("
                << variableName(obligation.dimension) << ")";
            break;
        }
    }
    out << ")\n";
    out << "}\n";
    return out.str();
}

} // namespace hira::polyhedral
