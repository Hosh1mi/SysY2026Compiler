# 前端实验文档

前端从 SysY 源文本构造 AST。实际链路比通常画的“lexer → parser → checker → IR”短：
当前 `main.cpp` 只执行 Flex、Bison 和 AST 到 IR 的 Visitor，`src/frontend/checker`
已经实现，但没有接入驱动。因此前端能把语法正确的输入继续交给 `GenIR`，并不表示
所有静态语义约束都已经在命令行编译流程中得到检查。

```text
.sy source
  └── lexer.l / yylex
        └── parser.y / yyparse
              └── global unique_ptr<CompUnitAST> root
                    └── GenIR visitor（src/mid/ir）
                          └── Module
```

本文只讨论 `src/frontend` 与 `src/include/frontend`。AST 降低为 IR 的具体 GEP、CFG、
短路逻辑和运行时函数声明在 [mid_ir.md](mid_ir.md) 中说明。

## 1. 构建与入口

[`CMakeLists.txt`](../CMakeLists.txt) 将两个源文件交给生成器：

```text
src/frontend/parser.y  -- Bison --> src/frontend/parser.cpp
                                    src/include/frontend/parser.hpp
src/frontend/lexer.l   -- Flex  --> src/frontend/lexer.cpp
```

`file(GLOB_RECURSE ...)` 会把生成后的 `.cpp` 和 `ast/`、`checker/` 下的普通实现文件
一同编入 `compiler`。生成文件已经在工作树中存在，但修改语法或词法规则时应改 `.y`/
`.l`，不要把手工修改留在 `parser.cpp`、`lexer.cpp`。

驱动在 [`src/main.cpp`](../src/main.cpp) 中：打开输入后设置全局 `yyin`，调用
`yyparse()`，再创建 `GenIR` 并对全局 `root` 调用 `accept`。`yyparse` 的返回值目前
没有被 main 检查；一旦错误恢复后 `root` 未建立，后续直接使用它会有风险。

| 文件 | 角色 |
| --- | --- |
| [`lexer.l`](../src/frontend/lexer.l) | 手写 Flex 规则 |
| [`parser.y`](../src/frontend/parser.y) | 手写 Bison 语法与 AST 动作 |
| [`lexer.cpp`](../src/frontend/lexer.cpp) | Flex 生成的扫描器 |
| [`parser.cpp`](../src/frontend/parser.cpp) | Bison 生成的 LALR 解析器 |
| [`parser.hpp`](../src/include/frontend/parser.hpp) | Bison 生成的 token/YYSTYPE/YYLTYPE 声明 |
| [`ast/`](../src/frontend/ast) | AST 分派和打印器 |
| [`checker/`](../src/frontend/checker) | 未接入的语义检查器 |

## 2. 词法实验

### 2.1 token 规则

扫描规则在 [`lexer.l`](../src/frontend/lexer.l)。整数由 `strtol(..., 0)` 转换，因此规则
接受十进制、`0x`/`0X` 十六进制和前导零八进制；浮点字面量接受小数点、十进制指数、
十六进制浮点和 `f/F/l/L` 后缀，再由 `strtof` 转换。

保留字只有：

```text
int float void const return if else while break continue
```

标识符是 `[A-Za-z_][A-Za-z0-9_]*`。比较运算符先匹配双字符版本，再匹配单字符版本；
逻辑 `&&`、`||` 也在 `&`、`|` 之外单独定义。词法器忽略 `//...` 和不跨越嵌套层级的
`/*...*/`，空白和换行不返回 token。

规则没有为单独的 `&`、`|`、字符字面量或字符串字面量定义 token。未知字符会用
`printf` 输出错误和当前 `yylineno`，然后继续扫描；这条错误信息走 stdout 而非 parser
的 stderr 路径。

### 2.2 位置记录

`%option yylineno` 让 Flex 维护行号。`YY_USER_ACTION` 在每一次匹配后写入 Bison 的
`yylloc`：

```text
first_line / last_line
first_column / last_column
```

`yycolumn` 从 1 开始；普通 token 按 `yyleng` 前进，换行规则把它重置为 1。注释和空白
同样触发 `YY_USER_ACTION`，因此后续 token 的列仍按源文本位置计算。

函数调用规则将 `ID` 的 `@1.first_line` 写入 `CallAST::lineno`。这是一个有实际用户的
AST 字段：`GenIR::visit(CallAST)` 把源级 `starttime()` 和 `stoptime()` 改名为
`_sysy_starttime`/`_sysy_stoptime`，并补入这个行号参数。

编译器以 `DEBUG_LEXER` 重新编译后，`LEXER_DEBUG_TOKEN` 会打印 token 名、原始文本和
行列范围。它是编译期开关，不是命令行 flag。

## 3. 语法与 AST 构造

### 3.1 Bison 接口

[`parser.y`](../src/frontend/parser.y) 使用 `%locations` 和 `%define parse.error verbose`。
语义值写在 `%union`：其中大部分是 AST 裸指针，token 则携带 `int`、`float` 或
`std::string*`。动作建立节点后立即放进 `unique_ptr` 成员或临时容器，顶层的 `Program`
把最终 CompUnit 移进全局：

```cpp
unique_ptr<CompUnitAST> root;
```

`make_node<T>()` 只是 `new T(...)` 的小包装，所有权转移由语法动作负责。写新产生式时，
最容易出错的地方不是分配本身，而是同一个裸指针被两个 `unique_ptr` 接管，或者错误路径
没有把临时节点交给任何所有者。

`yyerror` 按 `filename:line message` 输出。`initFileName` 已定义在 grammar 尾部，但当前
驱动没有调用它，所以命令行产生的 parser 错误在行号前通常没有文件名。这是当前接口的
实际状态，不应假定 filename 已自动初始化。

### 3.2 编译单元、声明和函数

文法入口 `Program` 只接受一个非空 `CompUnit`；`CompUnit` 由若干 `DeclDef` 组成。
`DeclDef` 在变量/常量声明与函数定义之间选择，AST 以两个互斥的 `unique_ptr` 字段保存
它们。

声明支持 `int`/`float`、可选 `const`、逗号分隔定义、数组维度和递归花括号初值。函数
支持 `int`、`float`、`void` 返回类型，标量参数、首维省略的数组参数和附加数组维度。
数组维度在 AST 中保存为 `vector<unique_ptr<AddExpAST>>`，它们仍是表达式而不是 parser
阶段已求值的整数。

`InitVal` 用一个节点统一标量初值、空 `{}` 与嵌套初始化列表。这里有一处需要注意的
当前实现细节：`InitValList: InitVal` 的 base action 将同一个 `$1` 指针连续 push 两次
到 `vector<unique_ptr<InitValAST>>`。因此单元素非空初始化列表不是安全的“单节点列表”
表示，可能造成重复所有权和析构问题。文档保留这一行为描述，是为了避免后续 AST/IR
改动依赖一个并不存在的规范列表不变式。

### 3.3 语句和悬挂 else

`Stmt` 覆盖空语句、赋值、表达式语句、`break`、`continue`、嵌套 block、return、if 和
while。语句种类记录在 `StmtAST::sType`，具体 payload 放在对应的 `unique_ptr` 字段中。

`SelectStmt` 使用：

```bison
%nonassoc LOWER_THEN_ELSE
%nonassoc ELSE
```

无 else 的 `if` 使用 `%prec LOWER_THEN_ELSE`，所以 `else` 归到最近的尚未匹配 if。
语法树不把 `else` 人工包装为一个空节点：没有 else 时 `SelectStmtAST::elseStmt` 为
null，GenIR 与 Checker 都按该空值分支。

### 3.4 表达式层次

表达式 node 沿文法层次保留，而不是在 parser 阶段折叠成一个通用 binary node：

```text
PrimaryExp → UnaryExp → MulExp → AddExp
           → RelExp → EqExp → LAndExp → LOrExp
```

每个二元层使用左递归，因而 `a-b-c`、`a/b/c`、`a<b<c` 会形成左结合的链。`UnaryExp`
额外容纳调用和前缀 `+`、`-`、`!`。赋值不在该表达式链内，只由 `Stmt: LVal ASSIGN Exp ;`
处理；这避免了把赋值当作可嵌套值表达式。

`%left` 声明为 parser 的冲突处理提供优先级，但主要的表达式结构已经由这些非终结符
固定。修改表达式规则时应同时检查 AST 的 left child/right child 字段和 GenIR visitor 的
递归方向，不能只改 precedence 表。

## 4. AST 接口

接口在 [`ast.hpp`](../src/include/frontend/ast/ast.hpp)，分派实现在
[`ast.cpp`](../src/frontend/ast/ast.cpp)。所有可访问节点继承 `BaseAST`，并实现：

```cpp
virtual void accept(Visitor &visitor) = 0;
```

`Visitor` 为每一种 AST 节点声明一个 `visit` 重载；`ast.cpp` 中的 `accept` 不含额外
逻辑，只做动态分派。`DefListAST`、`ArraysAST`、`InitValListAST`、`FuncFParamListAST` 和
`FuncCParamListAST` 是 parser 期间使用的容器，不继承 `BaseAST`，也没有 `accept`。

| 节点组 | 保存的主要信息 |
| --- | --- |
| `CompUnitAST`、`DeclDefAST`、`DeclAST`、`DefAST` | 顶层顺序、变量/函数分支、类型、const、名字、维度和初值 |
| `FuncDefAST`、`FuncFParamAST`、`BlockAST`、`BlockItemAST` | 函数签名、数组形参、块项顺序 |
| `StmtAST`、`ReturnStmtAST`、`SelectStmtAST`、`IterationStmtAST` | 语句种类、条件、分支、循环体和返回值 |
| `LValAST`、`CallAST`、`NumberAST` | 名字、下标、实参、源行号和字面量 |
| `PrimaryExpAST` 至 `LOrExpAST` | 表达式层次与运算符枚举 |

`TYPE`、`STYPE`、`AOP`、`MOP`、`UOP`、`ROP`、`EOP` 是 AST 的语法级枚举。它们没有
承载 IR 类型推断结果；例如比较节点在 AST 中只记录比较运算符，最终产生 i1 还是作为
控制流条件使用由 GenIR 决定。

### 4.1 AST 打印器

[`astPrinter.hpp`](../src/include/frontend/ast/astPrinter.hpp) 和
[`astPrinter.cpp`](../src/frontend/ast/astPrinter.cpp) 提供 `Printer`。它不是 `Visitor`
子类，而是一组同名 `visit` 重载，递归返回缩进后的字符串。它会显示节点类型、标识符、
字面量和操作符，适合单独在调试代码中调用：

```cpp
Printer printer;
std::cerr << printer.visit(*root);
```

当前 `main.cpp` 没有创建 `Printer`，也没有 `DEBUG_PARSER` 的运行时代码路径；不要把它
当成默认编译输出。

## 5. Checker：实现存在，但驱动不调用

[`checker.hpp`](../src/include/frontend/checker/checker.hpp)、
[`checker.cpp`](../src/frontend/checker/checker.cpp) 和
[`errReporter.hpp`](../src/include/frontend/checker/errReporter.hpp) 实现了另一套 AST
Visitor。它维护 `list<map<string, Entry>>` 作用域栈；`Entry` 同时描述变量/函数、标量/
数组、数组维度和函数参数。

该 Visitor 会尝试检查：

- 变量、形参与函数重定义；
- 未定义变量和未定义函数；
- 实参与形参数量/基本类型不匹配；
- return 类型、数组下标整数性、非数组下标访问；
- `break`/`continue` 是否位于 while 内。

错误种类由 `ErrorType` 编码，`ErrorReporter` 按英文文本打印；多数 Checker 路径在报告
后直接 `exit`。`BlockAST::is_inloop`、`BlockItemAST::is_inloop` 等字段就是为了这个
Visitor 传递 while 上下文，GenIR 不依赖这些 checker 标志。

不过，当前 `main.cpp` 仅调用 `yyparse()` 和 `GenIR`，没有构造 `ErrorReporter` 或
`Checker`。因此上述检查不是命令行编译器的有效前置条件。接入 Checker 前也应复查其
特殊运行时函数名单与 GenIR 预注册函数集合是否一致，否则会出现 parser 接受、Checker
拒绝或相反的情况。

## 6. 前端与 GenIR 的交界

`GenIR` 在 [`irGen.hpp`](../src/include/mid/ir/irGen.hpp) 中继承前端 `Visitor`，实现在
[`irGen.cpp`](../src/mid/ir/irGen.cpp)。它在构造时创建 Module、IR builder、顶层 Scope
并预注册 SysY I/O 函数以及计时函数。然后从 `CompUnitAST` 递归生成全局变量、函数、
基本块、算术、比较、短路逻辑和调用。

前端交给 GenIR 的约束主要是：

1. 所有 AST 子节点的 `unique_ptr` 所有权必须唯一；
2. `DeclDefAST`、`StmtAST`、`PrimaryExpAST` 等互斥分支必须只设置一个有效 payload；
3. `CallAST::lineno` 必须保留 lexer location；
4. 表达式左递归链的方向不能改变，否则 GenIR 的递归访问会改变源级求值顺序；
5. 语义不明确的输入不能指望 Checker 自动拦住，因为它没有接入。

## 7. 调试和回归建议

词法/语法问题先使用一个最小 `.sy` 文件缩小范围：先只保留 declaration，再加入
expression、block、if/while 和 call。对 AST 所有权问题，优先测试单元素 `{...}` 初始化
和嵌套初值，因为它们会经过 `InitValList` 的 unique_ptr 转移路径。

常用检查命令：

```bash
cmake --build build -j4
build/compiler -O0 -c input.sy -o /tmp/input.ir
```

前端本身不依赖 `-O1`；语法和 AST 改动应首先用 `-O0` 观察未经中端改写的 IR。若改动
影响 `GenIR` 的 visitor 契约，再运行功能测试；只改本文档则无需运行性能测试。

## 8. 文件索引

### 手写输入与接口

- [`src/frontend/lexer.l`](../src/frontend/lexer.l)
- [`src/frontend/parser.y`](../src/frontend/parser.y)
- [`src/include/frontend/parser.hpp`](../src/include/frontend/parser.hpp)
- [`src/include/frontend/ast/ast.hpp`](../src/include/frontend/ast/ast.hpp)
- [`src/include/frontend/ast/astPrinter.hpp`](../src/include/frontend/ast/astPrinter.hpp)
- [`src/include/frontend/checker/checker.hpp`](../src/include/frontend/checker/checker.hpp)
- [`src/include/frontend/checker/errReporter.hpp`](../src/include/frontend/checker/errReporter.hpp)

### 实现与生成文件

- [`src/frontend/ast/ast.cpp`](../src/frontend/ast/ast.cpp)
- [`src/frontend/ast/astPrinter.cpp`](../src/frontend/ast/astPrinter.cpp)
- [`src/frontend/checker/checker.cpp`](../src/frontend/checker/checker.cpp)
- [`src/frontend/checker/errReporter.cpp`](../src/frontend/checker/errReporter.cpp)
- [`src/frontend/lexer.cpp`](../src/frontend/lexer.cpp)（Flex 生成）
- [`src/frontend/parser.cpp`](../src/frontend/parser.cpp)（Bison 生成）
