#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"

#include "../../../include/mid/analysis/argumentAliasAnalysis.hpp"
#include "../../../include/mid/analysis/loopInfo.hpp"
#include "../../../include/mid/hira/analysis/loopStructureAnalysis.hpp"
#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/ir/instruction.hpp"
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

const char *statementKindName(StatementKind kind) {
    switch (kind) {
    case StatementKind::Compute:
        return "compute";
    case StatementKind::Load:
        return "load";
    case StatementKind::Store:
        return "store";
    case StatementKind::Yield:
        return "yield";
    }
    return "unknown";
}

const char *aliasKindName(MemoryAliasKind kind) {
    switch (kind) {
    case MemoryAliasKind::MustAlias:
        return "must_alias";
    case MemoryAliasKind::MayAlias:
        return "may_alias";
    case MemoryAliasKind::NoAlias:
        return "no_alias";
    }
    return "unknown";
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

void printSchedule(std::ostringstream &out,
                   const std::vector<ScheduleComponent> &schedule) {
    out << "[";
    for (std::size_t index = 0; index < schedule.size(); ++index) {
        if (index)
            out << ", ";
        const ScheduleComponent &component = schedule[index];
        switch (component.kind) {
        case ScheduleComponentKind::SequencePosition:
            out << "seq" << component.position;
            break;
        case ScheduleComponentKind::Iteration:
            out << variableName(component.dimension);
            break;
        case ScheduleComponentKind::Branch:
            out << (component.position == 0 ? "then" : "else");
            break;
        }
    }
    out << "]";
}

void printScalarSource(std::ostringstream &out,
                       const ScalarValueSource &source) {
    switch (source.kind) {
    case ScalarSourceKind::External:
        out << "external";
        break;
    case ScalarSourceKind::Statement:
        out << "S" << source.source;
        break;
    case ScalarSourceKind::RecurrenceIteration:
        out << "R" << source.source << ".iter";
        break;
    case ScalarSourceKind::RecurrenceResult:
        out << "R" << source.source << ".result";
        break;
    case ScalarSourceKind::Dimension:
        out << "d" << source.source;
        break;
    }
}

} // namespace

class PolyhedralModelBuilder {
public:
    PolyhedralModelBuilder(
        const HiraRegion &region,
        const ArgumentAliasAnalysis *aliasAnalysis)
        : region_(region), aliasAnalysis_(aliasAnalysis),
          model_(std::make_unique<PolyhedralModel>()) {}

    PolyhedralBuildResult run() {
        std::vector<AffineVariable> dimensions;
        std::vector<AffineConstraint> constraints;
        std::vector<ScheduleComponent> schedule;
        if (!visitSequence(region_.rootSequence(), dimensions,
                           constraints, schedule,
                           DomainPrecision::Exact) ||
            !buildScalarRelations())
            return std::move(failure_);
        classifyMemoryAliases();

        PolyhedralBuildResult result;
        result.model = std::move(model_);
        return result;
    }

private:
    struct RecurrenceValue {
        ScalarRecurrenceId recurrence = 0;
        RecurrenceValueKind kind =
            RecurrenceValueKind::Iteration;
    };

    bool reject(PolyhedralBuildError error, std::string detail) {
        failure_.error = error;
        failure_.detail = std::move(detail);
        return false;
    }

    std::string statementDetail(StatementId statement) const {
        return "S" + std::to_string(statement);
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

    enum class ConditionConstraintStatus {
        Exact,
        Opaque,
        Error,
    };

    ConditionConstraintStatus buildConditionConstraint(
        const HiraValue *condition, bool truth,
        AffineConstraint &constraint) {
        auto *comparison =
            condition
                ? dynamic_cast<const HiraComputeOp *>(
                      condition->definingNode())
                : nullptr;
        if (!comparison ||
            comparison->computeKind() != ComputeKind::ICmp ||
            comparison->operands().size() != 2)
            return ConditionConstraintStatus::Opaque;

        AffineExpr left = analyze(comparison->operands()[0]);
        AffineExpr right = analyze(comparison->operands()[1]);
        if (!left.valid() || !right.valid())
            return ConditionConstraintStatus::Opaque;

        AffineExpr expression = AffineExpr::invalid();
        AffineRelation relation = AffineRelation::GreaterEqualZero;
        auto predicate =
            static_cast<ICmpInst::ICmpOp>(comparison->predicate());
        switch (predicate) {
        case ICmpInst::ICMP_SLT:
            expression =
                truth ? right.subtract(left).add(
                            AffineExpr::constant(-1))
                      : left.subtract(right);
            break;
        case ICmpInst::ICMP_SLE:
            expression =
                truth ? right.subtract(left)
                      : left.subtract(right).add(
                            AffineExpr::constant(-1));
            break;
        case ICmpInst::ICMP_SGT:
            expression =
                truth ? left.subtract(right).add(
                            AffineExpr::constant(-1))
                      : right.subtract(left);
            break;
        case ICmpInst::ICMP_SGE:
            expression =
                truth ? left.subtract(right)
                      : right.subtract(left).add(
                            AffineExpr::constant(-1));
            break;
        case ICmpInst::ICMP_EQ:
            if (!truth)
                return ConditionConstraintStatus::Opaque;
            expression = left.subtract(right);
            relation = AffineRelation::EqualZero;
            break;
        case ICmpInst::ICMP_NE:
            if (truth)
                return ConditionConstraintStatus::Opaque;
            expression = left.subtract(right);
            relation = AffineRelation::EqualZero;
            break;
        default:
            return ConditionConstraintStatus::Opaque;
        }

        if (!expression.valid()) {
            reject(PolyhedralBuildError::ConstraintOverflow,
                   "condition-constraint");
            return ConditionConstraintStatus::Error;
        }
        constraint = {std::move(expression), relation};
        return ConditionConstraintStatus::Exact;
    }

    bool resolveAddress(const HiraValue *address,
                        const HiraValue *&base,
                        std::vector<AffineExpr> &subscripts) {
        if (!address)
            return reject(PolyhedralBuildError::UnsupportedAddress,
                          "null-address");

        auto *addressCompute =
            dynamic_cast<const HiraComputeOp *>(
                address->definingNode());
        if (!addressCompute) {
            if (address->kind() != ValueKind::Parameter ||
                !dynamic_cast<PointerType *>(address->type()))
                return reject(
                    PolyhedralBuildError::UnsupportedAddress,
                    "address-without-base");
            base = address;
            return true;
        }

        if (addressCompute->computeKind() !=
                ComputeKind::GetElementPtr ||
            addressCompute->operands().size() < 2)
            return reject(PolyhedralBuildError::UnsupportedAddress,
                          "unsupported-address-operation");

        const HiraValue *candidateBase =
            addressCompute->operands().front();
        if (!candidateBase ||
            !dynamic_cast<PointerType *>(candidateBase->type()))
            return reject(PolyhedralBuildError::UnsupportedAddress,
                          "gep-without-pointer-base");

        // Frontend lowering may split one source-level multidimensional
        // subscript into a chain of GEPs.  Preserve every index while
        // recovering the original memory object so linear stride and
        // dependence reasoning see the complete affine address.
        if (!resolveAddress(candidateBase, base, subscripts))
            return false;
        for (std::size_t index = 1;
             index < addressCompute->operands().size(); ++index) {
            AffineExpr subscript =
                analyze(addressCompute->operands()[index]);
            if (!subscript.valid())
                return reject(PolyhedralBuildError::NonAffineAccess,
                              "non-affine-gep-index");
            subscripts.push_back(std::move(subscript));
        }
        return true;
    }

    MemoryObjectId memoryObject(const HiraValue *base) {
        auto existing = memoryObjects_.find(base);
        if (existing != memoryObjects_.end())
            return existing->second;
        MemoryObjectId id = static_cast<MemoryObjectId>(
            model_->memoryObjects_.size());
        bool taskPrivate = false;
        ::Value *source =
            region_.sourceMapping().sourceValue(base);
        source = ArgumentAliasAnalysis::underlyingObject(source);
        auto *alloca = dynamic_cast<AllocaInst *>(source);
        Loop *sourceLoop = region_.sourceLoop();
        if (alloca && alloca->isLoopExpansionScratch() &&
            sourceLoop) {
            taskPrivate = true;
            for (const Use &use : alloca->use_list_) {
                auto *user =
                    dynamic_cast<Instruction *>(use.val_);
                if (!user || !user->parent_ ||
                    !sourceLoop->isInLoop(user->parent_)) {
                    taskPrivate = false;
                    break;
                }
            }
        }
        model_->memoryObjects_.push_back(
            {id, base, taskPrivate});
        memoryObjects_[base] = id;
        return id;
    }

    bool addAccess(StatementId statement, MemoryAccessKind kind,
                   const HiraValue *address) {
        const HiraValue *base = nullptr;
        std::vector<AffineExpr> subscripts;
        if (!resolveAddress(address, base, subscripts)) {
            if (failure_.detail.empty())
                failure_.detail = statementDetail(statement);
            else
                failure_.detail =
                    statementDetail(statement) + ":" +
                    failure_.detail;
            return false;
        }
        model_->accesses_.push_back(
            {statement, kind, memoryObject(base),
             std::move(subscripts)});
        return true;
    }

    bool addStatement(
        const HiraNode &node,
        const std::vector<AffineVariable> &dimensions,
        const std::vector<AffineConstraint> &constraints,
        const std::vector<ScheduleComponent> &identitySchedule,
        DomainPrecision domainPrecision) {
        StatementKind kind = StatementKind::Compute;
        if (dynamic_cast<const HiraLoad *>(&node))
            kind = StatementKind::Load;
        else if (dynamic_cast<const HiraStore *>(&node))
            kind = StatementKind::Store;
        else if (dynamic_cast<const HiraYield *>(&node))
            kind = StatementKind::Yield;
        else if (!dynamic_cast<const HiraComputeOp *>(&node) &&
                 !dynamic_cast<const HiraIf *>(&node))
            return reject(
                PolyhedralBuildError::UnsupportedStatement,
                "node-kind-" +
                    std::to_string(static_cast<int>(node.kind())));

        StatementId id =
            static_cast<StatementId>(model_->statements_.size());
        model_->statements_.push_back(
            {id, kind, &node, dimensions, constraints,
             identitySchedule, domainPrecision});
        nodeStatements_[&node] = id;
        if (auto *load = dynamic_cast<const HiraLoad *>(&node))
            return addAccess(id, MemoryAccessKind::Read,
                             load->address());
        if (auto *store = dynamic_cast<const HiraStore *>(&node))
            return addAccess(id, MemoryAccessKind::Write,
                             store->address());
        return true;
    }

    std::vector<AffineVariable> commonDimensions(
        const std::vector<AffineVariable> &first,
        const std::vector<AffineVariable> &second) const {
        std::vector<AffineVariable> common;
        for (AffineVariable variable : first)
            for (AffineVariable candidate : second)
                if (variable == candidate) {
                    common.push_back(variable);
                    break;
                }
        return common;
    }

    bool buildScalarRecurrences() {
        for (const IterationDomain &domain : model_->domains_) {
            const HiraLoop &loop = *domain.loop;
            if (loop.carriedValues().empty())
                continue;
            if (loop.body().nodes().empty())
                return reject(
                    PolyhedralBuildError::InvalidScalarRecurrence,
                    "missing-yield");
            auto *yield = dynamic_cast<const HiraYield *>(
                loop.body().nodes().back().get());
            auto yieldStatement =
                yield ? nodeStatements_.find(yield)
                      : nodeStatements_.end();
            if (!yield ||
                yieldStatement == nodeStatements_.end())
                return reject(
                    PolyhedralBuildError::InvalidScalarRecurrence,
                    "unmapped-yield");

            for (const HiraLoop::CarriedBinding &binding :
                 loop.carriedValues()) {
                if (!binding.initial || !binding.iteration ||
                    !binding.yielded || !binding.result)
                    return reject(
                        PolyhedralBuildError::
                            InvalidScalarRecurrence,
                        "incomplete-carried-binding");
                ScalarRecurrenceId id =
                    static_cast<ScalarRecurrenceId>(
                        model_->scalarRecurrences_.size());
                model_->scalarRecurrences_.push_back(
                    {id, &loop, domain.dimension,
                     domain.dimensions, binding.initial,
                     binding.iteration, binding.yielded,
                     binding.result, yieldStatement->second});
                recurrenceValues_[binding.iteration] = {
                    id, RecurrenceValueKind::Iteration};
                recurrenceValues_[binding.result] = {
                    id, RecurrenceValueKind::Result};
            }
        }
        return true;
    }

    bool buildRecurrenceInitialSources() {
        for (ScalarRecurrence &recurrence :
             model_->scalarRecurrences_) {
            const HiraValue *initial = recurrence.initial;
            const HiraNode *definition =
                initial ? initial->definingNode() : nullptr;
            auto statement = nodeStatements_.find(definition);
            if (statement != nodeStatements_.end()) {
                recurrence.initialSource = {
                    ScalarSourceKind::Statement,
                    statement->second,
                    commonDimensions(
                        model_->statements_[statement->second]
                            .dimensions,
                        recurrence.dimensions)};
                continue;
            }

            auto carried = recurrenceValues_.find(initial);
            if (carried != recurrenceValues_.end()) {
                const ScalarRecurrence &source =
                    model_->scalarRecurrences_[
                        carried->second.recurrence];
                recurrence.initialSource = {
                    carried->second.kind ==
                            RecurrenceValueKind::Iteration
                        ? ScalarSourceKind::RecurrenceIteration
                        : ScalarSourceKind::RecurrenceResult,
                    carried->second.recurrence,
                    commonDimensions(source.dimensions,
                                     recurrence.dimensions)};
                continue;
            }

            auto variable =
                model_->space_.variableFor(initial);
            if (variable &&
                variable->kind ==
                    AffineVariableKind::Dimension) {
                recurrence.initialSource = {
                    ScalarSourceKind::Dimension,
                    variable->position, {*variable}};
                continue;
            }

            if (initial &&
                (initial->kind() == ValueKind::Parameter ||
                 initial->kind() ==
                     ValueKind::IntegerConstant ||
                 initial->kind() == ValueKind::FloatConstant)) {
                recurrence.initialSource = {};
                continue;
            }
            return reject(
                PolyhedralBuildError::InvalidScalarRecurrence,
                "unresolved-initial-source");
        }
        return true;
    }

    bool buildScalarRelations() {
        if (!buildScalarRecurrences() ||
            !buildRecurrenceInitialSources())
            return false;

        for (const PolyhedralStatement &consumer :
             model_->statements_) {
            const auto &operands = consumer.node->operands();
            for (std::size_t operandIndex = 0;
                 operandIndex < operands.size(); ++operandIndex) {
                const HiraValue *value = operands[operandIndex];
                const HiraNode *definition =
                    value ? value->definingNode() : nullptr;
                auto producer = nodeStatements_.find(definition);
                if (producer != nodeStatements_.end()) {
                    const PolyhedralStatement &producerStatement =
                        model_->statements_[producer->second];
                    model_->scalarFlows_.push_back(
                        {producer->second, consumer.id,
                         static_cast<std::uint32_t>(operandIndex),
                         value,
                         commonDimensions(
                             producerStatement.dimensions,
                             consumer.dimensions)});
                    continue;
                }

                auto recurrence = recurrenceValues_.find(value);
                if (recurrence == recurrenceValues_.end())
                    continue;
                const ScalarRecurrence &source =
                    model_->scalarRecurrences_[
                        recurrence->second.recurrence];
                model_->recurrenceUses_.push_back(
                    {recurrence->second.recurrence,
                     recurrence->second.kind, consumer.id,
                     static_cast<std::uint32_t>(operandIndex),
                     commonDimensions(source.dimensions,
                                      consumer.dimensions)});
            }
        }
        return true;
    }

    void classifyMemoryAliases() {
        for (MemoryObjectId first = 0;
             first < model_->memoryObjects_.size(); ++first) {
            for (MemoryObjectId second = first;
                 second < model_->memoryObjects_.size(); ++second) {
                MemoryAliasKind kind =
                    first == second
                        ? MemoryAliasKind::MustAlias
                        : MemoryAliasKind::MayAlias;
                if (first != second && aliasAnalysis_) {
                    ::Value *firstSource =
                        region_.sourceMapping().sourceValue(
                            model_->memoryObjects_[first].base);
                    ::Value *secondSource =
                        region_.sourceMapping().sourceValue(
                            model_->memoryObjects_[second].base);
                    firstSource =
                        ArgumentAliasAnalysis::underlyingObject(
                            firstSource);
                    secondSource =
                        ArgumentAliasAnalysis::underlyingObject(
                            secondSource);
                    if (aliasAnalysis_->noAlias(firstSource,
                                                secondSource))
                        kind = MemoryAliasKind::NoAlias;
                }
                model_->aliasRelations_.push_back(
                    {first, second, kind});
            }
        }
    }

    bool visitLoop(
        const HiraLoop &loop,
        const std::vector<AffineVariable> &outerDimensions,
        const std::vector<AffineConstraint> &outerConstraints,
        const std::vector<ScheduleComponent> &outerSchedule,
        DomainPrecision domainPrecision) {
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
        domain.precision = domainPrecision;
        model_->domains_.push_back(domain);
        model_->proofObligations_.push_back(
            {ProofObligationKind::NoSignedWrap, &loop, dimension});

        std::vector<ScheduleComponent> loopSchedule = outerSchedule;
        loopSchedule.push_back(
            {ScheduleComponentKind::Iteration, 0, dimension});
        // The induction update and yield implement the iteration
        // domain itself. They are not independently schedulable
        // payload statements.
        if (auto control =
                analyzeCanonicalLoopControl(loop)) {
            structuralControl_.insert(
                control->inductionUpdate);
            structuralControl_.insert(control->yield);
        }
        return visitSequence(loop.body(), domain.dimensions,
                             domain.constraints, loopSchedule,
                             domainPrecision);
    }

    bool visitSequence(
        const HiraSequence &sequence,
        const std::vector<AffineVariable> &dimensions,
        const std::vector<AffineConstraint> &constraints,
        const std::vector<ScheduleComponent> &schedulePrefix,
        DomainPrecision domainPrecision) {
        const auto &nodes = sequence.nodes();
        for (std::size_t nodeIndex = 0;
             nodeIndex < nodes.size(); ++nodeIndex) {
            const auto &node = nodes[nodeIndex];
            if (structuralControl_.count(node.get()))
                continue;
            std::vector<ScheduleComponent> nodeSchedule =
                schedulePrefix;
            nodeSchedule.push_back(
                {ScheduleComponentKind::SequencePosition,
                 static_cast<std::uint32_t>(nodeIndex), {}});
            if (auto *loop =
                    dynamic_cast<const HiraLoop *>(node.get())) {
                if (!visitLoop(*loop, dimensions, constraints,
                               nodeSchedule, domainPrecision))
                    return false;
                continue;
            }
            if (auto *condition =
                    dynamic_cast<const HiraIf *>(node.get())) {
                if (!condition->thenSequence().nodes().empty()) {
                    std::vector<AffineConstraint> thenConstraints =
                        constraints;
                    AffineConstraint branchConstraint;
                    ConditionConstraintStatus status =
                        buildConditionConstraint(
                            condition->condition(), true,
                            branchConstraint);
                    if (status ==
                        ConditionConstraintStatus::Error)
                        return false;
                    DomainPrecision thenPrecision =
                        domainPrecision;
                    if (status ==
                        ConditionConstraintStatus::Exact)
                        thenConstraints.push_back(
                            std::move(branchConstraint));
                    else
                        thenPrecision =
                            DomainPrecision::
                                OpaqueGuardOverapproximation;
                    std::vector<ScheduleComponent> branchSchedule =
                        nodeSchedule;
                    branchSchedule.push_back(
                        {ScheduleComponentKind::Branch, 0, {}});
                    if (!visitSequence(condition->thenSequence(),
                                       dimensions,
                                       thenConstraints,
                                       branchSchedule,
                                       thenPrecision))
                        return false;
                }
                if (!condition->elseSequence().nodes().empty()) {
                    std::vector<AffineConstraint> elseConstraints =
                        constraints;
                    AffineConstraint branchConstraint;
                    ConditionConstraintStatus status =
                        buildConditionConstraint(
                            condition->condition(), false,
                            branchConstraint);
                    if (status ==
                        ConditionConstraintStatus::Error)
                        return false;
                    DomainPrecision elsePrecision =
                        domainPrecision;
                    if (status ==
                        ConditionConstraintStatus::Exact)
                        elseConstraints.push_back(
                            std::move(branchConstraint));
                    else
                        elsePrecision =
                            DomainPrecision::
                                OpaqueGuardOverapproximation;
                    std::vector<ScheduleComponent> branchSchedule =
                        nodeSchedule;
                    branchSchedule.push_back(
                        {ScheduleComponentKind::Branch, 1, {}});
                    if (!visitSequence(condition->elseSequence(),
                                       dimensions,
                                       elseConstraints,
                                       branchSchedule,
                                       elsePrecision))
                        return false;
                }
                if (!condition->results().empty()) {
                    if (!addStatement(
                            *condition, dimensions, constraints,
                            nodeSchedule, domainPrecision))
                        return false;
                }
                continue;
            }
            if (!addStatement(*node, dimensions, constraints,
                              nodeSchedule, domainPrecision))
                return false;
        }
        return true;
    }

    const HiraRegion &region_;
    const ArgumentAliasAnalysis *aliasAnalysis_;
    std::unique_ptr<PolyhedralModel> model_;
    std::map<const HiraValue *, AffineExpr> expressions_;
    std::map<const HiraValue *, MemoryObjectId> memoryObjects_;
    std::map<const HiraNode *, StatementId> nodeStatements_;
    std::map<const HiraValue *, RecurrenceValue>
        recurrenceValues_;
    std::set<const HiraNode *> structuralControl_;
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
    case PolyhedralBuildError::NonAffineCondition:
        return "non-affine-condition";
    case PolyhedralBuildError::NonAffineAccess:
        return "non-affine-access";
    case PolyhedralBuildError::UnsupportedAddress:
        return "unsupported-address";
    case PolyhedralBuildError::UnsupportedStatement:
        return "unsupported-statement";
    case PolyhedralBuildError::InvalidScalarRecurrence:
        return "invalid-scalar-recurrence";
    case PolyhedralBuildError::ConstraintOverflow:
        return "constraint-overflow";
    }
    return "unknown";
}

MemoryAliasKind
PolyhedralModel::aliasRelation(MemoryObjectId first,
                               MemoryObjectId second) const {
    if (first > second)
        std::swap(first, second);
    for (const MemoryAliasRelation &relation : aliasRelations_)
        if (relation.first == first &&
            relation.second == second)
            return relation.kind;
    return MemoryAliasKind::MayAlias;
}

PolyhedralBuildResult
buildPolyhedralModel(const HiraRegion &region) {
    return buildPolyhedralModel(region, nullptr);
}

PolyhedralBuildResult
buildPolyhedralModel(
    const HiraRegion &region,
    const ArgumentAliasAnalysis *aliasAnalysis) {
    return PolyhedralModelBuilder(region, aliasAnalysis).run();
}

std::string printPolyhedralModel(const PolyhedralModel &model) {
    std::ostringstream out;
    out << "polyhedral.model {\n";
    printVariableList(out, "dimensions",
                      AffineVariableKind::Dimension,
                      model.space().dimensions());
    printVariableList(out, "symbols", AffineVariableKind::Symbol,
                      model.space().symbols());
    out << "  memory_objects(";
    for (std::size_t index = 0;
         index < model.memoryObjects().size(); ++index) {
        if (index)
            out << ", ";
        out << "M" << index << " = %h"
            << model.memoryObjects()[index].base->id();
        if (model.memoryObjects()[index].taskPrivate)
            out << " task_private";
    }
    out << ")\n";
    for (const MemoryAliasRelation &relation :
         model.aliasRelations())
        out << "  alias M" << relation.first << ", M"
            << relation.second << " = "
            << aliasKindName(relation.kind) << "\n";

    for (const IterationDomain &domain : model.domains()) {
        out << "  domain " << variableName(domain.dimension) << "[";
        for (std::size_t index = 0;
             index < domain.dimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << variableName(domain.dimensions[index]);
        }
        out << "]";
        if (domain.precision ==
            DomainPrecision::OpaqueGuardOverapproximation)
            out << " overapproximated";
        out << " {\n";
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

    for (const PolyhedralStatement &statement :
         model.statements()) {
        out << "  statement S" << statement.id << "[";
        for (std::size_t index = 0;
             index < statement.dimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << variableName(statement.dimensions[index]);
        }
        out << "] " << statementKindName(statement.kind)
            << " schedule=";
        printSchedule(out, statement.identitySchedule);
        if (statement.domainPrecision ==
            DomainPrecision::OpaqueGuardOverapproximation)
            out << " domain=opaque-guard-overapproximation";
        out << " {\n";
        for (const AffineConstraint &constraint :
             statement.constraints)
            out << "    " << printExpression(constraint.expression)
                << (constraint.relation ==
                            AffineRelation::GreaterEqualZero
                        ? " >= 0"
                        : " = 0")
                << "\n";
        out << "  }\n";
    }

    for (const AccessRelation &access : model.accesses()) {
        out << "  access S" << access.statement << " "
            << (access.kind == MemoryAccessKind::Read
                    ? "read "
                    : "write ")
            << "M" << access.object << "[";
        for (std::size_t index = 0;
             index < access.subscripts.size(); ++index) {
            if (index)
                out << ", ";
            out << printExpression(access.subscripts[index]);
        }
        out << "]\n";
    }

    for (const ScalarFlowRelation &flow : model.scalarFlows()) {
        out << "  scalar_flow S" << flow.producer << " -> S"
            << flow.consumer << " operand=" << flow.operand
            << " value=%h" << flow.value->id() << " common=[";
        for (std::size_t index = 0;
             index < flow.commonDimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << variableName(flow.commonDimensions[index]);
        }
        out << "]\n";
    }

    for (const ScalarRecurrence &recurrence :
         model.scalarRecurrences()) {
        out << "  recurrence R" << recurrence.id << " "
            << variableName(recurrence.dimension)
            << " init=%h" << recurrence.initial->id()
            << " iter=%h" << recurrence.iteration->id()
            << " yield=%h" << recurrence.yielded->id()
            << " result=%h" << recurrence.result->id()
            << " latch=S" << recurrence.yieldStatement
            << " init_source=";
        printScalarSource(out, recurrence.initialSource);
        out << "\n";
    }

    for (const RecurrenceUseRelation &use :
         model.recurrenceUses()) {
        out << "  recurrence_use R" << use.recurrence
            << (use.kind == RecurrenceValueKind::Iteration
                    ? ".iter"
                    : ".result")
            << " -> S" << use.consumer
            << " operand=" << use.operand << " common=[";
        for (std::size_t index = 0;
             index < use.commonDimensions.size(); ++index) {
            if (index)
                out << ", ";
            out << variableName(use.commonDimensions[index]);
        }
        out << "]\n";
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
