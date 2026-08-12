// This file validates context-sensitive SysY rules before an AST reaches IR
// generation, preventing malformed source programs from creating invalid IR.

#include "../include/frontend/validation.hpp"
#include "../include/frontend/ast/ast.hpp"

namespace {

bool checkAdd(const AddExpAST *ast, bool allowNot, std::string &error);

bool containsBrace(const AddExpAST *ast);

bool containsBrace(const InitValAST *init) {
    if (!init) return false;
    if (init->exp && containsBrace(init->exp.get())) return true;
    for (const auto &item : init->initValList)
        if (containsBrace(item.get())) return true;
    return false;
}

bool containsBrace(const UnaryExpAST *ast);

bool containsBrace(const MulExpAST *ast) {
    return ast && (containsBrace(ast->mulExp.get()) ||
                   containsBrace(ast->unaryExp.get()));
}

bool containsBrace(const AddExpAST *ast) {
    return ast && (containsBrace(ast->addExp.get()) ||
                   containsBrace(ast->mulExp.get()));
}

bool containsBrace(const PrimaryExpAST *ast) {
    if (!ast) return false;
    if (ast->initVal) return true;
    if (containsBrace(ast->exp.get())) return true;
    return false;
}

bool containsBrace(const CallAST *) { return false; }

bool containsBrace(const UnaryExpAST *ast) {
    if (!ast) return false;
    // A lane extraction has scalar result even when its base is a vector
    // literal expression.  Indices and call arguments do not determine the
    // value category of the surrounding expression either.
    if (ast->subscript) return false;
    return containsBrace(ast->primaryExp.get()) ||
           containsBrace(ast->call.get()) ||
           containsBrace(ast->unaryExp.get());
}

bool checkInit(const InitValAST *init, std::string &error) {
    if (!init) return true;
    if (init->exp && !checkAdd(init->exp.get(), false, error)) return false;
    for (const auto &item : init->initValList)
        if (!checkInit(item.get(), error)) return false;
    return true;
}

bool checkLVal(const LValAST *ast, std::string &error) {
    if (!ast) return true;
    for (const auto &index : ast->arrays)
        if (!checkAdd(index.get(), false, error)) return false;
    return true;
}

bool checkCall(const CallAST *ast, std::string &error) {
    if (!ast) return true;
    for (const auto &argument : ast->funcCParamList)
        if (argument->exp && !checkAdd(argument->exp.get(), false, error))
            return false;
    return true;
}

bool checkPrimary(const PrimaryExpAST *ast, bool allowNot,
                  std::string &error);

bool checkUnary(const UnaryExpAST *ast, bool allowNot, std::string &error) {
    if (!ast) return true;
    if (ast->subscript) {
        if (!checkUnary(ast->unaryExp.get(), allowNot, error)) return false;
        return checkAdd(ast->subscript.get(), false, error);
    }
    if (ast->primaryExp)
        return checkPrimary(ast->primaryExp.get(), allowNot, error);
    if (ast->call) return checkCall(ast->call.get(), error);
    if (ast->unaryExp) {
        if (ast->op == UOP_NOT && !allowNot) {
            error = "logical '!' is only valid inside a condition";
            return false;
        }
        return checkUnary(ast->unaryExp.get(), allowNot, error);
    }
    return true;
}

bool checkMul(const MulExpAST *ast, bool allowNot, std::string &error) {
    if (!ast) return true;
    return checkMul(ast->mulExp.get(), allowNot, error) &&
           checkUnary(ast->unaryExp.get(), allowNot, error);
}

bool checkAdd(const AddExpAST *ast, bool allowNot, std::string &error) {
    if (!ast) return true;
    return checkAdd(ast->addExp.get(), allowNot, error) &&
           checkMul(ast->mulExp.get(), allowNot, error);
}

bool checkPrimary(const PrimaryExpAST *ast, bool,
                  std::string &error) {
    if (!ast) return true;
    // Parenthesized expressions, indices, call arguments and initializer
    // elements are Exp in the official grammar even when nested in Cond.
    if (ast->exp) return checkAdd(ast->exp.get(), false, error);
    if (ast->lval) return checkLVal(ast->lval.get(), error);
    if (ast->initVal) return checkInit(ast->initVal.get(), error);
    return true;
}

bool checkRel(const RelExpAST *ast, std::string &error) {
    if (!ast) return true;
    return checkRel(ast->relExp.get(), error) &&
           checkAdd(ast->addExp.get(), true, error);
}

bool checkEq(const EqExpAST *ast, std::string &error) {
    if (!ast) return true;
    return checkEq(ast->eqExp.get(), error) &&
           checkRel(ast->relExp.get(), error);
}

bool checkLAnd(const LAndExpAST *ast, std::string &error) {
    if (!ast) return true;
    return checkLAnd(ast->lAndExp.get(), error) &&
           checkEq(ast->eqExp.get(), error);
}

bool checkCond(const LOrExpAST *ast, std::string &error) {
    if (!ast) return true;
    return checkCond(ast->lOrExp.get(), error) &&
           checkLAnd(ast->lAndExp.get(), error);
}

bool checkDecl(const DeclAST *decl, std::string &error) {
    if (!decl) return true;
    for (const auto &def : decl->defList) {
        if (decl->isConst && !def->initVal) {
            error = "a const definition requires an initializer";
            return false;
        }
        for (const auto &dimension : def->arrays)
            if (!checkAdd(dimension.get(), false, error)) return false;
        if (decl->bType.isScalar() && def->arrays.empty() && def->initVal &&
            (!def->initVal->exp || containsBrace(def->initVal->exp.get()))) {
            error = "a brace literal cannot initialize a scalar object";
            return false;
        }
        if (!checkInit(def->initVal.get(), error)) return false;
    }
    return true;
}

bool checkBlock(const BlockAST *block, const TypeSpec &returnType,
                unsigned loopDepth, std::string &error);

bool checkStmt(const StmtAST *stmt, const TypeSpec &returnType,
               unsigned loopDepth, std::string &error) {
    if (!stmt) return true;
    switch (stmt->sType) {
      case ASS:
        return checkLVal(stmt->lVal.get(), error) &&
               checkAdd(stmt->exp.get(), false, error) &&
               checkInit(stmt->initVal.get(), error);
      case EXP:
        return checkAdd(stmt->exp.get(), false, error);
      case BRE:
      case CONT:
        if (loopDepth == 0) {
            error = stmt->sType == BRE
                        ? "break is only valid inside a while loop"
                        : "continue is only valid inside a while loop";
            return false;
        }
        return true;
      case BLK:
        return checkBlock(stmt->block.get(), returnType, loopDepth, error);
      case RET: {
        bool hasValue = stmt->returnStmt && stmt->returnStmt->exp;
        if (returnType == TYPE_VOID && hasValue) {
            error = "a void function cannot return a value";
            return false;
        }
        if (returnType != TYPE_VOID && !hasValue) {
            error = "a value-returning function requires a return expression";
            return false;
        }
        if (hasValue && returnType.isScalar() &&
            containsBrace(stmt->returnStmt->exp.get())) {
            error = "a scalar function cannot return a brace literal";
            return false;
        }
        return !hasValue ||
               checkAdd(stmt->returnStmt->exp.get(), false, error);
      }
      case SEL:
        return checkCond(stmt->selectStmt->cond.get(), error) &&
               checkStmt(stmt->selectStmt->ifStmt.get(), returnType,
                         loopDepth, error) &&
               checkStmt(stmt->selectStmt->elseStmt.get(), returnType,
                         loopDepth, error);
      case ITER:
        return checkCond(stmt->iterationStmt->cond.get(), error) &&
               checkStmt(stmt->iterationStmt->stmt.get(), returnType,
                         loopDepth + 1, error);
      case SEMI:
        return true;
    }
    return true;
}

bool checkBlock(const BlockAST *block, const TypeSpec &returnType,
                unsigned loopDepth, std::string &error) {
    if (!block) return true;
    for (const auto &item : block->blockItemList) {
        if (!checkDecl(item->decl.get(), error) ||
            !checkStmt(item->stmt.get(), returnType, loopDepth, error))
            return false;
    }
    return true;
}

} // namespace

bool validateFrontend(const CompUnitAST &unit, std::string &error) {
    unsigned mainCount = 0;
    for (const auto &entry : unit.declDefList) {
        if (entry->Decl && !checkDecl(entry->Decl.get(), error)) return false;
        if (!entry->funcDef) continue;
        const auto &function = *entry->funcDef;
        for (const auto &parameter : function.funcFParamList)
            for (const auto &dimension : parameter->arrays)
                if (!checkAdd(dimension.get(), false, error)) return false;
        if (*function.id == "main") {
            ++mainCount;
            if (function.funcType != TYPE_INT ||
                !function.funcFParamList.empty()) {
                error = "main must have signature int main()";
                return false;
            }
        }
        if (!checkBlock(function.block.get(), function.funcType, 0, error))
            return false;
    }
    if (mainCount != 1) {
        error = "a SysY program must define exactly one int main()";
        return false;
    }
    return true;
}
