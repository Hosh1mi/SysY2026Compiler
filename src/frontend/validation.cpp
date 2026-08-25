// This pass checks context-sensitive SysY rules before IR generation.  It is
// intentionally structural: it depends only on the stable semantic AST and
// does not duplicate IR type lowering.

#include "../include/frontend/validation.hpp"
#include "../include/frontend/ast/ast.hpp"

#include <variant>

namespace {

bool checkExpr(const ExprAST *expr, std::string &error);

bool containsAggregate(const ExprAST *expr) {
    if (!expr) return false;
    if (dynamic_cast<const AggregateExprAST *>(expr)) return true;
    if (const auto *unary = dynamic_cast<const UnaryExprAST *>(expr))
        return containsAggregate(unary->operand.get());
    if (const auto *binary = dynamic_cast<const BinaryExprAST *>(expr))
        return containsAggregate(binary->left.get()) ||
               containsAggregate(binary->right.get());
    // A subscript produces one scalar lane even when its base is an aggregate.
    return false;
}

bool checkInitializer(const InitValAST *initializer, std::string &error) {
    if (!initializer) return true;
    if (initializer->isExpression())
        return checkExpr(initializer->expression(), error);
    for (const auto &element : initializer->elements())
        if (!checkInitializer(element.get(), error)) return false;
    return true;
}

bool checkLValue(const LValueAST *value, std::string &error) {
    for (const auto &index : value->indices)
        if (!checkExpr(index.get(), error)) return false;
    return true;
}

bool checkExpr(const ExprAST *expr, std::string &error) {
    if (!expr) return true;
    if (const auto *value = dynamic_cast<const LValueAST *>(expr))
        return checkLValue(value, error);
    if (const auto *call = dynamic_cast<const CallExprAST *>(expr)) {
        for (const auto &argument : call->arguments) {
            if (const auto *value =
                    std::get_if<std::unique_ptr<ExprAST>>(&argument))
                if (!checkExpr(value->get(), error)) return false;
        }
        return true;
    }
    if (const auto *unary = dynamic_cast<const UnaryExprAST *>(expr)) {
        return checkExpr(unary->operand.get(), error);
    }
    if (const auto *binary = dynamic_cast<const BinaryExprAST *>(expr)) {
        return checkExpr(binary->left.get(), error) &&
               checkExpr(binary->right.get(), error);
    }
    if (const auto *subscript = dynamic_cast<const SubscriptExprAST *>(expr))
        return checkExpr(subscript->base.get(), error) &&
               checkExpr(subscript->index.get(), error);
    if (const auto *aggregate =
            dynamic_cast<const AggregateExprAST *>(expr))
        return checkInitializer(aggregate->initializer.get(), error);
    return true; // numeric literal
}

bool checkDeclaration(const DeclAST *declaration, std::string &error) {
    for (const auto &object : declaration->objects) {
        if (declaration->isConst && !object->initializer) {
            error = "a const definition requires an initializer";
            return false;
        }
        for (const auto &dimension : object->dimensions)
            if (!checkExpr(dimension.get(), error)) return false;
        if (!declaration->tensor && object->dimensions.empty() &&
            object->initializer &&
            (!object->initializer->isExpression() ||
             containsAggregate(object->initializer->expression()))) {
            error = "a brace literal cannot initialize a scalar object";
            return false;
        }
        if (!checkInitializer(object->initializer.get(), error)) return false;
    }
    return true;
}

bool checkBlock(const BlockAST *block, TYPE returnType,
                unsigned loopDepth, std::string &error);

bool checkStatement(const StmtAST *statement, TYPE returnType,
                    unsigned loopDepth, std::string &error) {
    if (dynamic_cast<const EmptyStmtAST *>(statement)) return true;
    if (const auto *assignment =
            dynamic_cast<const AssignStmtAST *>(statement))
        return checkLValue(assignment->target.get(), error) &&
               checkExpr(assignment->value.get(), error);
    if (const auto *expression =
            dynamic_cast<const ExprStmtAST *>(statement))
        return checkExpr(expression->expression.get(), error);
    if (dynamic_cast<const BreakStmtAST *>(statement) ||
        dynamic_cast<const ContinueStmtAST *>(statement)) {
        if (loopDepth != 0) return true;
        error = dynamic_cast<const BreakStmtAST *>(statement)
                    ? "break is only valid inside a while loop"
                    : "continue is only valid inside a while loop";
        return false;
    }
    if (const auto *nested = dynamic_cast<const BlockStmtAST *>(statement))
        return checkBlock(nested->block.get(), returnType, loopDepth, error);
    if (const auto *ret = dynamic_cast<const ReturnStmtAST *>(statement)) {
        if (returnType == TYPE_VOID && ret->value) {
            error = "a void function cannot return a value";
            return false;
        }
        if (returnType != TYPE_VOID && !ret->value) {
            error = "a value-returning function requires a return expression";
            return false;
        }
        if (ret->value && returnType != TYPE_VOID &&
            containsAggregate(ret->value.get())) {
            error = "a scalar function cannot return a brace literal";
            return false;
        }
        return !ret->value || checkExpr(ret->value.get(), error);
    }
    if (const auto *selection = dynamic_cast<const IfStmtAST *>(statement))
        return checkExpr(selection->condition.get(), error) &&
               checkStatement(selection->thenBranch.get(), returnType,
                              loopDepth, error) &&
               (!selection->elseBranch ||
                checkStatement(selection->elseBranch.get(), returnType,
                               loopDepth, error));
    const auto *loop = dynamic_cast<const WhileStmtAST *>(statement);
    return loop && checkExpr(loop->condition.get(), error) &&
           checkStatement(loop->body.get(), returnType, loopDepth + 1, error);
}

bool checkBlock(const BlockAST *block, TYPE returnType,
                unsigned loopDepth, std::string &error) {
    for (const auto &item : block->items) {
        if (const auto *declaration =
                std::get_if<std::unique_ptr<DeclAST>>(&item)) {
            if (!checkDeclaration(declaration->get(), error)) return false;
        } else if (!checkStatement(
                       std::get<std::unique_ptr<StmtAST>>(item).get(),
                       returnType, loopDepth, error)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool validateFrontend(const CompUnitAST &unit, std::string &error) {
    unsigned mainCount = 0;
    for (const auto &item : unit.items) {
        if (const auto *declaration =
                std::get_if<std::unique_ptr<DeclAST>>(&item)) {
            if (!checkDeclaration(declaration->get(), error)) return false;
            continue;
        }

        const auto &function =
            *std::get<std::unique_ptr<FuncDefAST>>(item);
        for (const auto &parameter : function.parameters)
            for (const auto &dimension : parameter->trailingDimensions)
                if (!checkExpr(dimension.get(), error)) return false;
        if (function.name == "main") {
            ++mainCount;
            if (function.returnType != TYPE_INT ||
                !function.parameters.empty()) {
                error = "main must have signature int main()";
                return false;
            }
        }
        if (!checkBlock(function.body.get(), function.returnType, 0, error))
            return false;
    }
    if (mainCount != 1) {
        error = "a SysY program must define exactly one int main()";
        return false;
    }
    return true;
}
