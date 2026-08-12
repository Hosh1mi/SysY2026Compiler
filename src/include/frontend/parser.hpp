/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_WORKSPACE_SRC_INCLUDE_FRONTEND_PARSER_HPP_INCLUDED
# define YY_YY_WORKSPACE_SRC_INCLUDE_FRONTEND_PARSER_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 5 "/workspace/src/frontend/parser.y"

    #include "ast/ast.hpp"
    #include <string>

    // Parser-only storage for comma-separated lists.  These helpers disappear
    // after reduction and are not part of the AST/IRGen contract.
    template <typename T>
    struct ParsedList {
        std::vector<std::unique_ptr<T>> values;
    };

    using ObjectDefList = ParsedList<ObjectDefAST>;
    using ExprList = ParsedList<ExprAST>;
    using InitValList = ParsedList<InitValAST>;
    using FuncParamList = ParsedList<FuncParamAST>;
    using TopLevelItem = TopLevelItemAST;
    using BlockItemList = std::vector<BlockItemAST>;
    using CallArgList = std::vector<CallArgumentAST>;

#line 69 "/workspace/src/include/frontend/parser.hpp"

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    INT = 258,                     /* INT  */
    FLOAT = 259,                   /* FLOAT  */
    ID = 260,                      /* ID  */
    STRING_LITERAL = 261,          /* STRING_LITERAL  */
    GTE = 262,                     /* GTE  */
    LTE = 263,                     /* LTE  */
    GT = 264,                      /* GT  */
    LT = 265,                      /* LT  */
    EQ = 266,                      /* EQ  */
    NEQ = 267,                     /* NEQ  */
    BASICTYPE = 268,               /* BASICTYPE  */
    VOID = 269,                    /* VOID  */
    INVALID = 270,                 /* INVALID  */
    CONST = 271,                   /* CONST  */
    RETURN = 272,                  /* RETURN  */
    IF = 273,                      /* IF  */
    ELSE = 274,                    /* ELSE  */
    WHILE = 275,                   /* WHILE  */
    BREAK = 276,                   /* BREAK  */
    CONTINUE = 277,                /* CONTINUE  */
    LP = 278,                      /* LP  */
    RP = 279,                      /* RP  */
    LB = 280,                      /* LB  */
    RB = 281,                      /* RB  */
    LC = 282,                      /* LC  */
    RC = 283,                      /* RC  */
    COMMA = 284,                   /* COMMA  */
    SEMICOLON = 285,               /* SEMICOLON  */
    NOT = 286,                     /* NOT  */
    ASSIGN = 287,                  /* ASSIGN  */
    MINUS = 288,                   /* MINUS  */
    ADD = 289,                     /* ADD  */
    MUL = 290,                     /* MUL  */
    DIV = 291,                     /* DIV  */
    MOD = 292,                     /* MOD  */
    AND = 293,                     /* AND  */
    OR = 294,                      /* OR  */
    LOWER_THEN_ELSE = 295          /* LOWER_THEN_ELSE  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 177 "/workspace/src/frontend/parser.y"

    CompUnitAST* compUnit;
    TopLevelItem* topLevel;
    DeclAST* decl;
    ObjectDefList* objectDefList;
    ObjectDefAST* objectDef;
    ExprList* exprList;
    InitValList* initValList;
    InitValAST* initVal;
    FuncDefAST* funcDef;
    FuncParamList* funcParamList;
    FuncParamAST* funcParam;
    BlockAST* block;
    BlockItemList* blockItemList;
    BlockItemAST* blockItem;
    StmtAST* stmt;
    LValueAST* lValue;
    ExprAST* expr;
    CallExprAST* callExpr;
    CallArgumentAST* callArg;
    CallArgList* callArgList;
    TypeSpec* type_spec;
    UnaryOp unaryOp;
    std::string* token;
    int int_val;
    float float_val;

#line 154 "/workspace/src/include/frontend/parser.hpp"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


extern YYSTYPE yylval;
extern YYLTYPE yylloc;

int yyparse (void);


#endif /* !YY_YY_WORKSPACE_SRC_INCLUDE_FRONTEND_PARSER_HPP_INCLUDED  */
