# SysY Language and Runtime Definition

This document consolidates the SysY language definition, including scalar,
array, and tensor types, together with the runtime-library definition.

## 1. Language Overview

SysY is a C-like language. A source file has the `.sy` suffix and contains
exactly one `main` function (no parameters, return type `int`). It may also
contain global declarations and other function definitions. The scalar types
are signed 32-bit `int` and 32-bit single-precision `float`; multidimensional
row-major arrays and tensors have either scalar element type. `const` declares constants.
Implicit conversion between `int` and `float` is supported; explicit casts are
not part of SysY. I/O is supplied by the runtime library described below.

Parameters of scalar type are passed by value. Array and tensor parameters are passed as
the starting address; only the first formal dimension may be omitted. Blocks
contain declarations and statements. Statements include assignment,
expression, block, `if`, `while`, `break`, `continue`, and `return`.
Arithmetic operators are `+ - * / %`, relational operators are `== != < > <=
>=`, and logical operators are `! && ||`. Nonzero is true, and relational and
logical results are `1` or `0`; logical operators use C-compatible precedence,
associativity, and short-circuit evaluation.

## 2. SysY Grammar and Semantics

The grammar is in EBNF: `[...]` is optional, `{...}` repeats zero or more
times, and quoted strings or token names are terminals.

```ebnf
CompUnit     -> [ CompUnit ] ( Decl | FuncDef )
Decl         -> ConstDecl | VarDecl
ConstDecl    -> 'const' BType ConstDef { ',' ConstDef } ';'
BType        -> 'int' | 'float' | TensorType
TensorType   -> 'tensor' ( 'int' | 'float' )
ConstDef     -> Ident { '[' ConstExp ']' } '=' ConstInitVal
ConstInitVal -> ConstExp | '{' [ ConstInitVal { ',' ConstInitVal } ] '}'
VarDecl      -> BType VarDef { ',' VarDef } ';'
VarDef       -> Ident { '[' ConstExp ']' }
             | Ident { '[' ConstExp ']' } '=' InitVal
InitVal      -> Exp | '{' [ InitVal { ',' InitVal } ] '}'
FuncDef      -> FuncType Ident '(' [FuncFParams] ')' Block
FuncType     -> 'void' | 'int' | 'float' | TensorType
FuncFParams  -> FuncFParam { ',' FuncFParam }
FuncFParam   -> BType Ident ['[' ']' { '[' Exp ']' }]
Block        -> '{' { BlockItem } '}'
BlockItem    -> Decl | Stmt
Stmt         -> LVal '=' Exp ';' | [Exp] ';' | Block
             | 'if' '(' Cond ')' Stmt [ 'else' Stmt ]
             | 'while' '(' Cond ')' Stmt
             | 'break' ';' | 'continue' ';' | 'return' [Exp] ';'
Exp          -> AddExp
Cond         -> LOrExp
LVal         -> Ident { '[' Exp ']' }
PrimaryExp   -> '(' Exp ')' | LVal | Number
Number       -> IntConst | FloatConst
UnaryExp     -> PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
UnaryOp      -> '+' | '-' | '!'
FuncRParams  -> Exp { ',' Exp }
MulExp       -> UnaryExp | MulExp ('*' | '/' | '%' | '@') UnaryExp
AddExp       -> MulExp | AddExp ('+' | '-') MulExp
RelExp       -> AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp
EqExp        -> RelExp | EqExp ('==' | '!=') RelExp
LAndExp      -> EqExp | LAndExp '&&' EqExp
LOrExp       -> LAndExp | LOrExp '||' LAndExp
ConstExp     -> AddExp
```

Identifiers use letters, digits, and `_`, with a non-digit first character.
Comments follow C (`//` and `/*...*/`). Integer constants may be decimal,
octal, or hexadecimal; floating constants follow the C floating-constant
syntax. All suffixes are ignored. Global declaration scopes begin at their
declaration and extend to end of file; a local declaration shadows a global
one, and overlapping local declarations with the same name are forbidden.

In a declaration, dimensions following an identifier define an array or tensor
shape. For a tensor type, the element type is selected by `TensorType`; rank
and dimension matching use the same rules as multidimensional arrays.

### Declarations and initialization

Aggregates may be global, local, function parameters, or return values. Their
initializers use row-major rules: nested depth follows the rank, omitted
trailing elements are zero-initialized, and flat lists are accepted.
Uninitialized global aggregates have unspecified values. Indexing with `[]`
consumes one dimension at a time; out-of-range indexing is undefined.

Examples: `int a[4] = {1, 2};`, `float b[2][2] =
{{1.0,2.0},{3.0,4.0}};`, and `tensor int c[4] = {1, 2};`.

### Arithmetic, assignment, and parameter semantics

Unary `+` and `-` operate elementwise. Binary `+`, `-`, `*`, `/`, and `%`
operate elementwise and require equal aggregate shapes; `%` is valid only for
integer elements. A scalar can be promoted to the other operand's shape when
base types match. `*` is elementwise multiplication. `@` is matrix
multiplication: `M x N @ N x P` produces `M x P`. Relational and logical
operators accept scalar values only. Aggregate assignment requires identical
types; scalar and aggregate values cannot be assigned directly to one another.

Aggregate parameters use array syntax with an omitted first dimension, for
example `int a[]`, `float b[][10]`, or `tensor int c[]`; subsequent dimensions
are fixed and actual arguments must match. Functions may return aggregate
values with compile-time-known dimensions where required.

## 3. SysY Runtime Library

The supplied library consists of `libsysy.a`, `libsysy.so`, and `sylib.h`.
SysY source files do not include the header; the compiler recognizes these
functions directly. Arguments may be scalar values, variables, or array
element expressions. Array routines receive the starting address and do not
check capacity.

| Function | Meaning |
|---|---|
| `int getint()` | Read and return an integer. |
| `int getch()` | Read a character and return its ASCII code. |
| `float getfloat()` | Read and return a floating-point number. |
| `int getarray(int[])` | Read a count followed by integers into the array; return the count. |
| `int getfarray(float[])` | Read a count followed by floats into the array; return the count. |
| `void putint(int)` | Output an integer. |
| `void putch(int)` | Output the argument as an ASCII character (valid range 0--255). |
| `void putfloat(float)` | Output a float using six fractional digits. |
| `void putarray(int, int[])` | Output `N` integers with spaces; format is `N:` followed by values. |
| `void putfarray(int, float[])` | Output `N` floats with spaces; format is `N:` followed by values. |
| `void putf(format, int, ...)` | Format output using only `%d`, `%c`, and `%f`; other characters are literal. |

`starttime()` and `stoptime()` measure non-nested code regions. Multiple pairs
are allowed; completion prints each timer and a cumulative total in the form
`Timer#number@start-line-stop-line: hour-minute-second-microsecond` followed by
`TOTAL`.