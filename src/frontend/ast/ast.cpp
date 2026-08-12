// This file contains the AST's visitor dispatch points.  Keeping these tiny
// definitions out of the header avoids exposing IR-generation dependencies to
// parser translation units.

#include "../../include/frontend/ast/ast.hpp"

void CompUnitAST::accept(Visitor &visitor) { visitor.visit(*this); }
void DeclAST::accept(Visitor &visitor) { visitor.visit(*this); }
void ObjectDefAST::accept(Visitor &visitor) { visitor.visit(*this); }
void InitValAST::accept(Visitor &visitor) { visitor.visit(*this); }
void FuncDefAST::accept(Visitor &visitor) { visitor.visit(*this); }
void FuncParamAST::accept(Visitor &visitor) { visitor.visit(*this); }
void BlockAST::accept(Visitor &visitor) { visitor.visit(*this); }
void EmptyStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void AssignStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void ExprStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void BreakStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void ContinueStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void ReturnStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void BlockStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void IfStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void WhileStmtAST::accept(Visitor &visitor) { visitor.visit(*this); }
void LiteralExprAST::accept(Visitor &visitor) { visitor.visit(*this); }
void LValueAST::accept(Visitor &visitor) { visitor.visit(*this); }
void CallExprAST::accept(Visitor &visitor) { visitor.visit(*this); }
void UnaryExprAST::accept(Visitor &visitor) { visitor.visit(*this); }
void BinaryExprAST::accept(Visitor &visitor) { visitor.visit(*this); }
void SubscriptExprAST::accept(Visitor &visitor) { visitor.visit(*this); }
void AggregateExprAST::accept(Visitor &visitor) { visitor.visit(*this); }
