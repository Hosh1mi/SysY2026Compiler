%define parse.error verbose
%locations

%code requires {
  #include "../include/ast/ast.hpp"
  #include <memory>
  #include <string>
}

%{
  #include <cstdio>
  #include <cstdlib>
  #include <iostream>
  #include <memory>
  #include <string>
  #include <utility>
  #include "../include/ast/ast.hpp"
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
%}

%type <compUnit> CompUnit
%type <declDef> DeclDef
%type <decl> Decl
%type <defList> DefList
%type <def> Def
%type <arrays> Arrays
%type <initValList> InitValList
%type <initVal> InitVal
%type <funcDef> FuncDef
%type <FuncFParamList> FuncFParamList
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
%type <funcCParamList> FuncCParamList
%type <mulExp> MulExp
%type <addExp> AddExp Exp
%type <relExp> RelExp
%type <eqExp> EqExp
%type <lAndExp> LAndExp
%type <lOrExp> Cond LOrExp

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
  FuncCParamListAST* funcCParamList;
  MulExpAST* mulExp;
  AddExpAST* addExp;
  RelExpAST* relExp;
  EqExpAST* eqExp;
  LAndExpAST* lAndExp;
  LOrExpAST* lOrExp;
  TYPE ty;
  UOP op;
  string* token;
  int int_val;
  float float_val;
}

%type <ty> BType VoidType
%type <op> UnaryOp

// %token 定义终结符的语义值类型
%token <int_val> INT           // 指定INT字面量的语义值是type_int，有词法分析得到的数值
%token <float_val> FLOAT       // 指定FLOAT字面量的语义值是type_float，有词法分析得到的数值
%token <token> ID              // 指定ID
%token GTE LTE GT LT EQ NEQ    // 关系运算
%token INTTYPE FLOATTYPE VOID  // 数据类型
%token CONST RETURN IF ELSE WHILE BREAK CONTINUE
%token LP RP LB RB LC RC COMMA SEMICOLON
// 用bison对该文件编译时，带参数-d，生成的exp.tab.h中给这些单词进行编码，可在lex.l中包含parser.tab.h使用这些单词种类码
%token NOT ASSIGN MINUS ADD MUL DIV MOD AND OR
// Unused tokens
/* %token POS NEG */

%left ASSIGN
%left OR AND
%left EQ NEQ
%left GTE LTE GT LT
%left ADD MINUS
%left MOD MUL DIV
%right NOT


%nonassoc LOWER_THEN_ELSE
%nonassoc ELSE

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
  CONST BType DefList SEMICOLON {
    $$ = make_node<DeclAST>();
    $$->isConst = true;
    $$->bType = $2;
    $$->defList.swap($3->list);
  }|
  BType DefList SEMICOLON {
    $$ = make_node<DeclAST>();
    $$->isConst = false;
    $$->bType = $1;
    $$->defList.swap($2->list);
  };

// 基本类型
BType:
  INTTYPE {
    $$ = TYPE_INT;
  }|
  FLOATTYPE {
    $$ = TYPE_FLOAT;
  };

// 空类型
VoidType:
  VOID {
    $$ = TYPE_VOID;
  };

// 定义列表
DefList:
  Def {
    $$ = make_node<DefListAST>();
    $$->list.push_back(unique_ptr<DefAST>($1));
  }|
  DefList COMMA Def {
    $$ = $1;
    $$->list.push_back(unique_ptr<DefAST>($3));
  };

// 定义
Def:
  ID Arrays ASSIGN InitVal {
    $$ = make_node<DefAST>();
    $$->id = unique_ptr<string>($1);
    $$->arrays.swap($2->list);
    $$->initVal = unique_ptr<InitValAST>($4);
  }|
  ID ASSIGN InitVal {
    $$ = make_node<DefAST>();
    $$->id = unique_ptr<string>($1);
    $$->initVal = unique_ptr<InitValAST>($3);
  }|
  ID Arrays {
    $$ = make_node<DefAST>();
    $$->id = unique_ptr<string>($1);
    $$->arrays.swap($2->list);
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


// 变量或常量初值
InitVal:
  Exp {
    $$ = make_node<InitValAST>();
    $$->exp = unique_ptr<AddExpAST>($1);
  }|
  LC RC {
    $$ = make_node<InitValAST>();
  }|
  LC InitValList RC {
    $$ = make_node<InitValAST>();
    $$->initValList.swap($2->list);
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
  BType ID LP FuncFParamList RP Block {
    $$ = make_node<FuncDefAST>();
    $$->funcType = $1;
    $$->id = unique_ptr<string>($2);
    $$->funcFParamList.swap($4->list);
    $$->block = unique_ptr<BlockAST>($6);
  }|
  BType ID LP RP Block {
    $$ = make_node<FuncDefAST>();
    $$->funcType = $1;
    $$->id = unique_ptr<string>($2);
    $$->block = unique_ptr<BlockAST>($5);
  }|
  VoidType ID LP FuncFParamList RP Block {
    $$ = make_node<FuncDefAST>();
    $$->funcType = $1;
    $$->id = unique_ptr<string>($2);
    $$->funcFParamList.swap($4->list);
    $$->block = unique_ptr<BlockAST>($6);
  }|
  VoidType ID LP RP Block {
    $$ = make_node<FuncDefAST>();
    $$->funcType = $1;
    $$->id = unique_ptr<string>($2);
    $$->block = unique_ptr<BlockAST>($5);
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
    $$->bType = $1;
    $$->id = unique_ptr<string>($2);
    $$->isArray = false;
  }|
  BType ID LB RB {
    $$ = make_node<FuncFParamAST>();
    $$->bType = $1;
    $$->id = unique_ptr<string>($2);
    $$->isArray = true;
  }|
  BType ID LB RB Arrays {
    $$ = make_node<FuncFParamAST>();
    $$->bType = $1;
    $$->id = unique_ptr<string>($2);
    $$->isArray = true;
    $$->arrays.swap($5->list);
  };

// 语句块
Block:
  LC RC {
    $$ = make_node<BlockAST>();
  }|
  LC BlockItemList RC {
    $$ = make_node<BlockAST>();
    $$->blockItemList.swap($2->list);
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
  Exp SEMICOLON {
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
UnaryExp:
  PrimaryExp {
    $$ = make_node<UnaryExpAST>();
    $$->primaryExp = unique_ptr<PrimaryExpAST>($1);
  }|
  Call {
    $$ = make_node<UnaryExpAST>();
    $$->call = unique_ptr<CallAST>($1);
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
  }|
  ID LP FuncCParamList RP {
    $$ = make_node<CallAST>();
    $$->id = unique_ptr<string>($1);
    $$->funcCParamList.swap($3->list);
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
  Exp {
    $$ = make_node<FuncCParamListAST>();
    $$->list.push_back(unique_ptr<AddExpAST>($1));
  }|
  FuncCParamList COMMA Exp {
    $$ = (FuncCParamListAST*) $1;
    $$->list.push_back(unique_ptr<AddExpAST>($3));
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
