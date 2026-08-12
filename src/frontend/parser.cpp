/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 25 "/workspace/src/frontend/parser.y"

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

#line 223 "/workspace/src/frontend/parser.cpp"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "../include/frontend/parser.hpp"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_INT = 3,                        /* INT  */
  YYSYMBOL_FLOAT = 4,                      /* FLOAT  */
  YYSYMBOL_ID = 5,                         /* ID  */
  YYSYMBOL_STRING_LITERAL = 6,             /* STRING_LITERAL  */
  YYSYMBOL_GTE = 7,                        /* GTE  */
  YYSYMBOL_LTE = 8,                        /* LTE  */
  YYSYMBOL_GT = 9,                         /* GT  */
  YYSYMBOL_LT = 10,                        /* LT  */
  YYSYMBOL_EQ = 11,                        /* EQ  */
  YYSYMBOL_NEQ = 12,                       /* NEQ  */
  YYSYMBOL_BASICTYPE = 13,                 /* BASICTYPE  */
  YYSYMBOL_VOID = 14,                      /* VOID  */
  YYSYMBOL_INVALID = 15,                   /* INVALID  */
  YYSYMBOL_CONST = 16,                     /* CONST  */
  YYSYMBOL_RETURN = 17,                    /* RETURN  */
  YYSYMBOL_IF = 18,                        /* IF  */
  YYSYMBOL_ELSE = 19,                      /* ELSE  */
  YYSYMBOL_WHILE = 20,                     /* WHILE  */
  YYSYMBOL_BREAK = 21,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 22,                  /* CONTINUE  */
  YYSYMBOL_LP = 23,                        /* LP  */
  YYSYMBOL_RP = 24,                        /* RP  */
  YYSYMBOL_LB = 25,                        /* LB  */
  YYSYMBOL_RB = 26,                        /* RB  */
  YYSYMBOL_LC = 27,                        /* LC  */
  YYSYMBOL_RC = 28,                        /* RC  */
  YYSYMBOL_COMMA = 29,                     /* COMMA  */
  YYSYMBOL_SEMICOLON = 30,                 /* SEMICOLON  */
  YYSYMBOL_NOT = 31,                       /* NOT  */
  YYSYMBOL_ASSIGN = 32,                    /* ASSIGN  */
  YYSYMBOL_MINUS = 33,                     /* MINUS  */
  YYSYMBOL_ADD = 34,                       /* ADD  */
  YYSYMBOL_MUL = 35,                       /* MUL  */
  YYSYMBOL_DIV = 36,                       /* DIV  */
  YYSYMBOL_MOD = 37,                       /* MOD  */
  YYSYMBOL_AND = 38,                       /* AND  */
  YYSYMBOL_OR = 39,                        /* OR  */
  YYSYMBOL_LOWER_THEN_ELSE = 40,           /* LOWER_THEN_ELSE  */
  YYSYMBOL_YYACCEPT = 41,                  /* $accept  */
  YYSYMBOL_Program = 42,                   /* Program  */
  YYSYMBOL_CompUnit = 43,                  /* CompUnit  */
  YYSYMBOL_DeclDef = 44,                   /* DeclDef  */
  YYSYMBOL_Decl = 45,                      /* Decl  */
  YYSYMBOL_BType = 46,                     /* BType  */
  YYSYMBOL_VecType = 47,                   /* VecType  */
  YYSYMBOL_VecWidth = 48,                  /* VecWidth  */
  YYSYMBOL_VoidType = 49,                  /* VoidType  */
  YYSYMBOL_ConstDefList = 50,              /* ConstDefList  */
  YYSYMBOL_VarDefList = 51,                /* VarDefList  */
  YYSYMBOL_ConstDef = 52,                  /* ConstDef  */
  YYSYMBOL_VarDef = 53,                    /* VarDef  */
  YYSYMBOL_Arrays = 54,                    /* Arrays  */
  YYSYMBOL_Block = 55,                     /* Block  */
  YYSYMBOL_InitVal = 56,                   /* InitVal  */
  YYSYMBOL_ConstInitVal = 57,              /* ConstInitVal  */
  YYSYMBOL_BraceInitVal = 58,              /* BraceInitVal  */
  YYSYMBOL_InitValList = 59,               /* InitValList  */
  YYSYMBOL_FuncDef = 60,                   /* FuncDef  */
  YYSYMBOL_OptFuncFParamList = 61,         /* OptFuncFParamList  */
  YYSYMBOL_FuncFParamList = 62,            /* FuncFParamList  */
  YYSYMBOL_FuncFParam = 63,                /* FuncFParam  */
  YYSYMBOL_BlockItemList = 64,             /* BlockItemList  */
  YYSYMBOL_BlockItem = 65,                 /* BlockItem  */
  YYSYMBOL_Stmt = 66,                      /* Stmt  */
  YYSYMBOL_SelectStmt = 67,                /* SelectStmt  */
  YYSYMBOL_IterationStmt = 68,             /* IterationStmt  */
  YYSYMBOL_ReturnStmt = 69,                /* ReturnStmt  */
  YYSYMBOL_Exp = 70,                       /* Exp  */
  YYSYMBOL_NonBraceExp = 71,               /* NonBraceExp  */
  YYSYMBOL_NonBraceAddExp = 72,            /* NonBraceAddExp  */
  YYSYMBOL_NonBraceMulExp = 73,            /* NonBraceMulExp  */
  YYSYMBOL_NonBraceUnaryExp = 74,          /* NonBraceUnaryExp  */
  YYSYMBOL_Cond = 75,                      /* Cond  */
  YYSYMBOL_LVal = 76,                      /* LVal  */
  YYSYMBOL_PrimaryExp = 77,                /* PrimaryExp  */
  YYSYMBOL_Number = 78,                    /* Number  */
  YYSYMBOL_UnaryExp = 79,                  /* UnaryExp  */
  YYSYMBOL_Call = 80,                      /* Call  */
  YYSYMBOL_UnaryOp = 81,                   /* UnaryOp  */
  YYSYMBOL_FuncCParamList = 82,            /* FuncCParamList  */
  YYSYMBOL_FuncCParam = 83,                /* FuncCParam  */
  YYSYMBOL_MulExp = 84,                    /* MulExp  */
  YYSYMBOL_AddExp = 85,                    /* AddExp  */
  YYSYMBOL_RelExp = 86,                    /* RelExp  */
  YYSYMBOL_EqExp = 87,                     /* EqExp  */
  YYSYMBOL_LAndExp = 88,                   /* LAndExp  */
  YYSYMBOL_LOrExp = 89                     /* LOrExp  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  19
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   408

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  41
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  133
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  247

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   295


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   262,   262,   268,   273,   281,   282,   286,   290,   297,
     300,   309,   319,   328,   337,   346,   355,   359,   363,   374,
     382,   385,   391,   394,   404,   410,   414,   420,   424,   433,
     438,   447,   452,   459,   463,   470,   474,   485,   488,   499,
     502,   506,   511,   514,   518,   524,   527,   534,   538,   545,
     550,   557,   560,   566,   570,   577,   581,   585,   593,   598,
     606,   609,   616,   619,   623,   626,   629,   632,   635,   636,
     637,   641,   645,   653,   660,   663,   669,   678,   681,   682,
     687,   694,   695,   700,   705,   712,   713,   714,   715,   716,
     720,   724,   730,   736,   740,   749,   750,   751,   752,   758,
     761,   769,   770,   771,   775,   779,   785,   789,   798,   801,
     804,   810,   815,   822,   825,   838,   839,   844,   849,   857,
     858,   863,   871,   872,   877,   882,   887,   895,   896,   901,
     909,   910,   918,   919
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "INT", "FLOAT", "ID",
  "STRING_LITERAL", "GTE", "LTE", "GT", "LT", "EQ", "NEQ", "BASICTYPE",
  "VOID", "INVALID", "CONST", "RETURN", "IF", "ELSE", "WHILE", "BREAK",
  "CONTINUE", "LP", "RP", "LB", "RB", "LC", "RC", "COMMA", "SEMICOLON",
  "NOT", "ASSIGN", "MINUS", "ADD", "MUL", "DIV", "MOD", "AND", "OR",
  "LOWER_THEN_ELSE", "$accept", "Program", "CompUnit", "DeclDef", "Decl",
  "BType", "VecType", "VecWidth", "VoidType", "ConstDefList", "VarDefList",
  "ConstDef", "VarDef", "Arrays", "Block", "InitVal", "ConstInitVal",
  "BraceInitVal", "InitValList", "FuncDef", "OptFuncFParamList",
  "FuncFParamList", "FuncFParam", "BlockItemList", "BlockItem", "Stmt",
  "SelectStmt", "IterationStmt", "ReturnStmt", "Exp", "NonBraceExp",
  "NonBraceAddExp", "NonBraceMulExp", "NonBraceUnaryExp", "Cond", "LVal",
  "PrimaryExp", "Number", "UnaryExp", "Call", "UnaryOp", "FuncCParamList",
  "FuncCParam", "MulExp", "AddExp", "RelExp", "EqExp", "LAndExp", "LOrExp", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-150)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-12)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      99,    91,    47,  -150,    26,    38,    99,  -150,  -150,    54,
    -150,    80,  -150,    81,    60,    94,   169,    43,   116,  -150,
    -150,   103,    -8,  -150,   107,  -150,  -150,    23,   104,    -9,
     123,  -150,   154,  -150,   145,    57,    68,  -150,    26,   337,
     337,    97,   160,  -150,    26,  -150,   139,   166,  -150,   139,
     139,  -150,  -150,   337,   111,   116,  -150,   194,   180,   182,
    -150,  -150,  -150,    33,   337,    72,  -150,  -150,  -150,  -150,
     188,  -150,  -150,  -150,  -150,   192,   337,   119,   142,  -150,
     337,   369,   125,  -150,   205,   227,   237,   225,   226,  -150,
     374,  -150,   232,   235,    26,   270,   239,   242,   337,   282,
    -150,  -150,   167,  -150,   176,   149,  -150,  -150,  -150,   245,
     337,  -150,   337,  -150,   337,   337,   337,   337,   337,   241,
    -150,   235,  -150,  -150,  -150,  -150,   295,  -150,  -150,   262,
     185,  -150,  -150,  -150,  -150,  -150,    31,  -150,   264,   266,
    -150,   195,  -150,   369,   337,   337,   337,   337,   337,   337,
    -150,   265,  -150,  -150,  -150,   119,   119,  -150,  -150,  -150,
     197,   267,   141,   316,   240,   272,   276,   277,  -150,  -150,
    -150,   160,  -150,   217,  -150,  -150,  -150,  -150,  -150,   278,
     279,  -150,   328,   337,   271,  -150,  -150,   119,   119,  -150,
    -150,  -150,   286,  -150,  -150,   239,   114,   353,  -150,   284,
     337,   337,  -150,  -150,  -150,  -150,  -150,   337,  -150,   291,
     337,  -150,  -150,   300,   142,   184,   216,   287,   263,   303,
     305,  -150,   304,   238,   337,   337,   337,   337,   337,   337,
     337,   337,   238,  -150,  -150,   317,   142,   142,   142,   142,
     184,   184,   216,   287,  -150,   238,  -150
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,    11,     9,    24,     0,     0,     2,     4,     5,     0,
      10,     0,     6,     0,     0,     0,     0,     0,     0,     1,
       3,    34,     0,    27,     0,    22,    23,     0,     0,     0,
       0,    20,     0,    21,     0,     0,     0,    25,    51,     0,
       0,    33,     0,     8,    51,    18,     0,     0,    19,     0,
       0,    16,    17,     0,     0,     0,     7,     0,     0,    52,
      53,    99,   100,    93,     0,     0,   110,   109,   108,    98,
       0,    96,   101,    97,   115,   102,     0,   119,    76,    32,
       0,     0,    34,    28,     0,     0,     0,     0,     0,    30,
       0,    26,    55,     0,     0,     0,    94,     0,     0,     0,
      45,    48,     0,    41,    77,    78,    81,    86,    87,    88,
       0,    35,     0,   105,     0,     0,     0,     0,     0,     0,
      31,     0,    12,    13,    14,    15,     0,    29,    44,     0,
       0,    49,    54,   114,   106,   113,     0,   111,    95,     0,
      39,     0,    46,     0,     0,     0,     0,     0,     0,     0,
      91,     0,   116,   117,   118,   121,   120,    36,    50,    42,
       0,    56,    93,     0,     0,     0,     0,     0,    37,    62,
      60,     0,    67,     0,    58,    61,    69,    70,    68,     0,
      86,   107,     0,     0,    85,    40,    47,    80,    79,    82,
      83,    84,     0,   103,    43,    57,     0,     0,    75,     0,
       0,     0,    66,    65,    38,    59,    64,     0,   112,     0,
       0,    89,    74,     0,   122,   127,   130,   132,    92,     0,
       0,   104,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    63,    90,    71,   123,   124,   125,   126,
     128,   129,   131,   133,    73,     0,    72
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -150,  -150,  -150,   331,  -124,     4,  -150,    -5,  -150,  -150,
    -150,   283,   302,   -30,   -79,   -72,  -150,  -150,   -86,  -150,
     301,  -150,   254,  -150,   179,  -149,  -150,  -150,  -150,   -10,
     -83,  -150,  -150,  -150,   152,   -65,  -150,   -64,   -23,   -63,
     -62,  -150,   172,   -94,    53,     3,   133,   134,  -150
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     5,     6,     7,     8,    57,    10,    28,    11,    36,
      22,    37,    23,    41,   172,   101,   127,    69,   102,    12,
      58,    59,    60,   173,   174,   175,   176,   177,   178,   135,
     103,   104,   105,   106,   213,    71,    72,    73,    74,    75,
      76,   136,   137,    77,    78,   215,   216,   217,   218
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     107,   108,   109,   110,     9,    54,   170,   128,    18,   120,
       9,    32,    34,   141,   131,    48,   107,   108,   109,   110,
      49,    42,    43,   155,   156,   107,   108,   109,   110,    70,
      79,     1,    45,    96,   107,   108,   109,   110,    19,     2,
     160,    85,   158,    89,    87,    88,    25,   179,    26,   170,
     187,   188,    46,   113,    97,   181,    95,    16,    39,    21,
     182,   107,   108,   109,   110,   180,   108,   109,   110,    33,
     119,   186,    17,    29,   235,    61,    62,    63,   107,   108,
     109,   110,    39,   244,    25,    24,    26,   150,   139,    53,
     179,   152,   153,   154,    27,    98,   246,    55,    56,    99,
     100,    13,   151,    66,     1,    67,    68,    30,   180,   108,
     109,   110,     2,     3,    14,     4,    15,    61,    62,    63,
     133,    35,    80,   189,   190,   191,    38,    29,    39,    81,
      44,   195,    96,    47,   171,    40,    80,    64,   134,   192,
     179,    65,    25,    90,    26,    66,   -11,    67,    68,   179,
      39,    13,    50,   199,   114,   115,   116,    40,   180,   108,
     109,   110,   179,    51,   196,    82,   197,   180,   108,   109,
     110,    52,    25,   209,    26,   117,   118,   171,    31,    86,
     180,   108,   109,   110,   146,   147,   148,    70,    61,    62,
     162,   224,   225,   226,   227,   142,   143,   220,     2,    92,
     222,     4,   163,   164,    93,   165,   166,   167,    98,   144,
     145,    94,   130,   168,   111,   169,    66,   112,    67,    68,
      61,    62,   162,   185,   143,   194,   143,   228,   229,   121,
       2,   240,   241,     4,   163,   164,   122,   165,   166,   167,
      98,    61,    62,    63,   130,   204,   123,   169,    66,   124,
      67,    68,   125,   214,   214,   163,   164,   129,   165,   166,
     167,    98,   130,   200,    80,   130,   138,   157,   169,    66,
     149,    67,    68,    61,    62,    63,   133,   236,   237,   238,
     239,   214,   214,   214,   214,    61,    62,    63,   161,   183,
     184,   193,    39,    64,   134,   201,   210,    65,    61,    62,
      63,    66,   231,    67,    68,    98,   202,   203,   206,    99,
     140,   207,   211,    66,   212,    67,    68,   221,    98,    61,
      62,    63,    99,   159,   223,   230,    66,   232,    67,    68,
     234,    61,    62,    63,   133,   233,   245,    20,    91,    64,
      61,    62,    63,    65,    83,    84,   198,    66,   132,    67,
      68,    64,   205,   219,   208,    65,    61,    62,    63,    66,
      64,    67,    68,   242,    65,   243,    30,     0,    66,     0,
      67,    68,    61,    62,    63,     0,    64,    61,    62,    63,
      65,     0,     0,     0,    66,     0,    67,    68,     0,     0,
       0,     0,    98,     0,     0,     0,    99,    98,     0,     0,
      66,   126,    67,    68,     0,    66,     0,    67,    68
};

static const yytype_int16 yycheck[] =
{
      65,    65,    65,    65,     0,    35,   130,    90,     4,    81,
       6,    16,    17,    99,    93,    24,    81,    81,    81,    81,
      29,    29,    30,   117,   118,    90,    90,    90,    90,    39,
      40,     5,     9,    63,    99,    99,    99,    99,     0,    13,
     126,    46,   121,    53,    49,    50,     3,   130,     5,   173,
     144,   145,    29,    76,    64,    24,    23,    10,    25,     5,
      29,   126,   126,   126,   126,   130,   130,   130,   130,    26,
      80,   143,    25,    13,   223,     3,     4,     5,   143,   143,
     143,   143,    25,   232,     3,     5,     5,   110,    98,    32,
     173,   114,   115,   116,    13,    23,   245,    29,    30,    27,
      28,    10,   112,    31,     5,    33,    34,    13,   173,   173,
     173,   173,    13,    14,    23,    16,    25,     3,     4,     5,
       6,     5,    25,   146,   147,   148,    23,    13,    25,    32,
      23,   161,   162,    29,   130,    32,    25,    23,    24,   149,
     223,    27,     3,    32,     5,    31,     5,    33,    34,   232,
      25,    10,    29,   163,    35,    36,    37,    32,   223,   223,
     223,   223,   245,     9,    23,     5,    25,   232,   232,   232,
     232,    26,     3,   183,     5,    33,    34,   173,     9,    13,
     245,   245,   245,   245,    35,    36,    37,   197,     3,     4,
       5,     7,     8,     9,    10,    28,    29,   207,    13,     5,
     210,    16,    17,    18,    24,    20,    21,    22,    23,    33,
      34,    29,    27,    28,    26,    30,    31,    25,    33,    34,
       3,     4,     5,    28,    29,    28,    29,    11,    12,    24,
      13,   228,   229,    16,    17,    18,     9,    20,    21,    22,
      23,     3,     4,     5,    27,    28,     9,    30,    31,    24,
      33,    34,    26,   200,   201,    17,    18,    25,    20,    21,
      22,    23,    27,    23,    25,    27,    24,    26,    30,    31,
      25,    33,    34,     3,     4,     5,     6,   224,   225,   226,
     227,   228,   229,   230,   231,     3,     4,     5,    26,    25,
      24,    26,    25,    23,    24,    23,    25,    27,     3,     4,
       5,    31,    39,    33,    34,    23,    30,    30,    30,    27,
      28,    32,    26,    31,    30,    33,    34,    26,    23,     3,
       4,     5,    27,    28,    24,    38,    31,    24,    33,    34,
      26,     3,     4,     5,     6,    30,    19,     6,    55,    23,
       3,     4,     5,    27,    42,    44,    30,    31,    94,    33,
      34,    23,   173,   201,   182,    27,     3,     4,     5,    31,
      23,    33,    34,   230,    27,   231,    13,    -1,    31,    -1,
      33,    34,     3,     4,     5,    -1,    23,     3,     4,     5,
      27,    -1,    -1,    -1,    31,    -1,    33,    34,    -1,    -1,
      -1,    -1,    23,    -1,    -1,    -1,    27,    23,    -1,    -1,
      31,    27,    33,    34,    -1,    31,    -1,    33,    34
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     5,    13,    14,    16,    42,    43,    44,    45,    46,
      47,    49,    60,    10,    23,    25,    10,    25,    46,     0,
      44,     5,    51,    53,     5,     3,     5,    13,    48,    13,
      13,     9,    48,    26,    48,     5,    50,    52,    23,    25,
      32,    54,    29,    30,    23,     9,    29,    29,    24,    29,
      29,     9,    26,    32,    54,    29,    30,    46,    61,    62,
      63,     3,     4,     5,    23,    27,    31,    33,    34,    58,
      70,    76,    77,    78,    79,    80,    81,    84,    85,    70,
      25,    32,     5,    53,    61,    48,    13,    48,    48,    70,
      32,    52,     5,    24,    29,    23,    54,    70,    23,    27,
      28,    56,    59,    71,    72,    73,    74,    76,    78,    80,
      81,    26,    25,    79,    35,    36,    37,    33,    34,    70,
      56,    24,     9,     9,    24,    26,    27,    57,    71,    25,
      27,    55,    63,     6,    24,    70,    82,    83,    24,    70,
      28,    59,    28,    29,    33,    34,    35,    36,    37,    25,
      79,    70,    79,    79,    79,    84,    84,    26,    55,    28,
      59,    26,     5,    17,    18,    20,    21,    22,    28,    30,
      45,    46,    55,    64,    65,    66,    67,    68,    69,    71,
      76,    24,    29,    25,    24,    28,    56,    84,    84,    79,
      79,    79,    70,    26,    28,    54,    23,    25,    30,    70,
      23,    23,    30,    30,    28,    65,    30,    32,    83,    70,
      25,    26,    30,    75,    85,    86,    87,    88,    89,    75,
      70,    26,    70,    24,     7,     8,     9,    10,    11,    12,
      38,    39,    24,    30,    26,    66,    85,    85,    85,    85,
      86,    86,    87,    88,    66,    19,    66
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    41,    42,    43,    43,    44,    44,    45,    45,    46,
      46,    47,    47,    47,    47,    47,    47,    47,    47,    47,
      47,    47,    48,    48,    49,    50,    50,    51,    51,    52,
      52,    53,    53,    53,    53,    54,    54,    55,    55,    56,
      56,    56,    57,    57,    57,    58,    58,    59,    59,    60,
      60,    61,    61,    62,    62,    63,    63,    63,    64,    64,
      65,    65,    66,    66,    66,    66,    66,    66,    66,    66,
      66,    67,    67,    68,    69,    69,    70,    71,    72,    72,
      72,    73,    73,    73,    73,    74,    74,    74,    74,    74,
      74,    74,    75,    76,    76,    77,    77,    77,    77,    78,
      78,    79,    79,    79,    79,    79,    80,    80,    81,    81,
      81,    82,    82,    83,    83,    84,    84,    84,    84,    85,
      85,    85,    86,    86,    86,    86,    86,    87,    87,    87,
      88,    88,    89,    89
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     4,     3,     1,
       1,     1,     6,     6,     6,     6,     4,     4,     4,     4,
       3,     3,     1,     1,     1,     1,     3,     1,     3,     4,
       3,     4,     3,     2,     1,     3,     4,     2,     3,     2,
       3,     1,     2,     3,     1,     2,     3,     3,     1,     6,
       6,     0,     1,     1,     3,     2,     4,     5,     1,     2,
       1,     1,     1,     4,     2,     2,     2,     1,     1,     1,
       1,     5,     7,     5,     3,     2,     1,     1,     1,     3,
       3,     1,     3,     3,     3,     3,     1,     1,     1,     4,
       6,     2,     1,     1,     2,     3,     1,     1,     1,     1,
       1,     1,     1,     4,     6,     2,     3,     4,     1,     1,
       1,     1,     3,     1,     1,     1,     3,     3,     3,     1,
       3,     3,     1,     3,     3,     3,     3,     1,     3,     3,
       1,     3,     1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  switch (yykind)
    {
    case YYSYMBOL_ID: /* ID  */
#line 253 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).token); }
#line 1599 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_STRING_LITERAL: /* STRING_LITERAL  */
#line 253 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).token); }
#line 1605 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_CompUnit: /* CompUnit  */
#line 248 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).compUnit); }
#line 1611 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_DeclDef: /* DeclDef  */
#line 248 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).topLevel); }
#line 1617 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Decl: /* Decl  */
#line 248 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).decl); }
#line 1623 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BType: /* BType  */
#line 253 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1629 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VecType: /* VecType  */
#line 253 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1635 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VecWidth: /* VecWidth  */
#line 253 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1641 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VoidType: /* VoidType  */
#line 253 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1647 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstDefList: /* ConstDefList  */
#line 248 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).objectDefList); }
#line 1653 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VarDefList: /* VarDefList  */
#line 248 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).objectDefList); }
#line 1659 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstDef: /* ConstDef  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).objectDef); }
#line 1665 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VarDef: /* VarDef  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).objectDef); }
#line 1671 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Arrays: /* Arrays  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).exprList); }
#line 1677 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Block: /* Block  */
#line 250 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).block); }
#line 1683 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_InitVal: /* InitVal  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1689 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstInitVal: /* ConstInitVal  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1695 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BraceInitVal: /* BraceInitVal  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1701 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_InitValList: /* InitValList  */
#line 249 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initValList); }
#line 1707 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncDef: /* FuncDef  */
#line 250 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcDef); }
#line 1713 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_OptFuncFParamList: /* OptFuncFParamList  */
#line 250 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcParamList); }
#line 1719 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncFParamList: /* FuncFParamList  */
#line 250 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcParamList); }
#line 1725 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncFParam: /* FuncFParam  */
#line 250 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcParam); }
#line 1731 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BlockItemList: /* BlockItemList  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).blockItemList); }
#line 1737 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BlockItem: /* BlockItem  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).blockItem); }
#line 1743 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Stmt: /* Stmt  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1749 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_SelectStmt: /* SelectStmt  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1755 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_IterationStmt: /* IterationStmt  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1761 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ReturnStmt: /* ReturnStmt  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1767 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Exp: /* Exp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1773 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceExp: /* NonBraceExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1779 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceAddExp: /* NonBraceAddExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1785 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceMulExp: /* NonBraceMulExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1791 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceUnaryExp: /* NonBraceUnaryExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1797 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Cond: /* Cond  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1803 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LVal: /* LVal  */
#line 251 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).lValue); }
#line 1809 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_PrimaryExp: /* PrimaryExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1815 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Number: /* Number  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1821 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_UnaryExp: /* UnaryExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1827 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Call: /* Call  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).callExpr); }
#line 1833 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncCParamList: /* FuncCParamList  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).callArgList); }
#line 1839 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncCParam: /* FuncCParam  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).callArg); }
#line 1845 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_MulExp: /* MulExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1851 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_AddExp: /* AddExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1857 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_RelExp: /* RelExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1863 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_EqExp: /* EqExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1869 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LAndExp: /* LAndExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1875 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LOrExp: /* LOrExp  */
#line 252 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1881 "/workspace/src/frontend/parser.cpp"
        break;

      default:
        break;
    }
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Location data for the lookahead symbol.  */
YYLTYPE yylloc
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* Program: CompUnit  */
#line 262 "/workspace/src/frontend/parser.y"
             {
        root = unique_ptr<CompUnitAST>((yyvsp[0].compUnit));
    }
#line 2181 "/workspace/src/frontend/parser.cpp"
    break;

  case 3: /* CompUnit: CompUnit DeclDef  */
#line 268 "/workspace/src/frontend/parser.y"
                     {
        (yyval.compUnit) = (yyvsp[-1].compUnit);
        (yyval.compUnit)->items.push_back(std::move(*(yyvsp[0].topLevel)));
        delete (yyvsp[0].topLevel);
    }
#line 2191 "/workspace/src/frontend/parser.cpp"
    break;

  case 4: /* CompUnit: DeclDef  */
#line 273 "/workspace/src/frontend/parser.y"
            {
        (yyval.compUnit) = make_node<CompUnitAST>();
        (yyval.compUnit)->items.push_back(std::move(*(yyvsp[0].topLevel)));
        delete (yyvsp[0].topLevel);
    }
#line 2201 "/workspace/src/frontend/parser.cpp"
    break;

  case 5: /* DeclDef: Decl  */
#line 281 "/workspace/src/frontend/parser.y"
         { (yyval.topLevel) = make_node<TopLevelItem>(unique_ptr<DeclAST>((yyvsp[0].decl))); }
#line 2207 "/workspace/src/frontend/parser.cpp"
    break;

  case 6: /* DeclDef: FuncDef  */
#line 282 "/workspace/src/frontend/parser.y"
            { (yyval.topLevel) = make_node<TopLevelItem>(unique_ptr<FuncDefAST>((yyvsp[0].funcDef))); }
#line 2213 "/workspace/src/frontend/parser.cpp"
    break;

  case 7: /* Decl: CONST BType ConstDefList SEMICOLON  */
#line 286 "/workspace/src/frontend/parser.y"
                                       {
        (yyval.decl) = make_node<DeclAST>(*(yyvsp[-2].type_spec), true, std::move((yyvsp[-1].objectDefList)->values));
        delete (yyvsp[-2].type_spec); delete (yyvsp[-1].objectDefList);
    }
#line 2222 "/workspace/src/frontend/parser.cpp"
    break;

  case 8: /* Decl: BType VarDefList SEMICOLON  */
#line 290 "/workspace/src/frontend/parser.y"
                               {
        (yyval.decl) = make_node<DeclAST>(*(yyvsp[-2].type_spec), false, std::move((yyvsp[-1].objectDefList)->values));
        delete (yyvsp[-2].type_spec); delete (yyvsp[-1].objectDefList);
    }
#line 2231 "/workspace/src/frontend/parser.cpp"
    break;

  case 9: /* BType: BASICTYPE  */
#line 297 "/workspace/src/frontend/parser.y"
              {
        (yyval.type_spec) = new TypeSpec(static_cast<TYPE>((yyvsp[0].int_val)));
    }
#line 2239 "/workspace/src/frontend/parser.cpp"
    break;

  case 10: /* BType: VecType  */
#line 300 "/workspace/src/frontend/parser.y"
            {
        (yyval.type_spec) = (yyvsp[0].type_spec);
    }
#line 2247 "/workspace/src/frontend/parser.cpp"
    break;

  case 11: /* VecType: ID  */
#line 309 "/workspace/src/frontend/parser.y"
       {
        auto type = simpleVectorType(*(yyvsp[0].token));
        if (!type) {
            yyerror(("unknown type spelling '" + *(yyvsp[0].token) + "'").c_str());
            delete (yyvsp[0].token);
            YYERROR;
        }
        (yyval.type_spec) = new TypeSpec(*type);
        delete (yyvsp[0].token);
    }
#line 2262 "/workspace/src/frontend/parser.cpp"
    break;

  case 12: /* VecType: ID LT BASICTYPE COMMA VecWidth GT  */
#line 319 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-1].type_spec); YYERROR;
        }
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2276 "/workspace/src/frontend/parser.cpp"
    break;

  case 13: /* VecType: ID LT VecWidth COMMA BASICTYPE GT  */
#line 328 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-3].type_spec); YYERROR;
        }
        (yyvsp[-3].type_spec)->element = static_cast<TYPE>((yyvsp[-1].int_val));
        (yyval.type_spec) = (yyvsp[-3].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2290 "/workspace/src/frontend/parser.cpp"
    break;

  case 14: /* VecType: ID LP BASICTYPE COMMA VecWidth RP  */
#line 337 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-1].type_spec); YYERROR;
        }
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2304 "/workspace/src/frontend/parser.cpp"
    break;

  case 15: /* VecType: ID LB BASICTYPE COMMA VecWidth RB  */
#line 346 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-1].type_spec); YYERROR;
        }
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2318 "/workspace/src/frontend/parser.cpp"
    break;

  case 16: /* VecType: BASICTYPE LT VecWidth GT  */
#line 355 "/workspace/src/frontend/parser.y"
                             {
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 2327 "/workspace/src/frontend/parser.cpp"
    break;

  case 17: /* VecType: BASICTYPE LB VecWidth RB  */
#line 359 "/workspace/src/frontend/parser.y"
                             {
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 2336 "/workspace/src/frontend/parser.cpp"
    break;

  case 18: /* VecType: ID LT BASICTYPE GT  */
#line 363 "/workspace/src/frontend/parser.y"
                       {
        if (isVectorConstructor(*(yyvsp[-3].token)))
            (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-1].int_val))));
        else if (auto width = vectorWidthAlias(*(yyvsp[-3].token)))
            (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(static_cast<TYPE>((yyvsp[-1].int_val)), *width));
        else {
            yyerror(("unknown vector constructor '" + *(yyvsp[-3].token) + "'").c_str());
            delete (yyvsp[-3].token); YYERROR;
        }
        delete (yyvsp[-3].token);
    }
#line 2352 "/workspace/src/frontend/parser.cpp"
    break;

  case 19: /* VecType: ID LP BASICTYPE RP  */
#line 374 "/workspace/src/frontend/parser.y"
                       {
        if (!isVectorConstructor(*(yyvsp[-3].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-3].token) + "'").c_str());
            delete (yyvsp[-3].token); YYERROR;
        }
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-1].int_val))));
        delete (yyvsp[-3].token);
    }
#line 2365 "/workspace/src/frontend/parser.cpp"
    break;

  case 20: /* VecType: BASICTYPE LT GT  */
#line 382 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-2].int_val))));
    }
#line 2373 "/workspace/src/frontend/parser.cpp"
    break;

  case 21: /* VecType: BASICTYPE LB RB  */
#line 385 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-2].int_val))));
    }
#line 2381 "/workspace/src/frontend/parser.cpp"
    break;

  case 22: /* VecWidth: INT  */
#line 391 "/workspace/src/frontend/parser.y"
        {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, (yyvsp[0].int_val)));
    }
#line 2389 "/workspace/src/frontend/parser.cpp"
    break;

  case 23: /* VecWidth: ID  */
#line 394 "/workspace/src/frontend/parser.y"
       {
        if (auto width = vectorWidthAlias(*(yyvsp[0].token)))
            (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *width));
        else
            (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 2401 "/workspace/src/frontend/parser.cpp"
    break;

  case 24: /* VoidType: VOID  */
#line 404 "/workspace/src/frontend/parser.y"
         {
        (yyval.type_spec) = new TypeSpec(TYPE_VOID);
    }
#line 2409 "/workspace/src/frontend/parser.cpp"
    break;

  case 25: /* ConstDefList: ConstDef  */
#line 410 "/workspace/src/frontend/parser.y"
             {
        (yyval.objectDefList) = make_node<ObjectDefList>();
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2418 "/workspace/src/frontend/parser.cpp"
    break;

  case 26: /* ConstDefList: ConstDefList COMMA ConstDef  */
#line 414 "/workspace/src/frontend/parser.y"
                                {
        (yyval.objectDefList) = (yyvsp[-2].objectDefList);
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2427 "/workspace/src/frontend/parser.cpp"
    break;

  case 27: /* VarDefList: VarDef  */
#line 420 "/workspace/src/frontend/parser.y"
           {
        (yyval.objectDefList) = make_node<ObjectDefList>();
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2436 "/workspace/src/frontend/parser.cpp"
    break;

  case 28: /* VarDefList: VarDefList COMMA VarDef  */
#line 424 "/workspace/src/frontend/parser.y"
                            {
        (yyval.objectDefList) = (yyvsp[-2].objectDefList);
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2445 "/workspace/src/frontend/parser.cpp"
    break;

  case 29: /* ConstDef: ID Arrays ASSIGN ConstInitVal  */
#line 433 "/workspace/src/frontend/parser.y"
                                  {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[-3].token)), std::move((yyvsp[-2].exprList)->values),
                                     unique_ptr<InitValAST>((yyvsp[0].initVal)));
        delete (yyvsp[-3].token); delete (yyvsp[-2].exprList);
    }
#line 2455 "/workspace/src/frontend/parser.cpp"
    break;

  case 30: /* ConstDef: ID ASSIGN Exp  */
#line 438 "/workspace/src/frontend/parser.y"
                  {
        (yyval.objectDef) = make_node<ObjectDefAST>(
            std::move(*(yyvsp[-2].token)), std::vector<unique_ptr<ExprAST>>{},
            unique_ptr<InitValAST>(make_node<InitValAST>(
                unique_ptr<ExprAST>((yyvsp[0].expr)))));
        delete (yyvsp[-2].token);
    }
#line 2467 "/workspace/src/frontend/parser.cpp"
    break;

  case 31: /* VarDef: ID Arrays ASSIGN InitVal  */
#line 447 "/workspace/src/frontend/parser.y"
                             {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[-3].token)), std::move((yyvsp[-2].exprList)->values),
                                     unique_ptr<InitValAST>((yyvsp[0].initVal)));
        delete (yyvsp[-3].token); delete (yyvsp[-2].exprList);
    }
#line 2477 "/workspace/src/frontend/parser.cpp"
    break;

  case 32: /* VarDef: ID ASSIGN Exp  */
#line 452 "/workspace/src/frontend/parser.y"
                  {
        (yyval.objectDef) = make_node<ObjectDefAST>(
            std::move(*(yyvsp[-2].token)), std::vector<unique_ptr<ExprAST>>{},
            unique_ptr<InitValAST>(make_node<InitValAST>(
                unique_ptr<ExprAST>((yyvsp[0].expr)))));
        delete (yyvsp[-2].token);
    }
#line 2489 "/workspace/src/frontend/parser.cpp"
    break;

  case 33: /* VarDef: ID Arrays  */
#line 459 "/workspace/src/frontend/parser.y"
              {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[-1].token)), std::move((yyvsp[0].exprList)->values));
        delete (yyvsp[-1].token); delete (yyvsp[0].exprList);
    }
#line 2498 "/workspace/src/frontend/parser.cpp"
    break;

  case 34: /* VarDef: ID  */
#line 463 "/workspace/src/frontend/parser.y"
       {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 2507 "/workspace/src/frontend/parser.cpp"
    break;

  case 35: /* Arrays: LB Exp RB  */
#line 470 "/workspace/src/frontend/parser.y"
              {
        (yyval.exprList) = make_node<ExprList>();
        (yyval.exprList)->values.push_back(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2516 "/workspace/src/frontend/parser.cpp"
    break;

  case 36: /* Arrays: Arrays LB Exp RB  */
#line 474 "/workspace/src/frontend/parser.y"
                     {
        (yyval.exprList) = (yyvsp[-3].exprList);
        (yyval.exprList)->values.push_back(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2525 "/workspace/src/frontend/parser.cpp"
    break;

  case 37: /* Block: LC RC  */
#line 485 "/workspace/src/frontend/parser.y"
          {
        (yyval.block) = make_node<BlockAST>();
    }
#line 2533 "/workspace/src/frontend/parser.cpp"
    break;

  case 38: /* Block: LC BlockItemList RC  */
#line 488 "/workspace/src/frontend/parser.y"
                        {
        (yyval.block) = make_node<BlockAST>();
        (yyval.block)->items.swap(*(yyvsp[-1].blockItemList));
        delete (yyvsp[-1].blockItemList);
    }
#line 2543 "/workspace/src/frontend/parser.cpp"
    break;

  case 39: /* InitVal: LC RC  */
#line 499 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2551 "/workspace/src/frontend/parser.cpp"
    break;

  case 40: /* InitVal: LC InitValList RC  */
#line 502 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>(std::move((yyvsp[-1].initValList)->values));
        delete (yyvsp[-1].initValList);
    }
#line 2560 "/workspace/src/frontend/parser.cpp"
    break;

  case 41: /* InitVal: NonBraceExp  */
#line 506 "/workspace/src/frontend/parser.y"
                {
        (yyval.initVal) = make_node<InitValAST>(unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2568 "/workspace/src/frontend/parser.cpp"
    break;

  case 42: /* ConstInitVal: LC RC  */
#line 511 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2576 "/workspace/src/frontend/parser.cpp"
    break;

  case 43: /* ConstInitVal: LC InitValList RC  */
#line 514 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>(std::move((yyvsp[-1].initValList)->values));
        delete (yyvsp[-1].initValList);
    }
#line 2585 "/workspace/src/frontend/parser.cpp"
    break;

  case 44: /* ConstInitVal: NonBraceExp  */
#line 518 "/workspace/src/frontend/parser.y"
                {
        (yyval.initVal) = make_node<InitValAST>(unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2593 "/workspace/src/frontend/parser.cpp"
    break;

  case 45: /* BraceInitVal: LC RC  */
#line 524 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2601 "/workspace/src/frontend/parser.cpp"
    break;

  case 46: /* BraceInitVal: LC InitValList RC  */
#line 527 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>(std::move((yyvsp[-1].initValList)->values));
        delete (yyvsp[-1].initValList);
    }
#line 2610 "/workspace/src/frontend/parser.cpp"
    break;

  case 47: /* InitValList: InitValList COMMA InitVal  */
#line 534 "/workspace/src/frontend/parser.y"
                            {
    (yyval.initValList) = (yyvsp[-2].initValList);
    (yyval.initValList)->values.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2619 "/workspace/src/frontend/parser.cpp"
    break;

  case 48: /* InitValList: InitVal  */
#line 538 "/workspace/src/frontend/parser.y"
          {
    (yyval.initValList) = make_node<InitValList>();
    (yyval.initValList)->values.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2628 "/workspace/src/frontend/parser.cpp"
    break;

  case 49: /* FuncDef: BType ID LP OptFuncFParamList RP Block  */
#line 545 "/workspace/src/frontend/parser.y"
                                           {
        (yyval.funcDef) = make_node<FuncDefAST>(*(yyvsp[-5].type_spec), std::move(*(yyvsp[-4].token)),
            std::move((yyvsp[-2].funcParamList)->values), unique_ptr<BlockAST>((yyvsp[0].block)));
        delete (yyvsp[-5].type_spec); delete (yyvsp[-4].token); delete (yyvsp[-2].funcParamList);
    }
#line 2638 "/workspace/src/frontend/parser.cpp"
    break;

  case 50: /* FuncDef: VoidType ID LP OptFuncFParamList RP Block  */
#line 550 "/workspace/src/frontend/parser.y"
                                              {
        (yyval.funcDef) = make_node<FuncDefAST>(*(yyvsp[-5].type_spec), std::move(*(yyvsp[-4].token)),
            std::move((yyvsp[-2].funcParamList)->values), unique_ptr<BlockAST>((yyvsp[0].block)));
        delete (yyvsp[-5].type_spec); delete (yyvsp[-4].token); delete (yyvsp[-2].funcParamList);
    }
#line 2648 "/workspace/src/frontend/parser.cpp"
    break;

  case 51: /* OptFuncFParamList: %empty  */
#line 557 "/workspace/src/frontend/parser.y"
           {
        (yyval.funcParamList) = make_node<FuncParamList>();
    }
#line 2656 "/workspace/src/frontend/parser.cpp"
    break;

  case 52: /* OptFuncFParamList: FuncFParamList  */
#line 560 "/workspace/src/frontend/parser.y"
                   {
        (yyval.funcParamList) = (yyvsp[0].funcParamList);
    }
#line 2664 "/workspace/src/frontend/parser.cpp"
    break;

  case 53: /* FuncFParamList: FuncFParam  */
#line 566 "/workspace/src/frontend/parser.y"
               {
        (yyval.funcParamList) = make_node<FuncParamList>();
        (yyval.funcParamList)->values.push_back(unique_ptr<FuncParamAST>((yyvsp[0].funcParam)));
    }
#line 2673 "/workspace/src/frontend/parser.cpp"
    break;

  case 54: /* FuncFParamList: FuncFParamList COMMA FuncFParam  */
#line 570 "/workspace/src/frontend/parser.y"
                                    {
        (yyval.funcParamList) = (yyvsp[-2].funcParamList);
        (yyval.funcParamList)->values.push_back(unique_ptr<FuncParamAST>((yyvsp[0].funcParam)));
    }
#line 2682 "/workspace/src/frontend/parser.cpp"
    break;

  case 55: /* FuncFParam: BType ID  */
#line 577 "/workspace/src/frontend/parser.y"
             {
        (yyval.funcParam) = make_node<FuncParamAST>(*(yyvsp[-1].type_spec), std::move(*(yyvsp[0].token)));
        delete (yyvsp[-1].type_spec); delete (yyvsp[0].token);
    }
#line 2691 "/workspace/src/frontend/parser.cpp"
    break;

  case 56: /* FuncFParam: BType ID LB RB  */
#line 581 "/workspace/src/frontend/parser.y"
                   {
        (yyval.funcParam) = make_node<FuncParamAST>(*(yyvsp[-3].type_spec), std::move(*(yyvsp[-2].token)), true);
        delete (yyvsp[-3].type_spec); delete (yyvsp[-2].token);
    }
#line 2700 "/workspace/src/frontend/parser.cpp"
    break;

  case 57: /* FuncFParam: BType ID LB RB Arrays  */
#line 585 "/workspace/src/frontend/parser.y"
                          {
        (yyval.funcParam) = make_node<FuncParamAST>(*(yyvsp[-4].type_spec), std::move(*(yyvsp[-3].token)), true,
                                     std::move((yyvsp[0].exprList)->values));
        delete (yyvsp[-4].type_spec); delete (yyvsp[-3].token); delete (yyvsp[0].exprList);
    }
#line 2710 "/workspace/src/frontend/parser.cpp"
    break;

  case 58: /* BlockItemList: BlockItem  */
#line 593 "/workspace/src/frontend/parser.y"
              {
        (yyval.blockItemList) = make_node<BlockItemList>();
        (yyval.blockItemList)->push_back(std::move(*(yyvsp[0].blockItem)));
        delete (yyvsp[0].blockItem);
    }
#line 2720 "/workspace/src/frontend/parser.cpp"
    break;

  case 59: /* BlockItemList: BlockItemList BlockItem  */
#line 598 "/workspace/src/frontend/parser.y"
                            {
        (yyval.blockItemList) = (yyvsp[-1].blockItemList);
        (yyval.blockItemList)->push_back(std::move(*(yyvsp[0].blockItem)));
        delete (yyvsp[0].blockItem);
    }
#line 2730 "/workspace/src/frontend/parser.cpp"
    break;

  case 60: /* BlockItem: Decl  */
#line 606 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>(unique_ptr<DeclAST>((yyvsp[0].decl)));
    }
#line 2738 "/workspace/src/frontend/parser.cpp"
    break;

  case 61: /* BlockItem: Stmt  */
#line 609 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>(unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2746 "/workspace/src/frontend/parser.cpp"
    break;

  case 62: /* Stmt: SEMICOLON  */
#line 616 "/workspace/src/frontend/parser.y"
              {
        (yyval.stmt) = make_node<EmptyStmtAST>();
    }
#line 2754 "/workspace/src/frontend/parser.cpp"
    break;

  case 63: /* Stmt: LVal ASSIGN Exp SEMICOLON  */
#line 619 "/workspace/src/frontend/parser.y"
                              {
        (yyval.stmt) = make_node<AssignStmtAST>(unique_ptr<LValueAST>((yyvsp[-3].lValue)),
                                      unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2763 "/workspace/src/frontend/parser.cpp"
    break;

  case 64: /* Stmt: NonBraceExp SEMICOLON  */
#line 623 "/workspace/src/frontend/parser.y"
                          {
        (yyval.stmt) = make_node<ExprStmtAST>(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2771 "/workspace/src/frontend/parser.cpp"
    break;

  case 65: /* Stmt: CONTINUE SEMICOLON  */
#line 626 "/workspace/src/frontend/parser.y"
                       {
        (yyval.stmt) = make_node<ContinueStmtAST>();
    }
#line 2779 "/workspace/src/frontend/parser.cpp"
    break;

  case 66: /* Stmt: BREAK SEMICOLON  */
#line 629 "/workspace/src/frontend/parser.y"
                    {
        (yyval.stmt) = make_node<BreakStmtAST>();
    }
#line 2787 "/workspace/src/frontend/parser.cpp"
    break;

  case 67: /* Stmt: Block  */
#line 632 "/workspace/src/frontend/parser.y"
          {
        (yyval.stmt) = make_node<BlockStmtAST>(unique_ptr<BlockAST>((yyvsp[0].block)));
    }
#line 2795 "/workspace/src/frontend/parser.cpp"
    break;

  case 68: /* Stmt: ReturnStmt  */
#line 635 "/workspace/src/frontend/parser.y"
               { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2801 "/workspace/src/frontend/parser.cpp"
    break;

  case 69: /* Stmt: SelectStmt  */
#line 636 "/workspace/src/frontend/parser.y"
               { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2807 "/workspace/src/frontend/parser.cpp"
    break;

  case 70: /* Stmt: IterationStmt  */
#line 637 "/workspace/src/frontend/parser.y"
                  { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2813 "/workspace/src/frontend/parser.cpp"
    break;

  case 71: /* SelectStmt: IF LP Cond RP Stmt  */
#line 641 "/workspace/src/frontend/parser.y"
                                             {
        (yyval.stmt) = make_node<IfStmtAST>(unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                  unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2822 "/workspace/src/frontend/parser.cpp"
    break;

  case 72: /* SelectStmt: IF LP Cond RP Stmt ELSE Stmt  */
#line 645 "/workspace/src/frontend/parser.y"
                                 {
        (yyval.stmt) = make_node<IfStmtAST>(unique_ptr<ExprAST>((yyvsp[-4].expr)),
                                  unique_ptr<StmtAST>((yyvsp[-2].stmt)),
                                  unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2832 "/workspace/src/frontend/parser.cpp"
    break;

  case 73: /* IterationStmt: WHILE LP Cond RP Stmt  */
#line 653 "/workspace/src/frontend/parser.y"
                          {
        (yyval.stmt) = make_node<WhileStmtAST>(unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                     unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2841 "/workspace/src/frontend/parser.cpp"
    break;

  case 74: /* ReturnStmt: RETURN Exp SEMICOLON  */
#line 660 "/workspace/src/frontend/parser.y"
                         {
        (yyval.stmt) = make_node<ReturnStmtAST>(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2849 "/workspace/src/frontend/parser.cpp"
    break;

  case 75: /* ReturnStmt: RETURN SEMICOLON  */
#line 663 "/workspace/src/frontend/parser.y"
                     {
        (yyval.stmt) = make_node<ReturnStmtAST>();
    }
#line 2857 "/workspace/src/frontend/parser.cpp"
    break;

  case 76: /* Exp: AddExp  */
#line 669 "/workspace/src/frontend/parser.y"
           {
        (yyval.expr) = (yyvsp[0].expr);
    }
#line 2865 "/workspace/src/frontend/parser.cpp"
    break;

  case 77: /* NonBraceExp: NonBraceAddExp  */
#line 678 "/workspace/src/frontend/parser.y"
                   { (yyval.expr) = (yyvsp[0].expr); }
#line 2871 "/workspace/src/frontend/parser.cpp"
    break;

  case 78: /* NonBraceAddExp: NonBraceMulExp  */
#line 681 "/workspace/src/frontend/parser.y"
                   { (yyval.expr) = (yyvsp[0].expr); }
#line 2877 "/workspace/src/frontend/parser.cpp"
    break;

  case 79: /* NonBraceAddExp: NonBraceAddExp ADD MulExp  */
#line 682 "/workspace/src/frontend/parser.y"
                              {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Add,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2887 "/workspace/src/frontend/parser.cpp"
    break;

  case 80: /* NonBraceAddExp: NonBraceAddExp MINUS MulExp  */
#line 687 "/workspace/src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Subtract,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2897 "/workspace/src/frontend/parser.cpp"
    break;

  case 81: /* NonBraceMulExp: NonBraceUnaryExp  */
#line 694 "/workspace/src/frontend/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 2903 "/workspace/src/frontend/parser.cpp"
    break;

  case 82: /* NonBraceMulExp: NonBraceMulExp MUL UnaryExp  */
#line 695 "/workspace/src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Multiply,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2913 "/workspace/src/frontend/parser.cpp"
    break;

  case 83: /* NonBraceMulExp: NonBraceMulExp DIV UnaryExp  */
#line 700 "/workspace/src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Divide,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2923 "/workspace/src/frontend/parser.cpp"
    break;

  case 84: /* NonBraceMulExp: NonBraceMulExp MOD UnaryExp  */
#line 705 "/workspace/src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Remainder,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2933 "/workspace/src/frontend/parser.cpp"
    break;

  case 85: /* NonBraceUnaryExp: LP Exp RP  */
#line 712 "/workspace/src/frontend/parser.y"
              { (yyval.expr) = (yyvsp[-1].expr); }
#line 2939 "/workspace/src/frontend/parser.cpp"
    break;

  case 86: /* NonBraceUnaryExp: LVal  */
#line 713 "/workspace/src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].lValue); }
#line 2945 "/workspace/src/frontend/parser.cpp"
    break;

  case 87: /* NonBraceUnaryExp: Number  */
#line 714 "/workspace/src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 2951 "/workspace/src/frontend/parser.cpp"
    break;

  case 88: /* NonBraceUnaryExp: Call  */
#line 715 "/workspace/src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].callExpr); }
#line 2957 "/workspace/src/frontend/parser.cpp"
    break;

  case 89: /* NonBraceUnaryExp: Call LB Exp RB  */
#line 716 "/workspace/src/frontend/parser.y"
                   {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-3].callExpr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2966 "/workspace/src/frontend/parser.cpp"
    break;

  case 90: /* NonBraceUnaryExp: LP Exp RP LB Exp RB  */
#line 720 "/workspace/src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-4].expr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2975 "/workspace/src/frontend/parser.cpp"
    break;

  case 91: /* NonBraceUnaryExp: UnaryOp UnaryExp  */
#line 724 "/workspace/src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<UnaryExprAST>((yyvsp[-1].unaryOp), unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2983 "/workspace/src/frontend/parser.cpp"
    break;

  case 92: /* Cond: LOrExp  */
#line 730 "/workspace/src/frontend/parser.y"
           {
        (yyval.expr) = (yyvsp[0].expr);
    }
#line 2991 "/workspace/src/frontend/parser.cpp"
    break;

  case 93: /* LVal: ID  */
#line 736 "/workspace/src/frontend/parser.y"
       {
        (yyval.lValue) = make_node<LValueAST>(std::move(*(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 3000 "/workspace/src/frontend/parser.cpp"
    break;

  case 94: /* LVal: ID Arrays  */
#line 740 "/workspace/src/frontend/parser.y"
              {
        (yyval.lValue) = make_node<LValueAST>(std::move(*(yyvsp[-1].token)));
        delete (yyvsp[-1].token);
        (yyval.lValue)->indices.swap((yyvsp[0].exprList)->values);
        delete (yyvsp[0].exprList);
    }
#line 3011 "/workspace/src/frontend/parser.cpp"
    break;

  case 95: /* PrimaryExp: LP Exp RP  */
#line 749 "/workspace/src/frontend/parser.y"
              { (yyval.expr) = (yyvsp[-1].expr); }
#line 3017 "/workspace/src/frontend/parser.cpp"
    break;

  case 96: /* PrimaryExp: LVal  */
#line 750 "/workspace/src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].lValue); }
#line 3023 "/workspace/src/frontend/parser.cpp"
    break;

  case 97: /* PrimaryExp: Number  */
#line 751 "/workspace/src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3029 "/workspace/src/frontend/parser.cpp"
    break;

  case 98: /* PrimaryExp: BraceInitVal  */
#line 752 "/workspace/src/frontend/parser.y"
                 {
        (yyval.expr) = make_node<AggregateExprAST>(unique_ptr<InitValAST>((yyvsp[0].initVal)));
    }
#line 3037 "/workspace/src/frontend/parser.cpp"
    break;

  case 99: /* Number: INT  */
#line 758 "/workspace/src/frontend/parser.y"
        {
        (yyval.expr) = make_node<LiteralExprAST>((yyvsp[0].int_val));
    }
#line 3045 "/workspace/src/frontend/parser.cpp"
    break;

  case 100: /* Number: FLOAT  */
#line 761 "/workspace/src/frontend/parser.y"
          {
        (yyval.expr) = make_node<LiteralExprAST>((yyvsp[0].float_val));
    }
#line 3053 "/workspace/src/frontend/parser.cpp"
    break;

  case 101: /* UnaryExp: PrimaryExp  */
#line 769 "/workspace/src/frontend/parser.y"
               { (yyval.expr) = (yyvsp[0].expr); }
#line 3059 "/workspace/src/frontend/parser.cpp"
    break;

  case 102: /* UnaryExp: Call  */
#line 770 "/workspace/src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].callExpr); }
#line 3065 "/workspace/src/frontend/parser.cpp"
    break;

  case 103: /* UnaryExp: Call LB Exp RB  */
#line 771 "/workspace/src/frontend/parser.y"
                   {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-3].callExpr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 3074 "/workspace/src/frontend/parser.cpp"
    break;

  case 104: /* UnaryExp: LP Exp RP LB Exp RB  */
#line 775 "/workspace/src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-4].expr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 3083 "/workspace/src/frontend/parser.cpp"
    break;

  case 105: /* UnaryExp: UnaryOp UnaryExp  */
#line 779 "/workspace/src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<UnaryExprAST>((yyvsp[-1].unaryOp), unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3091 "/workspace/src/frontend/parser.cpp"
    break;

  case 106: /* Call: ID LP RP  */
#line 785 "/workspace/src/frontend/parser.y"
             {
        (yyval.callExpr) = make_node<CallExprAST>(std::move(*(yyvsp[-2].token)), (yylsp[-2]).first_line);
        delete (yyvsp[-2].token);
    }
#line 3100 "/workspace/src/frontend/parser.cpp"
    break;

  case 107: /* Call: ID LP FuncCParamList RP  */
#line 789 "/workspace/src/frontend/parser.y"
                            {
        (yyval.callExpr) = make_node<CallExprAST>(std::move(*(yyvsp[-3].token)), (yylsp[-3]).first_line);
        delete (yyvsp[-3].token);
        (yyval.callExpr)->arguments.swap(*(yyvsp[-1].callArgList));
        delete (yyvsp[-1].callArgList);
    }
#line 3111 "/workspace/src/frontend/parser.cpp"
    break;

  case 108: /* UnaryOp: ADD  */
#line 798 "/workspace/src/frontend/parser.y"
        {
        (yyval.unaryOp) = UnaryOp::Plus;
    }
#line 3119 "/workspace/src/frontend/parser.cpp"
    break;

  case 109: /* UnaryOp: MINUS  */
#line 801 "/workspace/src/frontend/parser.y"
          {
        (yyval.unaryOp) = UnaryOp::Minus;
    }
#line 3127 "/workspace/src/frontend/parser.cpp"
    break;

  case 110: /* UnaryOp: NOT  */
#line 804 "/workspace/src/frontend/parser.y"
        {
        (yyval.unaryOp) = UnaryOp::LogicalNot;
    }
#line 3135 "/workspace/src/frontend/parser.cpp"
    break;

  case 111: /* FuncCParamList: FuncCParam  */
#line 810 "/workspace/src/frontend/parser.y"
               {
        (yyval.callArgList) = make_node<CallArgList>();
        (yyval.callArgList)->push_back(std::move(*(yyvsp[0].callArg)));
        delete (yyvsp[0].callArg);
    }
#line 3145 "/workspace/src/frontend/parser.cpp"
    break;

  case 112: /* FuncCParamList: FuncCParamList COMMA FuncCParam  */
#line 815 "/workspace/src/frontend/parser.y"
                                    {
        (yyval.callArgList) = (yyvsp[-2].callArgList);
        (yyval.callArgList)->push_back(std::move(*(yyvsp[0].callArg)));
        delete (yyvsp[0].callArg);
    }
#line 3155 "/workspace/src/frontend/parser.cpp"
    break;

  case 113: /* FuncCParam: Exp  */
#line 822 "/workspace/src/frontend/parser.y"
        {
        (yyval.callArg) = make_node<CallArgumentAST>(unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3163 "/workspace/src/frontend/parser.cpp"
    break;

  case 114: /* FuncCParam: STRING_LITERAL  */
#line 825 "/workspace/src/frontend/parser.y"
                   {
        string decoded;
        if (!decodeStringLiteral(*(yyvsp[0].token), decoded)) {
            yyerror("invalid escape sequence in string literal");
            delete (yyvsp[0].token);
            YYERROR;
        }
        (yyval.callArg) = make_node<CallArgumentAST>(std::move(decoded));
        delete (yyvsp[0].token);
    }
#line 3178 "/workspace/src/frontend/parser.cpp"
    break;

  case 115: /* MulExp: UnaryExp  */
#line 838 "/workspace/src/frontend/parser.y"
             { (yyval.expr) = (yyvsp[0].expr); }
#line 3184 "/workspace/src/frontend/parser.cpp"
    break;

  case 116: /* MulExp: MulExp MUL UnaryExp  */
#line 839 "/workspace/src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Multiply,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3194 "/workspace/src/frontend/parser.cpp"
    break;

  case 117: /* MulExp: MulExp DIV UnaryExp  */
#line 844 "/workspace/src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Divide,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3204 "/workspace/src/frontend/parser.cpp"
    break;

  case 118: /* MulExp: MulExp MOD UnaryExp  */
#line 849 "/workspace/src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Remainder,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3214 "/workspace/src/frontend/parser.cpp"
    break;

  case 119: /* AddExp: MulExp  */
#line 857 "/workspace/src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3220 "/workspace/src/frontend/parser.cpp"
    break;

  case 120: /* AddExp: AddExp ADD MulExp  */
#line 858 "/workspace/src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Add,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3230 "/workspace/src/frontend/parser.cpp"
    break;

  case 121: /* AddExp: AddExp MINUS MulExp  */
#line 863 "/workspace/src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Subtract,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3240 "/workspace/src/frontend/parser.cpp"
    break;

  case 122: /* RelExp: AddExp  */
#line 871 "/workspace/src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3246 "/workspace/src/frontend/parser.cpp"
    break;

  case 123: /* RelExp: RelExp GTE AddExp  */
#line 872 "/workspace/src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::GreaterEqual,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3256 "/workspace/src/frontend/parser.cpp"
    break;

  case 124: /* RelExp: RelExp LTE AddExp  */
#line 877 "/workspace/src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::LessEqual,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3266 "/workspace/src/frontend/parser.cpp"
    break;

  case 125: /* RelExp: RelExp GT AddExp  */
#line 882 "/workspace/src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Greater,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3276 "/workspace/src/frontend/parser.cpp"
    break;

  case 126: /* RelExp: RelExp LT AddExp  */
#line 887 "/workspace/src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Less,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3286 "/workspace/src/frontend/parser.cpp"
    break;

  case 127: /* EqExp: RelExp  */
#line 895 "/workspace/src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3292 "/workspace/src/frontend/parser.cpp"
    break;

  case 128: /* EqExp: EqExp EQ RelExp  */
#line 896 "/workspace/src/frontend/parser.y"
                    {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Equal,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3302 "/workspace/src/frontend/parser.cpp"
    break;

  case 129: /* EqExp: EqExp NEQ RelExp  */
#line 901 "/workspace/src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::NotEqual,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3312 "/workspace/src/frontend/parser.cpp"
    break;

  case 130: /* LAndExp: EqExp  */
#line 909 "/workspace/src/frontend/parser.y"
          { (yyval.expr) = (yyvsp[0].expr); }
#line 3318 "/workspace/src/frontend/parser.cpp"
    break;

  case 131: /* LAndExp: LAndExp AND EqExp  */
#line 910 "/workspace/src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::LogicalAnd,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3328 "/workspace/src/frontend/parser.cpp"
    break;

  case 132: /* LOrExp: LAndExp  */
#line 918 "/workspace/src/frontend/parser.y"
            { (yyval.expr) = (yyvsp[0].expr); }
#line 3334 "/workspace/src/frontend/parser.cpp"
    break;

  case 133: /* LOrExp: LOrExp OR LAndExp  */
#line 919 "/workspace/src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::LogicalOr,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3344 "/workspace/src/frontend/parser.cpp"
    break;


#line 3348 "/workspace/src/frontend/parser.cpp"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 924 "/workspace/src/frontend/parser.y"


void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
