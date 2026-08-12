%define parse.error verbose
%define api.header.include {"../include/frontend/parser.hpp"}
%locations

%code requires {
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
}

%{
    #include <cstdio>
    #include <cstdlib>
    #include <cctype>
    #include <iostream>
    #include <optional>
    #include <string>
    #include <utility>
    #include "../include/frontend/ast/ast.hpp"
    using std::string;
    using std::unique_ptr;

    unique_ptr<CompUnitAST> root; /* the top level root node of our final AST */
    std::string filename;

    extern int yylineno;
    extern int yylex();
    extern FILE *yyin;
    extern void yyerror(const char *s);
    void initFileName(const char *name);

    template <typename T, typename... Args>
    T *make_node(Args &&...args) {
      return new T(std::forward<Args>(args)...);
    }

    // ------------------------------------------------------------------
    // Unpublished type-extension compatibility layer.
    //
    // The official VecType production is intentionally still unspecified.
    // Keep every guessed spelling in these helpers and in the marked VecType
    // grammar below.  The lexer returns all of these words as ordinary IDs,
    // so extensions never reserve identifiers outside a type position.
    // ------------------------------------------------------------------
    static bool decimalSuffix(const string &text, size_t begin,
                              unsigned &value) {
      if (begin == text.size()) return false;
      unsigned result = 0;
      for (size_t i = begin; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        result = result * 10 + static_cast<unsigned>(text[i] - '0');
      }
      value = result;
      return true;
    }

    static std::optional<TypeSpec> simpleVectorType(const string &name) {
      if (name == "intvec" || name == "ivec")
        return TypeSpec::dynamic(TYPE_INT);
      if (name == "floatvec" || name == "fvec")
        return TypeSpec::dynamic(TYPE_FLOAT);
      if (name == "i32x4" || name == "int32x4" || name == "v4i32")
        return TypeSpec::fixed(TYPE_INT, 4);
      if (name == "f32x4" || name == "float32x4" || name == "v4f32")
        return TypeSpec::fixed(TYPE_FLOAT, 4);

      unsigned lanes = 0;
      if (name.rfind("int", 0) == 0 && decimalSuffix(name, 3, lanes))
        return TypeSpec::fixed(TYPE_INT, lanes);
      if (name.rfind("float", 0) == 0 && decimalSuffix(name, 5, lanes))
        return TypeSpec::fixed(TYPE_FLOAT, lanes);
      if (name.rfind("ivec", 0) == 0 && decimalSuffix(name, 4, lanes))
        return TypeSpec::fixed(TYPE_INT, lanes);
      if (name.rfind("fvec", 0) == 0 && decimalSuffix(name, 4, lanes))
        return TypeSpec::fixed(TYPE_FLOAT, lanes);

      auto suffixed = [&](const string &prefix, char suffix,
                          TYPE element) -> std::optional<TypeSpec> {
        if (name.size() <= prefix.size() + 1 ||
            name.rfind(prefix, 0) != 0 || name.back() != suffix)
          return std::nullopt;
        unsigned count = 0;
        if (!decimalSuffix(name.substr(0, name.size() - 1), prefix.size(),
                           count))
          return std::nullopt;
        return TypeSpec::fixed(element, count);
      };
      if (auto type = suffixed("vec", 'i', TYPE_INT)) return type;
      if (auto type = suffixed("vec", 'f', TYPE_FLOAT)) return type;
      if (auto type = suffixed("vector", 'i', TYPE_INT)) return type;
      if (auto type = suffixed("vector", 'f', TYPE_FLOAT)) return type;
      return std::nullopt;
    }

    static bool isVectorConstructor(const string &name) {
      return name == "vector" || name == "vec";
    }

    static std::optional<unsigned> vectorWidthAlias(const string &name) {
      unsigned lanes = 0;
      if (name.rfind("vec", 0) == 0 && decimalSuffix(name, 3, lanes))
        return lanes;
      if (name.rfind("vector", 0) == 0 && decimalSuffix(name, 6, lanes))
        return lanes;
      return std::nullopt;
    }

    static bool decodeStringLiteral(const string &raw, string &decoded) {
      auto hexValue = [](char ch) -> unsigned {
        if (ch >= '0' && ch <= '9') return static_cast<unsigned>(ch - '0');
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return static_cast<unsigned>(ch - 'a' + 10);
      };
      decoded.clear();
      if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"')
        return false;
      for (size_t i = 1; i + 1 < raw.size(); ++i) {
        char ch = raw[i];
        if (ch != '\\') {
          decoded.push_back(ch);
          continue;
        }
        if (++i + 1 >= raw.size()) return false;
        char escaped = raw[i];
        switch (escaped) {
          case 'a': decoded.push_back('\a'); break;
          case 'b': decoded.push_back('\b'); break;
          case 'f': decoded.push_back('\f'); break;
          case 'n': decoded.push_back('\n'); break;
          case 'r': decoded.push_back('\r'); break;
          case 't': decoded.push_back('\t'); break;
          case 'v': decoded.push_back('\v'); break;
          case '\\': decoded.push_back('\\'); break;
          case '"': decoded.push_back('"'); break;
          case '?': decoded.push_back('?'); break;
          case 'x': {
            if (i + 1 >= raw.size() - 1 ||
                !std::isxdigit(static_cast<unsigned char>(raw[i + 1])))
              return false;
            unsigned value = 0;
            while (i + 1 < raw.size() - 1 &&
                   std::isxdigit(static_cast<unsigned char>(raw[i + 1])))
              value = value * 16 + hexValue(raw[++i]);
            decoded.push_back(static_cast<char>(value));
            break;
          }
          default:
            if (escaped < '0' || escaped > '7') return false;
            unsigned value = static_cast<unsigned>(escaped - '0');
            for (unsigned count = 1; count < 3 &&
                                     i + 1 < raw.size() - 1 &&
                                     raw[i + 1] >= '0' && raw[i + 1] <= '7';
                 ++count)
              value = value * 8 + static_cast<unsigned>(raw[++i] - '0');
            decoded.push_back(static_cast<char>(value));
            break;
        }
      }
      return true;
    }
%}

%union {
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
}

%type <compUnit> CompUnit
%type <topLevel> DeclDef
%type <decl> Decl
%type <objectDefList> ConstDefList VarDefList
%type <objectDef> ConstDef VarDef
%type <exprList> Arrays
%type <initValList> InitValList
%type <initVal> ConstInitVal InitVal BraceInitVal
%type <funcDef> FuncDef
%type <funcParamList> FuncFParamList OptFuncFParamList
%type <funcParam> FuncFParam
%type <block> Block
%type <blockItemList> BlockItemList
%type <blockItem> BlockItem
%type <stmt> Stmt ReturnStmt SelectStmt IterationStmt
%type <lValue> LVal
%type <expr> PrimaryExp Number UnaryExp MulExp AddExp Exp NonBraceExp
%type <expr> NonBraceAddExp NonBraceMulExp NonBraceUnaryExp
%type <expr> RelExp EqExp LAndExp Cond LOrExp
%type <callExpr> Call
%type <callArg> FuncCParam
%type <callArgList> FuncCParamList

%type <type_spec> BType VoidType VecType VecWidth
%type <unaryOp> UnaryOp

// %token 定义终结符的语义值类型
%token <int_val> INT           // 指定INT字面量的语义值是type_int，有词法分析得到的数值
%token <float_val> FLOAT       // 指定FLOAT字面量的语义值是type_float，有词法分析得到的数值
%token <token> ID STRING_LITERAL
%token GTE LTE GT LT EQ NEQ    // 关系运算
%token <int_val> BASICTYPE
%token VOID INVALID
%token CONST RETURN IF ELSE WHILE BREAK CONTINUE
%token LP RP LB RB LC RC COMMA SEMICOLON
// 用bison对该文件编译时，带参数-d，生成的exp.tab.h中给这些单词进行编码，可在lex.l中包含parser.tab.h使用这些单词种类码
%token NOT ASSIGN MINUS ADD MUL DIV MOD AND OR
// Unused tokens
/* %token POS NEG */

// Every pointer-valued semantic object is owned either by the AST action that
// consumes it or by Bison while it is on the parse stack.  These destructors
// make syntax-error paths leak-free.
%destructor { delete $$; } <compUnit> <topLevel> <decl> <objectDefList>
%destructor { delete $$; } <objectDef> <exprList> <initValList> <initVal>
%destructor { delete $$; } <funcDef> <funcParamList> <funcParam> <block>
%destructor { delete $$; } <blockItemList> <blockItem> <stmt> <lValue>
%destructor { delete $$; } <expr> <callExpr> <callArg> <callArgList>
%destructor { delete $$; } <type_spec> <token>

%precedence LOWER_THEN_ELSE
%precedence ELSE

%start Program

%%
Program:
    CompUnit {
        root = unique_ptr<CompUnitAST>($1);
    };

// 编译单元
CompUnit:
    CompUnit DeclDef {
        $$ = $1;
        $$->items.push_back(std::move(*$2));
        delete $2;
    }|
    DeclDef {
        $$ = make_node<CompUnitAST>();
        $$->items.push_back(std::move(*$1));
        delete $1;
    };

// A top-level item is already a semantic node; no DeclDef wrapper is built.
DeclDef:
    Decl { $$ = make_node<TopLevelItem>(unique_ptr<DeclAST>($1)); }|
    FuncDef { $$ = make_node<TopLevelItem>(unique_ptr<FuncDefAST>($1)); };

// 变量或常量声明
Decl:
    CONST BType ConstDefList SEMICOLON {
        $$ = make_node<DeclAST>(*$2, true, std::move($3->values));
        delete $2; delete $3;
    }|
    BType VarDefList SEMICOLON {
        $$ = make_node<DeclAST>(*$1, false, std::move($2->values));
        delete $1; delete $2;
    };

// 基本类型
BType:
    BASICTYPE {
        $$ = new TypeSpec(static_cast<TYPE>($1));
    }|
    VecType {
        $$ = $1;
    };

// === DECISION-DAY VecType EDIT AREA ====================================
// Replace or extend only this block when the official VecType production is
// published.  Word-like spellings arrive as ID and are checked here so none of
// the guessed spellings become global lexer keywords.
VecType:
    ID {
        auto type = simpleVectorType(*$1);
        if (!type) {
            yyerror(("unknown type spelling '" + *$1 + "'").c_str());
            delete $1;
            YYERROR;
        }
        $$ = new TypeSpec(*type);
        delete $1;
    }|
    ID LT BASICTYPE COMMA VecWidth GT {
        if (!isVectorConstructor(*$1)) {
            yyerror(("unknown vector constructor '" + *$1 + "'").c_str());
            delete $1; delete $5; YYERROR;
        }
        $5->element = static_cast<TYPE>($3);
        $$ = $5;
        delete $1;
    }|
    ID LT VecWidth COMMA BASICTYPE GT {
        if (!isVectorConstructor(*$1)) {
            yyerror(("unknown vector constructor '" + *$1 + "'").c_str());
            delete $1; delete $3; YYERROR;
        }
        $3->element = static_cast<TYPE>($5);
        $$ = $3;
        delete $1;
    }|
    ID LP BASICTYPE COMMA VecWidth RP {
        if (!isVectorConstructor(*$1)) {
            yyerror(("unknown vector constructor '" + *$1 + "'").c_str());
            delete $1; delete $5; YYERROR;
        }
        $5->element = static_cast<TYPE>($3);
        $$ = $5;
        delete $1;
    }|
    ID LB BASICTYPE COMMA VecWidth RB {
        if (!isVectorConstructor(*$1)) {
            yyerror(("unknown vector constructor '" + *$1 + "'").c_str());
            delete $1; delete $5; YYERROR;
        }
        $5->element = static_cast<TYPE>($3);
        $$ = $5;
        delete $1;
    }|
    BASICTYPE LT VecWidth GT {
        $3->element = static_cast<TYPE>($1);
        $$ = $3;
    }|
    BASICTYPE LB VecWidth RB {
        $3->element = static_cast<TYPE>($1);
        $$ = $3;
    }|
    ID LT BASICTYPE GT {
        if (isVectorConstructor(*$1))
            $$ = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>($3)));
        else if (auto width = vectorWidthAlias(*$1))
            $$ = new TypeSpec(TypeSpec::fixed(static_cast<TYPE>($3), *width));
        else {
            yyerror(("unknown vector constructor '" + *$1 + "'").c_str());
            delete $1; YYERROR;
        }
        delete $1;
    }|
    ID LP BASICTYPE RP {
        if (!isVectorConstructor(*$1)) {
            yyerror(("unknown vector constructor '" + *$1 + "'").c_str());
            delete $1; YYERROR;
        }
        $$ = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>($3)));
        delete $1;
    }|
    BASICTYPE LT GT {
        $$ = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>($1)));
    }|
    BASICTYPE LB RB {
        $$ = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>($1)));
    };
// === END DECISION-DAY VecType EDIT AREA ================================

VecWidth:
    INT {
        $$ = new TypeSpec(TypeSpec::fixed(TYPE_VOID, $1));
    }|
    ID {
        if (auto width = vectorWidthAlias(*$1))
            $$ = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *width));
        else
            $$ = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *$1));
        delete $1;
    };

// 空类型
VoidType:
    VOID {
        $$ = new TypeSpec(TYPE_VOID);
    };

// 定义列表
ConstDefList:
    ConstDef {
        $$ = make_node<ObjectDefList>();
        $$->values.push_back(unique_ptr<ObjectDefAST>($1));
    }|
    ConstDefList COMMA ConstDef {
        $$ = $1;
        $$->values.push_back(unique_ptr<ObjectDefAST>($3));
    };

VarDefList:
    VarDef {
        $$ = make_node<ObjectDefList>();
        $$->values.push_back(unique_ptr<ObjectDefAST>($1));
    }|
    VarDefList COMMA VarDef {
        $$ = $1;
        $$->values.push_back(unique_ptr<ObjectDefAST>($3));
    };

// Constants and variables deliberately have separate productions.  Besides
// matching the official grammar, this makes an uninitialized const impossible
// to represent in the AST.
ConstDef:
    ID Arrays ASSIGN ConstInitVal {
        $$ = make_node<ObjectDefAST>(std::move(*$1), std::move($2->values),
                                     unique_ptr<InitValAST>($4));
        delete $1; delete $2;
    }|
    ID ASSIGN Exp {
        $$ = make_node<ObjectDefAST>(
            std::move(*$1), std::vector<unique_ptr<ExprAST>>{},
            unique_ptr<InitValAST>(make_node<InitValAST>(
                unique_ptr<ExprAST>($3))));
        delete $1;
    };

VarDef:
    ID Arrays ASSIGN InitVal {
        $$ = make_node<ObjectDefAST>(std::move(*$1), std::move($2->values),
                                     unique_ptr<InitValAST>($4));
        delete $1; delete $2;
    }|
    ID ASSIGN Exp {
        $$ = make_node<ObjectDefAST>(
            std::move(*$1), std::vector<unique_ptr<ExprAST>>{},
            unique_ptr<InitValAST>(make_node<InitValAST>(
                unique_ptr<ExprAST>($3))));
        delete $1;
    }|
    ID Arrays {
        $$ = make_node<ObjectDefAST>(std::move(*$1), std::move($2->values));
        delete $1; delete $2;
    }|
    ID {
        $$ = make_node<ObjectDefAST>(std::move(*$1));
        delete $1;
    };

// 数组
Arrays:
    LB Exp RB {
        $$ = make_node<ExprList>();
        $$->values.push_back(unique_ptr<ExprAST>($2));
    }|
    Arrays LB Exp RB {
        $$ = $1;
        $$->values.push_back(unique_ptr<ExprAST>($3));
    };

// Define blocks before braced expressions.  In the statement-start parser
// state, an empty pair of braces can otherwise be reduced as an empty vector
// literal even though no expression terminator follows it.  Initializer and
// assignment states do not admit Block, so `int4 value = {}` remains
// unambiguous there.
Block:
    LC RC {
        $$ = make_node<BlockAST>();
    }|
    LC BlockItemList RC {
        $$ = make_node<BlockAST>();
        $$->items.swap(*$2);
        delete $2;
    };


// 变量或常量初值
// Aggregate braces are listed first so array-of-vector initializers keep the
// nested InitVal shape instead of collapsing into expression brace primaries.
InitVal:
    LC RC {
        $$ = make_node<InitValAST>();
    }|
    LC InitValList RC {
        $$ = make_node<InitValAST>(std::move($2->values));
        delete $2;
    }|
    NonBraceExp {
        $$ = make_node<InitValAST>(unique_ptr<ExprAST>($1));
    };

ConstInitVal:
    LC RC {
        $$ = make_node<InitValAST>();
    }|
    LC InitValList RC {
        $$ = make_node<InitValAST>(std::move($2->values));
        delete $2;
    }|
    NonBraceExp {
        $$ = make_node<InitValAST>(unique_ptr<ExprAST>($1));
    };

// Braced initializer used on the right-hand side of a whole-vector assignment.
BraceInitVal:
    LC RC {
        $$ = make_node<InitValAST>();
    }|
    LC InitValList RC {
        $$ = make_node<InitValAST>(std::move($2->values));
        delete $2;
    };

// 变量列表
InitValList:
  InitValList COMMA InitVal {
    $$ = $1;
    $$->values.push_back(unique_ptr<InitValAST>($3));
  }|
  InitVal {
    $$ = make_node<InitValList>();
    $$->values.push_back(unique_ptr<InitValAST>($1));
  };

// 函数定义
FuncDef:
    BType ID LP OptFuncFParamList RP Block {
        $$ = make_node<FuncDefAST>(*$1, std::move(*$2),
            std::move($4->values), unique_ptr<BlockAST>($6));
        delete $1; delete $2; delete $4;
    }|
    VoidType ID LP OptFuncFParamList RP Block {
        $$ = make_node<FuncDefAST>(*$1, std::move(*$2),
            std::move($4->values), unique_ptr<BlockAST>($6));
        delete $1; delete $2; delete $4;
    };

OptFuncFParamList:
    %empty {
        $$ = make_node<FuncParamList>();
    }|
    FuncFParamList {
        $$ = $1;
    };

// 函数形参列表
FuncFParamList:
    FuncFParam {
        $$ = make_node<FuncParamList>();
        $$->values.push_back(unique_ptr<FuncParamAST>($1));
    }|
    FuncFParamList COMMA FuncFParam {
        $$ = $1;
        $$->values.push_back(unique_ptr<FuncParamAST>($3));
    };

// 函数形参
FuncFParam:
    BType ID {
        $$ = make_node<FuncParamAST>(*$1, std::move(*$2));
        delete $1; delete $2;
    }|
    BType ID LB RB {
        $$ = make_node<FuncParamAST>(*$1, std::move(*$2), true);
        delete $1; delete $2;
    }|
    BType ID LB RB Arrays {
        $$ = make_node<FuncParamAST>(*$1, std::move(*$2), true,
                                     std::move($5->values));
        delete $1; delete $2; delete $5;
    };

// 语句块项列表
BlockItemList:
    BlockItem {
        $$ = make_node<BlockItemList>();
        $$->push_back(std::move(*$1));
        delete $1;
    }|
    BlockItemList BlockItem {
        $$ = $1;
        $$->push_back(std::move(*$2));
        delete $2;
    };

// 语句块项
BlockItem:
    Decl {
        $$ = make_node<BlockItemAST>(unique_ptr<DeclAST>($1));
    }|
    Stmt {
        $$ = make_node<BlockItemAST>(unique_ptr<StmtAST>($1));
    };

// Each statement production creates its concrete semantic node.  There is no
// tag plus a collection of mostly-null fields for IRGen to decode.
Stmt:
    SEMICOLON {
        $$ = make_node<EmptyStmtAST>();
    }|
    LVal ASSIGN Exp SEMICOLON {
        $$ = make_node<AssignStmtAST>(unique_ptr<LValueAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    NonBraceExp SEMICOLON {
        $$ = make_node<ExprStmtAST>(unique_ptr<ExprAST>($1));
    }|
    CONTINUE SEMICOLON {
        $$ = make_node<ContinueStmtAST>();
    }|
    BREAK SEMICOLON {
        $$ = make_node<BreakStmtAST>();
    }|
    Block {
        $$ = make_node<BlockStmtAST>(unique_ptr<BlockAST>($1));
    }|
    ReturnStmt { $$ = $1; }|
    SelectStmt { $$ = $1; }|
    IterationStmt { $$ = $1; };

//选择语句
SelectStmt:
    IF LP Cond RP Stmt %prec LOWER_THEN_ELSE {
        $$ = make_node<IfStmtAST>(unique_ptr<ExprAST>($3),
                                  unique_ptr<StmtAST>($5));
    }|
    IF LP Cond RP Stmt ELSE Stmt {
        $$ = make_node<IfStmtAST>(unique_ptr<ExprAST>($3),
                                  unique_ptr<StmtAST>($5),
                                  unique_ptr<StmtAST>($7));
    };

//循环语句
IterationStmt:
    WHILE LP Cond RP Stmt {
        $$ = make_node<WhileStmtAST>(unique_ptr<ExprAST>($3),
                                     unique_ptr<StmtAST>($5));
    };

//返回语句
ReturnStmt:
    RETURN Exp SEMICOLON {
        $$ = make_node<ReturnStmtAST>(unique_ptr<ExprAST>($2));
    }|
    RETURN SEMICOLON {
        $$ = make_node<ReturnStmtAST>();
    };

// 表达式
Exp:
    AddExp {
        $$ = $1;
    };

// An expression statement cannot start with a raw brace literal: at statement
// start that spelling is a block.  Other contexts still use Exp and retain all
// existing vector-literal expressions.  This small split removes the genuine
// Block-vs-literal ambiguity without duplicating the complete expression tree.
NonBraceExp:
    NonBraceAddExp { $$ = $1; };

NonBraceAddExp:
    NonBraceMulExp { $$ = $1; }|
    NonBraceAddExp ADD MulExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Add,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    NonBraceAddExp MINUS MulExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Subtract,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

NonBraceMulExp:
    NonBraceUnaryExp { $$ = $1; }|
    NonBraceMulExp MUL UnaryExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Multiply,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    NonBraceMulExp DIV UnaryExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Divide,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    NonBraceMulExp MOD UnaryExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Remainder,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

NonBraceUnaryExp:
    LP Exp RP { $$ = $2; }|
    LVal { $$ = $1; }|
    Number { $$ = $1; }|
    Call { $$ = $1; }|
    Call LB Exp RB {
        $$ = make_node<SubscriptExprAST>(unique_ptr<ExprAST>($1),
                                         unique_ptr<ExprAST>($3));
    }|
    LP Exp RP LB Exp RB {
        $$ = make_node<SubscriptExprAST>(unique_ptr<ExprAST>($2),
                                         unique_ptr<ExprAST>($5));
    }|
    UnaryOp UnaryExp {
        $$ = make_node<UnaryExprAST>($1, unique_ptr<ExprAST>($2));
    };

// 条件表达式
Cond:
    LOrExp {
        $$ = $1;
    };

// 左值表达式
LVal:
    ID {
        $$ = make_node<LValueAST>(std::move(*$1));
        delete $1;
    }|
    ID Arrays {
        $$ = make_node<LValueAST>(std::move(*$1));
        delete $1;
        $$->indices.swap($2->values);
        delete $2;
    };

// 基本表达式
PrimaryExp:
    LP Exp RP { $$ = $2; }|
    LVal { $$ = $1; }|
    Number { $$ = $1; }|
    BraceInitVal {
        $$ = make_node<AggregateExprAST>(unique_ptr<InitValAST>($1));
    };

// 数值
Number:
    INT {
        $$ = make_node<LiteralExprAST>($1);
    }|
    FLOAT {
        $$ = make_node<LiteralExprAST>($1);
    };

// 一元表达式
// Lane extracts on calls / parenthesized values are written as dedicated
// productions so they do not steal `id[i]` from LVal array lowering.
UnaryExp:
    PrimaryExp { $$ = $1; }|
    Call { $$ = $1; }|
    Call LB Exp RB {
        $$ = make_node<SubscriptExprAST>(unique_ptr<ExprAST>($1),
                                         unique_ptr<ExprAST>($3));
    }|
    LP Exp RP LB Exp RB {
        $$ = make_node<SubscriptExprAST>(unique_ptr<ExprAST>($2),
                                         unique_ptr<ExprAST>($5));
    }|
    UnaryOp UnaryExp {
        $$ = make_node<UnaryExprAST>($1, unique_ptr<ExprAST>($2));
    };

//函数调用
Call:
    ID LP RP {
        $$ = make_node<CallExprAST>(std::move(*$1), @1.first_line);
        delete $1;
    }|
    ID LP FuncCParamList RP {
        $$ = make_node<CallExprAST>(std::move(*$1), @1.first_line);
        delete $1;
        $$->arguments.swap(*$3);
        delete $3;
    };

// 单目运算符,这里可能与优先级相关，不删除该非终结符
UnaryOp:
    ADD {
        $$ = UnaryOp::Plus;
    }|
    MINUS {
        $$ = UnaryOp::Minus;
    }|
    NOT {
        $$ = UnaryOp::LogicalNot;
    };

// 函数实参表
FuncCParamList:
    FuncCParam {
        $$ = make_node<CallArgList>();
        $$->push_back(std::move(*$1));
        delete $1;
    }|
    FuncCParamList COMMA FuncCParam {
        $$ = $1;
        $$->push_back(std::move(*$3));
        delete $3;
    };

FuncCParam:
    Exp {
        $$ = make_node<CallArgumentAST>(unique_ptr<ExprAST>($1));
    }|
    STRING_LITERAL {
        string decoded;
        if (!decodeStringLiteral(*$1, decoded)) {
            yyerror("invalid escape sequence in string literal");
            delete $1;
            YYERROR;
        }
        $$ = make_node<CallArgumentAST>(std::move(decoded));
        delete $1;
    };

//乘除模表达式
MulExp:
    UnaryExp { $$ = $1; }|
    MulExp MUL UnaryExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Multiply,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    MulExp DIV UnaryExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Divide,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    MulExp MOD UnaryExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Remainder,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

// 加减表达式
AddExp:
    MulExp { $$ = $1; }|
    AddExp ADD MulExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Add,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    AddExp MINUS MulExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Subtract,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

// 关系表达式
RelExp:
    AddExp { $$ = $1; }|
    RelExp GTE AddExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::GreaterEqual,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    RelExp LTE AddExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::LessEqual,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    RelExp GT AddExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Greater,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    RelExp LT AddExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Less,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

// 相等性表达式
EqExp:
    RelExp { $$ = $1; }|
    EqExp EQ RelExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::Equal,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    }|
    EqExp NEQ RelExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::NotEqual,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

// 逻辑与表达式
LAndExp:
    EqExp { $$ = $1; }|
    LAndExp AND EqExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::LogicalAnd,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };

// 逻辑或表达式
LOrExp:
    LAndExp { $$ = $1; }|
    LOrExp OR LAndExp {
        $$ = make_node<BinaryExprAST>(BinaryOp::LogicalOr,
                                      unique_ptr<ExprAST>($1),
                                      unique_ptr<ExprAST>($3));
    };
%%

void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
