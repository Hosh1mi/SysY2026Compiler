%define parse.error verbose
%define api.header.include {"../include/frontend/parser.hpp"}
%locations

%code requires {
    #include "ast/ast.hpp"
    #include <string>
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
    DeclDefAST* declDef;
    DeclAST* decl;
    DefListAST* defList;
    DefAST* def;
    ArraysAST* arrays;
    InitValListAST* initValList;
    InitValAST* initVal;
    FuncDefAST* funcDef;
    FuncFParamListAST* FuncFParamList;
    FuncFParamAST* funcFParam;
    BlockAST* block;
    BlockItemListAST* blockItemList;
    BlockItemAST* blockItem;
    StmtAST* stmt;
    ReturnStmtAST* returnStmt;
    SelectStmtAST* selectStmt;
    IterationStmtAST* iterationStmt;
    LValAST* lVal;
    PrimaryExpAST* primaryExp;
    NumberAST* number;
    UnaryExpAST* unaryExp;
    CallAST* call;
    CallArgAST* callArg;
    FuncCParamListAST* funcCParamList;
    MulExpAST* mulExp;
    AddExpAST* addExp;
    RelExpAST* relExp;
    EqExpAST* eqExp;
    LAndExpAST* lAndExp;
    LOrExpAST* lOrExp;
    TypeSpec* type_spec;
    UOP op;
    string* token;
    int int_val;
    float float_val;
}

%type <compUnit> CompUnit
%type <declDef> DeclDef
%type <decl> Decl
%type <defList> ConstDefList VarDefList
%type <def> ConstDef VarDef
%type <arrays> Arrays
%type <initValList> InitValList
%type <initVal> ConstInitVal InitVal BraceInitVal
%type <funcDef> FuncDef
%type <FuncFParamList> FuncFParamList OptFuncFParamList
%type <funcFParam> FuncFParam
%type <block> Block
%type <blockItemList> BlockItemList
%type <blockItem> BlockItem
%type <stmt> Stmt
%type <returnStmt> ReturnStmt
%type <selectStmt> SelectStmt
%type <iterationStmt> IterationStmt
%type <lVal> LVal
%type <primaryExp> PrimaryExp
%type <number> Number
%type <unaryExp> UnaryExp
%type <call> Call
%type <callArg> FuncCParam
%type <funcCParamList> FuncCParamList
%type <mulExp> MulExp
%type <addExp> AddExp Exp NonBraceExp NonBraceAddExp
%type <mulExp> NonBraceMulExp
%type <unaryExp> NonBraceUnaryExp
%type <relExp> RelExp
%type <eqExp> EqExp
%type <lAndExp> LAndExp
%type <lOrExp> Cond LOrExp

%type <type_spec> BType VoidType VecType VecWidth
%type <op> UnaryOp

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
%destructor { delete $$; } <compUnit> <declDef> <decl> <defList> <def>
%destructor { delete $$; } <arrays> <initValList> <initVal> <funcDef>
%destructor { delete $$; } <FuncFParamList> <funcFParam> <block>
%destructor { delete $$; } <blockItemList> <blockItem> <stmt> <returnStmt>
%destructor { delete $$; } <selectStmt> <iterationStmt> <lVal> <primaryExp>
%destructor { delete $$; } <number> <unaryExp> <call> <callArg>
%destructor { delete $$; } <funcCParamList> <mulExp> <addExp> <relExp>
%destructor { delete $$; } <eqExp> <lAndExp> <lOrExp> <type_spec> <token>

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
        $$->declDefList.push_back(unique_ptr<DeclDefAST>($2));
    }|
    DeclDef {
        $$ = make_node<CompUnitAST>();
        $$->declDefList.push_back(unique_ptr<DeclDefAST>($1));
    };

//声明或者函数定义
DeclDef:
    Decl {
        $$ = make_node<DeclDefAST>();
        $$->Decl = unique_ptr<DeclAST>($1);
    }|
    FuncDef {
        $$ = make_node<DeclDefAST>();
        $$->funcDef = unique_ptr<FuncDefAST>($1);
    };

// 变量或常量声明
Decl:
    CONST BType ConstDefList SEMICOLON {
        $$ = make_node<DeclAST>();
        $$->isConst = true;
        $$->bType = *$2;
        delete $2;
        $$->defList.swap($3->list);
        delete $3;
    }|
    BType VarDefList SEMICOLON {
        $$ = make_node<DeclAST>();
        $$->isConst = false;
        $$->bType = *$1;
        delete $1;
        $$->defList.swap($2->list);
        delete $2;
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
        $$ = make_node<DefListAST>();
        $$->list.push_back(unique_ptr<DefAST>($1));
    }|
    ConstDefList COMMA ConstDef {
        $$ = $1;
        $$->list.push_back(unique_ptr<DefAST>($3));
    };

VarDefList:
    VarDef {
        $$ = make_node<DefListAST>();
        $$->list.push_back(unique_ptr<DefAST>($1));
    }|
    VarDefList COMMA VarDef {
        $$ = $1;
        $$->list.push_back(unique_ptr<DefAST>($3));
    };

// Constants and variables deliberately have separate productions.  Besides
// matching the official grammar, this makes an uninitialized const impossible
// to represent in the AST.
ConstDef:
    ID Arrays ASSIGN ConstInitVal {
        $$ = make_node<DefAST>();
        $$->id = unique_ptr<string>($1);
        $$->arrays.swap($2->list);
        delete $2;
        $$->initVal = unique_ptr<InitValAST>($4);
    }|
    ID ASSIGN Exp {
        $$ = make_node<DefAST>();
        $$->id = unique_ptr<string>($1);
        $$->initVal = unique_ptr<InitValAST>(make_node<InitValAST>());
        $$->initVal->exp = unique_ptr<AddExpAST>($3);
    };

VarDef:
    ID Arrays ASSIGN InitVal {
        $$ = make_node<DefAST>();
        $$->id = unique_ptr<string>($1);
        $$->arrays.swap($2->list);
        delete $2;
        $$->initVal = unique_ptr<InitValAST>($4);
    }|
    ID ASSIGN Exp {
        $$ = make_node<DefAST>();
        $$->id = unique_ptr<string>($1);
        $$->initVal = unique_ptr<InitValAST>(make_node<InitValAST>());
        $$->initVal->exp = unique_ptr<AddExpAST>($3);
    }|
    ID Arrays {
        $$ = make_node<DefAST>();
        $$->id = unique_ptr<string>($1);
        $$->arrays.swap($2->list);
        delete $2;
    }|
    ID {
        $$ = make_node<DefAST>();
        $$->id = unique_ptr<string>($1);
    };

// 数组
Arrays:
    LB Exp RB {
        $$ = make_node<ArraysAST>();
        $$->list.push_back(unique_ptr<AddExpAST>($2));
    }|
    Arrays LB Exp RB {
        $$ = $1;
        $$->list.push_back(unique_ptr<AddExpAST>($3));
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
        $$->blockItemList.swap($2->list);
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
        $$ = make_node<InitValAST>();
        $$->initValList.swap($2->list);
        delete $2;
    }|
    NonBraceExp {
        $$ = make_node<InitValAST>();
        $$->exp = unique_ptr<AddExpAST>($1);
    };

ConstInitVal:
    LC RC {
        $$ = make_node<InitValAST>();
    }|
    LC InitValList RC {
        $$ = make_node<InitValAST>();
        $$->initValList.swap($2->list);
        delete $2;
    }|
    NonBraceExp {
        $$ = make_node<InitValAST>();
        $$->exp = unique_ptr<AddExpAST>($1);
    };

// Braced initializer used on the right-hand side of a whole-vector assignment.
BraceInitVal:
    LC RC {
        $$ = make_node<InitValAST>();
    }|
    LC InitValList RC {
        $$ = make_node<InitValAST>();
        $$->initValList.swap($2->list);
        delete $2;
    };

// 变量列表
InitValList:
  InitValList COMMA InitVal {
    $$ = $1;
    $$->list.push_back(unique_ptr<InitValAST>($3));
  }|
  InitVal {
    $$ = make_node<InitValListAST>();
    $$->list.push_back(unique_ptr<InitValAST>($1));
  };

// 函数定义
FuncDef:
    BType ID LP OptFuncFParamList RP Block {
        $$ = make_node<FuncDefAST>();
        $$->funcType = *$1;
        delete $1;
        $$->id = unique_ptr<string>($2);
        $$->funcFParamList.swap($4->list);
        delete $4;
        $$->block = unique_ptr<BlockAST>($6);
    }|
    VoidType ID LP OptFuncFParamList RP Block {
        $$ = make_node<FuncDefAST>();
        $$->funcType = *$1;
        delete $1;
        $$->id = unique_ptr<string>($2);
        $$->funcFParamList.swap($4->list);
        delete $4;
        $$->block = unique_ptr<BlockAST>($6);
    };

OptFuncFParamList:
    %empty {
        $$ = make_node<FuncFParamListAST>();
    }|
    FuncFParamList {
        $$ = $1;
    };

// 函数形参列表
FuncFParamList:
    FuncFParam {
        $$ = make_node<FuncFParamListAST>();
        $$->list.push_back(unique_ptr<FuncFParamAST>($1));
    }|
    FuncFParamList COMMA FuncFParam {
        $$ = $1;
        $$->list.push_back(unique_ptr<FuncFParamAST>($3));
    };

// 函数形参
FuncFParam:
    BType ID {
        $$ = make_node<FuncFParamAST>();
        $$->bType = *$1;
        delete $1;
        $$->id = unique_ptr<string>($2);
        $$->isArray = false;
    }|
    BType ID LB RB {
        $$ = make_node<FuncFParamAST>();
        $$->bType = *$1;
        delete $1;
        $$->id = unique_ptr<string>($2);
        $$->isArray = true;
    }|
    BType ID LB RB Arrays {
        $$ = make_node<FuncFParamAST>();
        $$->bType = *$1;
        delete $1;
        $$->id = unique_ptr<string>($2);
        $$->isArray = true;
        $$->arrays.swap($5->list);
        delete $5;
    };

// 语句块项列表
BlockItemList:
    BlockItem {
        $$ = make_node<BlockItemListAST>();
        $$->list.push_back(unique_ptr<BlockItemAST>($1));
    }|
    BlockItemList BlockItem {
        $$ = $1;
        $$->list.push_back(unique_ptr<BlockItemAST>($2));
    };

// 语句块项
BlockItem:
    Decl {
        $$ = make_node<BlockItemAST>();
        $$->decl = unique_ptr<DeclAST>($1);
    }|
    Stmt {
        $$ = make_node<BlockItemAST>();
        $$->stmt = unique_ptr<StmtAST>($1);
    };

// 语句，根据type判断是何种类型的Stmt
Stmt:
    SEMICOLON {
        $$ = make_node<StmtAST>();
        $$->sType = SEMI;
    }|
    LVal ASSIGN Exp SEMICOLON {
        $$ = make_node<StmtAST>();
        $$->sType = ASS;
        $$->lVal = unique_ptr<LValAST>($1);
        $$->exp = unique_ptr<AddExpAST>($3);
    }|
    NonBraceExp SEMICOLON {
        $$ = make_node<StmtAST>();
        $$->sType = EXP;
        $$->exp = unique_ptr<AddExpAST>($1);
    }|
    CONTINUE SEMICOLON {
        $$ = make_node<StmtAST>();
        $$->sType = CONT;
    }|
    BREAK SEMICOLON {
        $$ = make_node<StmtAST>();
        $$->sType = BRE;
    }|
    Block {
        $$ = make_node<StmtAST>();
        $$->sType = BLK;
        $$->block = unique_ptr<BlockAST>($1);
    }|
    ReturnStmt {
        $$ = make_node<StmtAST>();
        $$->sType = RET;
        $$->returnStmt = unique_ptr<ReturnStmtAST>($1);
    }|
    SelectStmt {
        $$ = make_node<StmtAST>();
        $$->sType = SEL;
        $$->selectStmt = unique_ptr<SelectStmtAST>($1);
    }|
    IterationStmt {
        $$ = make_node<StmtAST>();
        $$->sType = ITER;
        $$->iterationStmt = unique_ptr<IterationStmtAST>($1);
    };

//选择语句
SelectStmt:
    IF LP Cond RP Stmt %prec LOWER_THEN_ELSE {
        $$ = make_node<SelectStmtAST>();
        $$->cond = unique_ptr<LOrExpAST>($3);
        $$->ifStmt = unique_ptr<StmtAST>($5);
    }|
    IF LP Cond RP Stmt ELSE Stmt {
        $$ = make_node<SelectStmtAST>();
        $$->cond = unique_ptr<LOrExpAST>($3);
        $$->ifStmt = unique_ptr<StmtAST>($5);
        $$->elseStmt = unique_ptr<StmtAST>($7);
    };

//循环语句
IterationStmt:
    WHILE LP Cond RP Stmt {
        $$ = make_node<IterationStmtAST>();
        $$->cond = unique_ptr<LOrExpAST>($3);
        $$->stmt = unique_ptr<StmtAST>($5);
    };

//返回语句
ReturnStmt:
    RETURN Exp SEMICOLON {
        $$ = make_node<ReturnStmtAST>();
        $$->exp = unique_ptr<AddExpAST>($2);
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
    NonBraceMulExp {
        $$ = make_node<AddExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
    }|
    NonBraceAddExp ADD MulExp {
        $$ = make_node<AddExpAST>();
        $$->addExp = unique_ptr<AddExpAST>($1);
        $$->op = AOP_ADD;
        $$->mulExp = unique_ptr<MulExpAST>($3);
    }|
    NonBraceAddExp MINUS MulExp {
        $$ = make_node<AddExpAST>();
        $$->addExp = unique_ptr<AddExpAST>($1);
        $$->op = AOP_MINUS;
        $$->mulExp = unique_ptr<MulExpAST>($3);
    };

NonBraceMulExp:
    NonBraceUnaryExp {
        $$ = make_node<MulExpAST>();
        $$->unaryExp = unique_ptr<UnaryExpAST>($1);
    }|
    NonBraceMulExp MUL UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
        $$->op = MOP_MUL;
        $$->unaryExp = unique_ptr<UnaryExpAST>($3);
    }|
    NonBraceMulExp DIV UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
        $$->op = MOP_DIV;
        $$->unaryExp = unique_ptr<UnaryExpAST>($3);
    }|
    NonBraceMulExp MOD UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
        $$->op = MOP_MOD;
        $$->unaryExp = unique_ptr<UnaryExpAST>($3);
    };

NonBraceUnaryExp:
    LP Exp RP {
        $$ = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>($2);
        $$->primaryExp = unique_ptr<PrimaryExpAST>(primary);
    }|
    LVal {
        $$ = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->lval = unique_ptr<LValAST>($1);
        $$->primaryExp = unique_ptr<PrimaryExpAST>(primary);
    }|
    Number {
        $$ = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->number = unique_ptr<NumberAST>($1);
        $$->primaryExp = unique_ptr<PrimaryExpAST>(primary);
    }|
    Call {
        $$ = make_node<UnaryExpAST>();
        $$->call = unique_ptr<CallAST>($1);
    }|
    Call LB Exp RB {
        $$ = make_node<UnaryExpAST>();
        auto *base = make_node<UnaryExpAST>();
        base->call = unique_ptr<CallAST>($1);
        $$->unaryExp = unique_ptr<UnaryExpAST>(base);
        $$->subscript = unique_ptr<AddExpAST>($3);
    }|
    LP Exp RP LB Exp RB {
        $$ = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>($2);
        auto *base = make_node<UnaryExpAST>();
        base->primaryExp = unique_ptr<PrimaryExpAST>(primary);
        $$->unaryExp = unique_ptr<UnaryExpAST>(base);
        $$->subscript = unique_ptr<AddExpAST>($5);
    }|
    UnaryOp UnaryExp {
        $$ = make_node<UnaryExpAST>();
        $$->op = $1;
        $$->unaryExp = unique_ptr<UnaryExpAST>($2);
    };

// 条件表达式
Cond:
    LOrExp {
        $$ = $1;
    };

// 左值表达式
LVal:
    ID {
        $$ = make_node<LValAST>();
        $$->id = unique_ptr<string>($1);
    }|
    ID Arrays {
        $$ = make_node<LValAST>();
        $$->id = unique_ptr<string>($1);
        $$->arrays.swap($2->list);
        delete $2;
    };

// 基本表达式
PrimaryExp:
    LP Exp RP {
        $$ = make_node<PrimaryExpAST>();
        $$->exp = unique_ptr<AddExpAST>($2);
    }|
    LVal {
        $$ = make_node<PrimaryExpAST>();
        $$->lval = unique_ptr<LValAST>($1);
    }|
    Number {
        $$ = make_node<PrimaryExpAST>();
        $$->number = unique_ptr<NumberAST>($1);
    }|
    BraceInitVal {
        $$ = make_node<PrimaryExpAST>();
        $$->initVal = unique_ptr<InitValAST>($1);
    };

// 数值
Number:
    INT {
        $$ = make_node<NumberAST>();
        $$->isInt = true;
        $$->intval = $1;
    }|
    FLOAT {
        $$ = make_node<NumberAST>();
        $$->isInt = false;
        $$->floatval = $1;
    };

// 一元表达式
// Lane extracts on calls / parenthesized values are written as dedicated
// productions so they do not steal `id[i]` from LVal array lowering.
UnaryExp:
    PrimaryExp {
        $$ = make_node<UnaryExpAST>();
        $$->primaryExp = unique_ptr<PrimaryExpAST>($1);
    }|
    Call {
        $$ = make_node<UnaryExpAST>();
        $$->call = unique_ptr<CallAST>($1);
    }|
    Call LB Exp RB {
        $$ = make_node<UnaryExpAST>();
        auto *base = make_node<UnaryExpAST>();
        base->call = unique_ptr<CallAST>($1);
        $$->unaryExp = unique_ptr<UnaryExpAST>(base);
        $$->subscript = unique_ptr<AddExpAST>($3);
    }|
    LP Exp RP LB Exp RB {
        $$ = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>($2);
        auto *base = make_node<UnaryExpAST>();
        base->primaryExp = unique_ptr<PrimaryExpAST>(primary);
        $$->unaryExp = unique_ptr<UnaryExpAST>(base);
        $$->subscript = unique_ptr<AddExpAST>($5);
    }|
    UnaryOp UnaryExp {
        $$ = make_node<UnaryExpAST>();
        $$->op = $1;
        $$->unaryExp = unique_ptr<UnaryExpAST>($2);
    };

//函数调用
Call:
    ID LP RP {
        $$ = make_node<CallAST>();
        $$->id = unique_ptr<string>($1);
        $$->lineno = @1.first_line;
    }|
    ID LP FuncCParamList RP {
        $$ = make_node<CallAST>();
        $$->id = unique_ptr<string>($1);
        $$->funcCParamList.swap($3->list);
        delete $3;
        $$->lineno = @1.first_line;
    };

// 单目运算符,这里可能与优先级相关，不删除该非终结符
UnaryOp:
    ADD {
        $$ = UOP_ADD;
    }|
    MINUS {
        $$ = UOP_MINUS;
    }|
    NOT {
        $$ = UOP_NOT;
    };

// 函数实参表
FuncCParamList:
    FuncCParam {
        $$ = make_node<FuncCParamListAST>();
        $$->list.push_back(unique_ptr<CallArgAST>($1));
    }|
    FuncCParamList COMMA FuncCParam {
        $$ = $1;
        $$->list.push_back(unique_ptr<CallArgAST>($3));
    };

FuncCParam:
    Exp {
        $$ = make_node<CallArgAST>();
        $$->exp = unique_ptr<AddExpAST>($1);
    }|
    STRING_LITERAL {
        string decoded;
        if (!decodeStringLiteral(*$1, decoded)) {
            yyerror("invalid escape sequence in string literal");
            delete $1;
            YYERROR;
        }
        $$ = make_node<CallArgAST>();
        $$->stringLiteral = unique_ptr<string>(new string(std::move(decoded)));
        delete $1;
    };

//乘除模表达式
MulExp:
    UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->unaryExp = unique_ptr<UnaryExpAST>($1);
    }|
    MulExp MUL UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
        $$->op = MOP_MUL;
        $$->unaryExp = unique_ptr<UnaryExpAST>($3);
    }|
    MulExp DIV UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
        $$->op = MOP_DIV;
        $$->unaryExp = unique_ptr<UnaryExpAST>($3);
    }|
    MulExp MOD UnaryExp {
        $$ = make_node<MulExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
        $$->op = MOP_MOD;
        $$->unaryExp = unique_ptr<UnaryExpAST>($3);
    };

// 加减表达式
AddExp:
    MulExp {
        $$ = make_node<AddExpAST>();
        $$->mulExp = unique_ptr<MulExpAST>($1);
    }|
    AddExp ADD MulExp {
        $$ = make_node<AddExpAST>();
        $$->addExp = unique_ptr<AddExpAST>($1);
        $$->op = AOP_ADD;
        $$->mulExp = unique_ptr<MulExpAST>($3);
    }|
    AddExp MINUS MulExp {
        $$ = make_node<AddExpAST>();
        $$->addExp = unique_ptr<AddExpAST>($1);
        $$->op = AOP_MINUS;
        $$->mulExp = unique_ptr<MulExpAST>($3);
    };

// 关系表达式
RelExp:
    AddExp {
        $$ = make_node<RelExpAST>();
        $$->addExp = unique_ptr<AddExpAST>($1);
    }|
    RelExp GTE AddExp {
        $$ = make_node<RelExpAST>();
        $$->relExp = unique_ptr<RelExpAST>($1);
        $$->op = ROP_GTE;
        $$->addExp = unique_ptr<AddExpAST>($3);
    }|
    RelExp LTE AddExp {
        $$ = make_node<RelExpAST>();
        $$->relExp = unique_ptr<RelExpAST>($1);
        $$->op = ROP_LTE;
        $$->addExp = unique_ptr<AddExpAST>($3);
    }|
    RelExp GT AddExp {
        $$ = make_node<RelExpAST>();
        $$->relExp = unique_ptr<RelExpAST>($1);
        $$->op = ROP_GT;
        $$->addExp = unique_ptr<AddExpAST>($3);
    }|
    RelExp LT AddExp {
        $$ = make_node<RelExpAST>();
        $$->relExp = unique_ptr<RelExpAST>($1);
        $$->op = ROP_LT;
        $$->addExp = unique_ptr<AddExpAST>($3);
    };

// 相等性表达式
EqExp:
    RelExp {
        $$ = make_node<EqExpAST>();
        $$->relExp = unique_ptr<RelExpAST>($1);
    }|
    EqExp EQ RelExp {
        $$ = make_node<EqExpAST>();
        $$->eqExp = unique_ptr<EqExpAST>($1);
        $$->op = EOP_EQ;
        $$->relExp = unique_ptr<RelExpAST>($3);
    }|
    EqExp NEQ RelExp {
        $$ = make_node<EqExpAST>();
        $$->eqExp = unique_ptr<EqExpAST>($1);
        $$->op = EOP_NEQ;
        $$->relExp = unique_ptr<RelExpAST>($3);
    };

// 逻辑与表达式
LAndExp:
    EqExp {
        $$ = make_node<LAndExpAST>();
        $$->eqExp = unique_ptr<EqExpAST>($1);
    }|
    LAndExp AND EqExp {
        $$ = make_node<LAndExpAST>();
        $$->lAndExp = unique_ptr<LAndExpAST>($1);
        $$->eqExp = unique_ptr<EqExpAST>($3);
    };

// 逻辑或表达式
LOrExp:
    LAndExp {
        $$ = make_node<LOrExpAST>();
        $$->lAndExp = unique_ptr<LAndExpAST>($1);
    }|
    LOrExp OR LAndExp {
        $$ = make_node<LOrExpAST>();
        $$->lOrExp = unique_ptr<LOrExpAST>($1);
        $$->lAndExp = unique_ptr<LAndExpAST>($3);
    };
%%

void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
