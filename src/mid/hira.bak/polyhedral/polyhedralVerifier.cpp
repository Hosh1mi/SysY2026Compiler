#include "../../../include/mid/hira/polyhedral/polyhedralVerifier.hpp"

#include "../../../include/mid/hira/ir/hiraIR.hpp"
#include "../../../include/mid/hira/polyhedral/polyhedralModel.hpp"
#include "../../../include/mid/ir/type.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>

namespace hira::polyhedral {
namespace {

PolyhedralVerificationResult
fail(PolyhedralVerifyError error, std::string detail) {
    PolyhedralVerificationResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

bool contains(const std::vector<AffineVariable> &variables,
              AffineVariable variable) {
    for (AffineVariable candidate : variables)
        if (candidate == variable)
            return true;
    return false;
}

PolyhedralVerificationResult
verifyConstraint(const PolyhedralModel &model,
                 const std::vector<AffineVariable> &dimensions,
                 const AffineConstraint &constraint) {
    if (!constraint.expression.valid())
        return fail(PolyhedralVerifyError::InvalidConstraint,
                    "invalid-expression");

    for (const auto &[variable, coefficient] :
         constraint.expression.coefficients()) {
        if (!coefficient || !model.space().source(variable))
            return fail(PolyhedralVerifyError::InvalidConstraint,
                        "invalid-variable");
        if (variable.kind == AffineVariableKind::Dimension &&
            !contains(dimensions, variable))
            return fail(PolyhedralVerifyError::InvalidConstraint,
                        "out-of-domain-dimension");
    }
    return {};
}

bool validStatementKind(const PolyhedralStatement &statement) {
    switch (statement.kind) {
    case StatementKind::Compute:
        return dynamic_cast<const HiraComputeOp *>(statement.node) ||
               dynamic_cast<const HiraIf *>(statement.node);
    case StatementKind::Load:
        return dynamic_cast<const HiraLoad *>(statement.node);
    case StatementKind::Store:
        return dynamic_cast<const HiraStore *>(statement.node);
    case StatementKind::Yield:
        return dynamic_cast<const HiraYield *>(statement.node);
    }
    return false;
}

std::vector<AffineVariable> commonDimensions(
    const std::vector<AffineVariable> &first,
    const std::vector<AffineVariable> &second) {
    std::vector<AffineVariable> common;
    for (AffineVariable variable : first)
        if (contains(second, variable))
            common.push_back(variable);
    return common;
}

bool containsResult(const HiraNode &node, const HiraValue *value) {
    for (const HiraValue *result : node.results())
        if (result == value)
            return true;
    return false;
}

} // namespace

const char *polyhedralVerifyErrorName(PolyhedralVerifyError error) {
    switch (error) {
    case PolyhedralVerifyError::None:
        return "none";
    case PolyhedralVerifyError::InvalidSpace:
        return "invalid-space";
    case PolyhedralVerifyError::InvalidDomain:
        return "invalid-domain";
    case PolyhedralVerifyError::InvalidConstraint:
        return "invalid-constraint";
    case PolyhedralVerifyError::InvalidStatement:
        return "invalid-statement";
    case PolyhedralVerifyError::InvalidSchedule:
        return "invalid-schedule";
    case PolyhedralVerifyError::InvalidMemoryObject:
        return "invalid-memory-object";
    case PolyhedralVerifyError::InvalidAccess:
        return "invalid-access";
    case PolyhedralVerifyError::InvalidScalarFlow:
        return "invalid-scalar-flow";
    case PolyhedralVerifyError::InvalidRecurrence:
        return "invalid-recurrence";
    case PolyhedralVerifyError::InvalidAlias:
        return "invalid-alias";
    case PolyhedralVerifyError::MissingProofObligation:
        return "missing-proof-obligation";
    }
    return "unknown";
}

PolyhedralVerificationResult
verifyPolyhedralModel(const PolyhedralModel &model) {
    std::set<const HiraValue *> sources;
    for (const HiraValue *dimension : model.space().dimensions())
        if (!dimension || !sources.insert(dimension).second)
            return fail(PolyhedralVerifyError::InvalidSpace,
                        "invalid-dimension-source");
    for (const HiraValue *symbol : model.space().symbols())
        if (!symbol || !sources.insert(symbol).second)
            return fail(PolyhedralVerifyError::InvalidSpace,
                        "invalid-symbol-source");

    std::set<AffineVariable> domainDimensions;
    for (const IterationDomain &domain : model.domains()) {
        if (!domain.loop ||
            domain.dimension.kind !=
                AffineVariableKind::Dimension ||
            model.space().source(domain.dimension) !=
                domain.loop->induction() ||
            domain.dimensions.empty() ||
            !(domain.dimensions.back() == domain.dimension) ||
            !domainDimensions.insert(domain.dimension).second)
            return fail(PolyhedralVerifyError::InvalidDomain,
                        "invalid-loop-dimension");

        std::set<AffineVariable> activeDimensions;
        for (AffineVariable dimension : domain.dimensions)
            if (dimension.kind !=
                    AffineVariableKind::Dimension ||
                !model.space().source(dimension) ||
                !activeDimensions.insert(dimension).second)
                return fail(PolyhedralVerifyError::InvalidDomain,
                            "invalid-domain-space");

        if (domain.constraints.size() <
            domain.dimensions.size() * 2)
            return fail(PolyhedralVerifyError::InvalidDomain,
                        "incomplete-loop-constraints");
        for (const AffineConstraint &constraint :
             domain.constraints) {
            PolyhedralVerificationResult result =
                verifyConstraint(model, domain.dimensions,
                                 constraint);
            if (!result.succeeded())
                return result;
        }

        bool hasNoWrapObligation = false;
        for (const ProofObligation &obligation :
             model.proofObligations())
            if (obligation.kind ==
                    ProofObligationKind::NoSignedWrap &&
                obligation.loop == domain.loop &&
                obligation.dimension == domain.dimension) {
                hasNoWrapObligation = true;
                break;
            }
        if (!hasNoWrapObligation)
            return fail(
                PolyhedralVerifyError::MissingProofObligation,
                "missing-no-signed-wrap");
    }

    if (domainDimensions.size() !=
        model.space().dimensions().size())
        return fail(PolyhedralVerifyError::InvalidSpace,
                    "dimension-without-domain");

    std::vector<std::size_t> accessCounts(
        model.statements().size(), 0);
    std::map<const HiraNode *, StatementId> nodeStatements;
    std::set<const HiraValue *> memoryBases;
    for (std::size_t index = 0;
         index < model.memoryObjects().size(); ++index) {
        const MemoryObject &object = model.memoryObjects()[index];
        if (object.id != index || !object.base ||
            !dynamic_cast<PointerType *>(object.base->type()) ||
            !memoryBases.insert(object.base).second)
            return fail(PolyhedralVerifyError::InvalidMemoryObject,
                        "invalid-memory-base");
    }
    std::set<std::pair<MemoryObjectId, MemoryObjectId>>
        aliasPairs;
    for (const MemoryAliasRelation &relation :
         model.aliasRelations()) {
        if (relation.first > relation.second ||
            relation.second >= model.memoryObjects().size() ||
            !aliasPairs.insert(
                 {relation.first, relation.second})
                 .second ||
            (relation.first == relation.second &&
             relation.kind != MemoryAliasKind::MustAlias) ||
            (relation.first != relation.second &&
             relation.kind == MemoryAliasKind::MustAlias))
            return fail(PolyhedralVerifyError::InvalidAlias,
                        "invalid-alias-relation");
        if (model.aliasRelation(relation.first,
                                relation.second) != relation.kind)
            return fail(PolyhedralVerifyError::InvalidAlias,
                        "alias-query-mismatch");
    }
    std::size_t objectCount = model.memoryObjects().size();
    if (aliasPairs.size() !=
        objectCount * (objectCount + 1) / 2)
        return fail(PolyhedralVerifyError::InvalidAlias,
                    "incomplete-alias-matrix");

    for (std::size_t index = 0;
         index < model.statements().size(); ++index) {
        const PolyhedralStatement &statement =
            model.statements()[index];
        if (statement.id != index || !statement.node ||
            !validStatementKind(statement) ||
            !nodeStatements
                 .insert({statement.node, statement.id})
                 .second)
            return fail(PolyhedralVerifyError::InvalidStatement,
                        "invalid-statement-node");

        std::set<AffineVariable> activeDimensions;
        for (AffineVariable dimension : statement.dimensions)
            if (dimension.kind !=
                    AffineVariableKind::Dimension ||
                !model.space().source(dimension) ||
                !activeDimensions.insert(dimension).second)
                return fail(
                    PolyhedralVerifyError::InvalidStatement,
                    "invalid-statement-space");
        if (statement.constraints.size() <
            statement.dimensions.size() * 2)
            return fail(PolyhedralVerifyError::InvalidStatement,
                        "incomplete-statement-domain");
        for (const AffineConstraint &constraint :
             statement.constraints) {
            PolyhedralVerificationResult result =
                verifyConstraint(model, statement.dimensions,
                                 constraint);
            if (!result.succeeded())
                return result;
        }

        if (statement.identitySchedule.empty() ||
            statement.identitySchedule.front().kind !=
                ScheduleComponentKind::SequencePosition ||
            statement.identitySchedule.back().kind !=
                ScheduleComponentKind::SequencePosition)
            return fail(PolyhedralVerifyError::InvalidSchedule,
                        "invalid-schedule-boundary");
        std::vector<AffineVariable> scheduledDimensions;
        for (const ScheduleComponent &component :
             statement.identitySchedule) {
            if (component.kind ==
                ScheduleComponentKind::Iteration) {
                if (component.dimension.kind !=
                        AffineVariableKind::Dimension ||
                    !contains(statement.dimensions,
                              component.dimension))
                    return fail(
                        PolyhedralVerifyError::InvalidSchedule,
                        "invalid-schedule-dimension");
                scheduledDimensions.push_back(
                    component.dimension);
            } else if (component.kind ==
                           ScheduleComponentKind::Branch &&
                       component.position > 1) {
                return fail(
                    PolyhedralVerifyError::InvalidSchedule,
                    "invalid-branch-position");
            }
        }
        if (scheduledDimensions != statement.dimensions)
            return fail(PolyhedralVerifyError::InvalidSchedule,
                        "incomplete-schedule-dimensions");
    }

    for (const AccessRelation &access : model.accesses()) {
        if (access.statement >= model.statements().size() ||
            access.object >= model.memoryObjects().size())
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "invalid-access-reference");
        const PolyhedralStatement &statement =
            model.statements()[access.statement];
        if ((access.kind == MemoryAccessKind::Read &&
             statement.kind != StatementKind::Load) ||
            (access.kind == MemoryAccessKind::Write &&
             statement.kind != StatementKind::Store))
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "access-kind-mismatch");
        for (const AffineExpr &subscript : access.subscripts) {
            AffineConstraint constraint{
                subscript, AffineRelation::EqualZero};
            PolyhedralVerificationResult result =
                verifyConstraint(model, statement.dimensions,
                                 constraint);
            if (!result.succeeded())
                return fail(PolyhedralVerifyError::InvalidAccess,
                            result.detail);
        }
        if (access.linearizedExtent &&
            (access.subscripts.size() != 2 ||
             access.linearizedExtent->kind !=
                 AffineVariableKind::Symbol ||
             !model.space().source(
                 *access.linearizedExtent)))
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "invalid-linearized-extent");
        ++accessCounts[access.statement];
    }

    for (std::size_t index = 0;
         index < model.statements().size(); ++index) {
        StatementKind kind = model.statements()[index].kind;
        std::size_t expected =
            kind == StatementKind::Load ||
                    kind == StatementKind::Store
                ? 1
                : 0;
        if (accessCounts[index] != expected)
            return fail(PolyhedralVerifyError::InvalidAccess,
                        "incomplete-statement-access");
    }

    using RecurrenceDefinition =
        std::pair<ScalarRecurrenceId, RecurrenceValueKind>;
    std::map<const HiraValue *, RecurrenceDefinition>
        recurrenceDefinitions;
    std::size_t expectedRecurrences = 0;
    for (const IterationDomain &domain : model.domains())
        expectedRecurrences += domain.loop->carriedValues().size();
    if (expectedRecurrences != model.scalarRecurrences().size())
        return fail(PolyhedralVerifyError::InvalidRecurrence,
                    "incomplete-recurrences");
    for (std::size_t index = 0;
         index < model.scalarRecurrences().size(); ++index) {
        const ScalarRecurrence &recurrence =
            model.scalarRecurrences()[index];
        if (recurrence.id != index || !recurrence.loop ||
            recurrence.dimension.kind !=
                AffineVariableKind::Dimension ||
            recurrence.dimensions.empty() ||
            !(recurrence.dimensions.back() ==
              recurrence.dimension) ||
            recurrence.yieldStatement >=
                model.statements().size() ||
            model.statements()[recurrence.yieldStatement].kind !=
                StatementKind::Yield ||
            !recurrence.initial || !recurrence.iteration ||
            !recurrence.yielded || !recurrence.result ||
            recurrence.iteration->definingNode() !=
                recurrence.loop ||
            recurrence.result->definingNode() != recurrence.loop)
            return fail(PolyhedralVerifyError::InvalidRecurrence,
                        "invalid-recurrence-interface");

        bool matchingDomain = false;
        for (const IterationDomain &domain : model.domains())
            if (domain.loop == recurrence.loop &&
                domain.dimension == recurrence.dimension &&
                domain.dimensions == recurrence.dimensions) {
                matchingDomain = true;
                break;
            }
        bool matchingBinding = false;
        for (const HiraLoop::CarriedBinding &binding :
             recurrence.loop->carriedValues())
            if (binding.initial == recurrence.initial &&
                binding.iteration == recurrence.iteration &&
                binding.yielded == recurrence.yielded &&
                binding.result == recurrence.result) {
                matchingBinding = true;
                break;
            }
        bool validInitialSource = false;
        const ScalarValueSource &initialSource =
            recurrence.initialSource;
        switch (initialSource.kind) {
        case ScalarSourceKind::External:
            validInitialSource =
                !recurrence.initial->definingNode() &&
                initialSource.commonDimensions.empty();
            break;
        case ScalarSourceKind::Statement:
            if (initialSource.source <
                model.statements().size()) {
                const PolyhedralStatement &source =
                    model.statements()[initialSource.source];
                validInitialSource =
                    recurrence.initial->definingNode() ==
                        source.node &&
                    containsResult(*source.node,
                                   recurrence.initial) &&
                    initialSource.commonDimensions ==
                        commonDimensions(source.dimensions,
                                         recurrence.dimensions);
            }
            break;
        case ScalarSourceKind::RecurrenceIteration:
        case ScalarSourceKind::RecurrenceResult:
            if (initialSource.source <
                model.scalarRecurrences().size()) {
                const ScalarRecurrence &source =
                    model.scalarRecurrences()[
                        initialSource.source];
                const HiraValue *expected =
                    initialSource.kind ==
                            ScalarSourceKind::
                                RecurrenceIteration
                        ? source.iteration
                        : source.result;
                validInitialSource =
                    recurrence.initial == expected &&
                    initialSource.commonDimensions ==
                        commonDimensions(source.dimensions,
                                         recurrence.dimensions);
            }
            break;
        case ScalarSourceKind::Dimension: {
            AffineVariable dimension{
                AffineVariableKind::Dimension,
                initialSource.source};
            validInitialSource =
                model.space().source(dimension) ==
                    recurrence.initial &&
                contains(recurrence.dimensions, dimension) &&
                initialSource.commonDimensions ==
                    std::vector<AffineVariable>{dimension};
            break;
        }
        }
        if (!matchingDomain || !matchingBinding ||
            !validInitialSource ||
            model.statements()[recurrence.yieldStatement].node !=
                recurrence.loop->body().nodes().back().get() ||
            !recurrenceDefinitions
                 .insert({recurrence.iteration,
                          {recurrence.id,
                           RecurrenceValueKind::Iteration}})
                 .second ||
            !recurrenceDefinitions
                 .insert({recurrence.result,
                          {recurrence.id,
                           RecurrenceValueKind::Result}})
                 .second)
            return fail(PolyhedralVerifyError::InvalidRecurrence,
                        "mismatched-recurrence");
    }

    std::set<std::pair<StatementId, std::uint32_t>>
        scalarFlowUses;
    for (const ScalarFlowRelation &flow : model.scalarFlows()) {
        if (flow.producer >= model.statements().size() ||
            flow.consumer >= model.statements().size())
            return fail(PolyhedralVerifyError::InvalidScalarFlow,
                        "invalid-flow-statement");
        const PolyhedralStatement &producer =
            model.statements()[flow.producer];
        const PolyhedralStatement &consumer =
            model.statements()[flow.consumer];
        if (!flow.value ||
            flow.operand >= consumer.node->operands().size() ||
            consumer.node->operands()[flow.operand] != flow.value ||
            flow.value->definingNode() != producer.node ||
            !containsResult(*producer.node, flow.value) ||
            flow.commonDimensions !=
                commonDimensions(producer.dimensions,
                                 consumer.dimensions) ||
            !scalarFlowUses
                 .insert({flow.consumer, flow.operand})
                 .second)
            return fail(PolyhedralVerifyError::InvalidScalarFlow,
                        "invalid-flow-interface");
    }

    std::set<std::pair<StatementId, std::uint32_t>>
        recurrenceUseSites;
    for (const RecurrenceUseRelation &use :
         model.recurrenceUses()) {
        if (use.recurrence >=
                model.scalarRecurrences().size() ||
            use.consumer >= model.statements().size())
            return fail(PolyhedralVerifyError::InvalidRecurrence,
                        "invalid-recurrence-use-reference");
        const ScalarRecurrence &recurrence =
            model.scalarRecurrences()[use.recurrence];
        const PolyhedralStatement &consumer =
            model.statements()[use.consumer];
        const HiraValue *expectedValue =
            use.kind == RecurrenceValueKind::Iteration
                ? recurrence.iteration
                : recurrence.result;
        if (use.operand >= consumer.node->operands().size() ||
            consumer.node->operands()[use.operand] !=
                expectedValue ||
            use.commonDimensions !=
                commonDimensions(recurrence.dimensions,
                                 consumer.dimensions) ||
            !recurrenceUseSites
                 .insert({use.consumer, use.operand})
                 .second)
            return fail(PolyhedralVerifyError::InvalidRecurrence,
                        "invalid-recurrence-use");
    }

    for (const PolyhedralStatement &consumer :
         model.statements()) {
        const auto &operands = consumer.node->operands();
        for (std::size_t operand = 0;
             operand < operands.size(); ++operand) {
            const HiraValue *value = operands[operand];
            const HiraNode *definition =
                value ? value->definingNode() : nullptr;
            bool needsScalarFlow =
                nodeStatements.count(definition) != 0;
            bool needsRecurrenceUse =
                recurrenceDefinitions.count(value) != 0;
            auto site = std::make_pair(
                consumer.id,
                static_cast<std::uint32_t>(operand));
            if (needsScalarFlow !=
                    (scalarFlowUses.count(site) != 0) ||
                needsRecurrenceUse !=
                    (recurrenceUseSites.count(site) != 0) ||
                (needsScalarFlow && needsRecurrenceUse))
                return fail(
                    PolyhedralVerifyError::InvalidScalarFlow,
                    "incomplete-scalar-use");
        }
    }
    return {};
}

} // namespace hira::polyhedral
