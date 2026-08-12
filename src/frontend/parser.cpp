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
#line 10 "/workspace/src/frontend/parser.y"

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
       0,   271,   271,   277,   281,   288,   292,   299,   307,   318,
     321,   330,   340,   349,   358,   367,   376,   380,   384,   395,
     403,   406,   412,   415,   425,   431,   435,   441,   445,   454,
     461,   469,   476,   482,   488,   495,   499,   510,   513,   524,
     527,   532,   538,   541,   546,   553,   556,   564,   568,   575,
     584,   595,   598,   604,   608,   615,   622,   629,   641,   645,
     652,   656,   663,   667,   673,   678,   682,   686,   691,   696,
     701,   709,   714,   723,   731,   735,   741,   750,   753,   757,
     763,   771,   775,   781,   787,   795,   801,   807,   813,   817,
     824,   833,   841,   847,   851,   860,   864,   868,   872,   879,
     884,   894,   898,   902,   909,   918,   926,   931,   941,   944,
     947,   953,   957,   963,   967,   981,   985,   991,   997,  1006,
    1010,  1016,  1025,  1029,  1035,  1041,  1047,  1056,  1060,  1066,
    1075,  1079,  1087,  1091
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
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).token); }
#line 1599 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_STRING_LITERAL: /* STRING_LITERAL  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).token); }
#line 1605 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_CompUnit: /* CompUnit  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).compUnit); }
#line 1611 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_DeclDef: /* DeclDef  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).declDef); }
#line 1617 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Decl: /* Decl  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).decl); }
#line 1623 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BType: /* BType  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1629 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VecType: /* VecType  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1635 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VecWidth: /* VecWidth  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1641 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VoidType: /* VoidType  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1647 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstDefList: /* ConstDefList  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).defList); }
#line 1653 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VarDefList: /* VarDefList  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).defList); }
#line 1659 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstDef: /* ConstDef  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).def); }
#line 1665 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VarDef: /* VarDef  */
#line 255 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).def); }
#line 1671 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Arrays: /* Arrays  */
#line 256 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).arrays); }
#line 1677 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Block: /* Block  */
#line 257 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).block); }
#line 1683 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_InitVal: /* InitVal  */
#line 256 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1689 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstInitVal: /* ConstInitVal  */
#line 256 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1695 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BraceInitVal: /* BraceInitVal  */
#line 256 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1701 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_InitValList: /* InitValList  */
#line 256 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).initValList); }
#line 1707 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncDef: /* FuncDef  */
#line 256 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcDef); }
#line 1713 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_OptFuncFParamList: /* OptFuncFParamList  */
#line 257 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).FuncFParamList); }
#line 1719 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncFParamList: /* FuncFParamList  */
#line 257 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).FuncFParamList); }
#line 1725 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncFParam: /* FuncFParam  */
#line 257 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcFParam); }
#line 1731 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BlockItemList: /* BlockItemList  */
#line 258 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).blockItemList); }
#line 1737 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BlockItem: /* BlockItem  */
#line 258 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).blockItem); }
#line 1743 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Stmt: /* Stmt  */
#line 258 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1749 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_SelectStmt: /* SelectStmt  */
#line 259 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).selectStmt); }
#line 1755 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_IterationStmt: /* IterationStmt  */
#line 259 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).iterationStmt); }
#line 1761 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ReturnStmt: /* ReturnStmt  */
#line 258 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).returnStmt); }
#line 1767 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Exp: /* Exp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).addExp); }
#line 1773 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceExp: /* NonBraceExp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).addExp); }
#line 1779 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceAddExp: /* NonBraceAddExp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).addExp); }
#line 1785 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceMulExp: /* NonBraceMulExp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).mulExp); }
#line 1791 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceUnaryExp: /* NonBraceUnaryExp  */
#line 260 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).unaryExp); }
#line 1797 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Cond: /* Cond  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).lOrExp); }
#line 1803 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LVal: /* LVal  */
#line 259 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).lVal); }
#line 1809 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_PrimaryExp: /* PrimaryExp  */
#line 259 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).primaryExp); }
#line 1815 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Number: /* Number  */
#line 260 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).number); }
#line 1821 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_UnaryExp: /* UnaryExp  */
#line 260 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).unaryExp); }
#line 1827 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Call: /* Call  */
#line 260 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).call); }
#line 1833 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncCParamList: /* FuncCParamList  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).funcCParamList); }
#line 1839 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncCParam: /* FuncCParam  */
#line 260 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).callArg); }
#line 1845 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_MulExp: /* MulExp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).mulExp); }
#line 1851 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_AddExp: /* AddExp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).addExp); }
#line 1857 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_RelExp: /* RelExp  */
#line 261 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).relExp); }
#line 1863 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_EqExp: /* EqExp  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).eqExp); }
#line 1869 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LAndExp: /* LAndExp  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).lAndExp); }
#line 1875 "/workspace/src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LOrExp: /* LOrExp  */
#line 262 "/workspace/src/frontend/parser.y"
            { delete ((*yyvaluep).lOrExp); }
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
#line 271 "/workspace/src/frontend/parser.y"
             {
        root = unique_ptr<CompUnitAST>((yyvsp[0].compUnit));
    }
#line 2181 "/workspace/src/frontend/parser.cpp"
    break;

  case 3: /* CompUnit: CompUnit DeclDef  */
#line 277 "/workspace/src/frontend/parser.y"
                     {
        (yyval.compUnit) = (yyvsp[-1].compUnit);
        (yyval.compUnit)->declDefList.push_back(unique_ptr<DeclDefAST>((yyvsp[0].declDef)));
    }
#line 2190 "/workspace/src/frontend/parser.cpp"
    break;

  case 4: /* CompUnit: DeclDef  */
#line 281 "/workspace/src/frontend/parser.y"
            {
        (yyval.compUnit) = make_node<CompUnitAST>();
        (yyval.compUnit)->declDefList.push_back(unique_ptr<DeclDefAST>((yyvsp[0].declDef)));
    }
#line 2199 "/workspace/src/frontend/parser.cpp"
    break;

  case 5: /* DeclDef: Decl  */
#line 288 "/workspace/src/frontend/parser.y"
         {
        (yyval.declDef) = make_node<DeclDefAST>();
        (yyval.declDef)->Decl = unique_ptr<DeclAST>((yyvsp[0].decl));
    }
#line 2208 "/workspace/src/frontend/parser.cpp"
    break;

  case 6: /* DeclDef: FuncDef  */
#line 292 "/workspace/src/frontend/parser.y"
            {
        (yyval.declDef) = make_node<DeclDefAST>();
        (yyval.declDef)->funcDef = unique_ptr<FuncDefAST>((yyvsp[0].funcDef));
    }
#line 2217 "/workspace/src/frontend/parser.cpp"
    break;

  case 7: /* Decl: CONST BType ConstDefList SEMICOLON  */
#line 299 "/workspace/src/frontend/parser.y"
                                       {
        (yyval.decl) = make_node<DeclAST>();
        (yyval.decl)->isConst = true;
        (yyval.decl)->bType = *(yyvsp[-2].type_spec);
        delete (yyvsp[-2].type_spec);
        (yyval.decl)->defList.swap((yyvsp[-1].defList)->list);
        delete (yyvsp[-1].defList);
    }
#line 2230 "/workspace/src/frontend/parser.cpp"
    break;

  case 8: /* Decl: BType VarDefList SEMICOLON  */
#line 307 "/workspace/src/frontend/parser.y"
                               {
        (yyval.decl) = make_node<DeclAST>();
        (yyval.decl)->isConst = false;
        (yyval.decl)->bType = *(yyvsp[-2].type_spec);
        delete (yyvsp[-2].type_spec);
        (yyval.decl)->defList.swap((yyvsp[-1].defList)->list);
        delete (yyvsp[-1].defList);
    }
#line 2243 "/workspace/src/frontend/parser.cpp"
    break;

  case 9: /* BType: BASICTYPE  */
#line 318 "/workspace/src/frontend/parser.y"
              {
        (yyval.type_spec) = new TypeSpec(static_cast<TYPE>((yyvsp[0].int_val)));
    }
#line 2251 "/workspace/src/frontend/parser.cpp"
    break;

  case 10: /* BType: VecType  */
#line 321 "/workspace/src/frontend/parser.y"
            {
        (yyval.type_spec) = (yyvsp[0].type_spec);
    }
#line 2259 "/workspace/src/frontend/parser.cpp"
    break;

  case 11: /* VecType: ID  */
#line 330 "/workspace/src/frontend/parser.y"
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
#line 2274 "/workspace/src/frontend/parser.cpp"
    break;

  case 12: /* VecType: ID LT BASICTYPE COMMA VecWidth GT  */
#line 340 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-1].type_spec); YYERROR;
        }
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2288 "/workspace/src/frontend/parser.cpp"
    break;

  case 13: /* VecType: ID LT VecWidth COMMA BASICTYPE GT  */
#line 349 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-3].type_spec); YYERROR;
        }
        (yyvsp[-3].type_spec)->element = static_cast<TYPE>((yyvsp[-1].int_val));
        (yyval.type_spec) = (yyvsp[-3].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2302 "/workspace/src/frontend/parser.cpp"
    break;

  case 14: /* VecType: ID LP BASICTYPE COMMA VecWidth RP  */
#line 358 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-1].type_spec); YYERROR;
        }
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2316 "/workspace/src/frontend/parser.cpp"
    break;

  case 15: /* VecType: ID LB BASICTYPE COMMA VecWidth RB  */
#line 367 "/workspace/src/frontend/parser.y"
                                      {
        if (!isVectorConstructor(*(yyvsp[-5].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-5].token) + "'").c_str());
            delete (yyvsp[-5].token); delete (yyvsp[-1].type_spec); YYERROR;
        }
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
        delete (yyvsp[-5].token);
    }
#line 2330 "/workspace/src/frontend/parser.cpp"
    break;

  case 16: /* VecType: BASICTYPE LT VecWidth GT  */
#line 376 "/workspace/src/frontend/parser.y"
                             {
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 2339 "/workspace/src/frontend/parser.cpp"
    break;

  case 17: /* VecType: BASICTYPE LB VecWidth RB  */
#line 380 "/workspace/src/frontend/parser.y"
                             {
        (yyvsp[-1].type_spec)->element = static_cast<TYPE>((yyvsp[-3].int_val));
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 2348 "/workspace/src/frontend/parser.cpp"
    break;

  case 18: /* VecType: ID LT BASICTYPE GT  */
#line 384 "/workspace/src/frontend/parser.y"
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
#line 2364 "/workspace/src/frontend/parser.cpp"
    break;

  case 19: /* VecType: ID LP BASICTYPE RP  */
#line 395 "/workspace/src/frontend/parser.y"
                       {
        if (!isVectorConstructor(*(yyvsp[-3].token))) {
            yyerror(("unknown vector constructor '" + *(yyvsp[-3].token) + "'").c_str());
            delete (yyvsp[-3].token); YYERROR;
        }
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-1].int_val))));
        delete (yyvsp[-3].token);
    }
#line 2377 "/workspace/src/frontend/parser.cpp"
    break;

  case 20: /* VecType: BASICTYPE LT GT  */
#line 403 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-2].int_val))));
    }
#line 2385 "/workspace/src/frontend/parser.cpp"
    break;

  case 21: /* VecType: BASICTYPE LB RB  */
#line 406 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(static_cast<TYPE>((yyvsp[-2].int_val))));
    }
#line 2393 "/workspace/src/frontend/parser.cpp"
    break;

  case 22: /* VecWidth: INT  */
#line 412 "/workspace/src/frontend/parser.y"
        {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, (yyvsp[0].int_val)));
    }
#line 2401 "/workspace/src/frontend/parser.cpp"
    break;

  case 23: /* VecWidth: ID  */
#line 415 "/workspace/src/frontend/parser.y"
       {
        if (auto width = vectorWidthAlias(*(yyvsp[0].token)))
            (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *width));
        else
            (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 2413 "/workspace/src/frontend/parser.cpp"
    break;

  case 24: /* VoidType: VOID  */
#line 425 "/workspace/src/frontend/parser.y"
         {
        (yyval.type_spec) = new TypeSpec(TYPE_VOID);
    }
#line 2421 "/workspace/src/frontend/parser.cpp"
    break;

  case 25: /* ConstDefList: ConstDef  */
#line 431 "/workspace/src/frontend/parser.y"
             {
        (yyval.defList) = make_node<DefListAST>();
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2430 "/workspace/src/frontend/parser.cpp"
    break;

  case 26: /* ConstDefList: ConstDefList COMMA ConstDef  */
#line 435 "/workspace/src/frontend/parser.y"
                                {
        (yyval.defList) = (yyvsp[-2].defList);
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2439 "/workspace/src/frontend/parser.cpp"
    break;

  case 27: /* VarDefList: VarDef  */
#line 441 "/workspace/src/frontend/parser.y"
           {
        (yyval.defList) = make_node<DefListAST>();
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2448 "/workspace/src/frontend/parser.cpp"
    break;

  case 28: /* VarDefList: VarDefList COMMA VarDef  */
#line 445 "/workspace/src/frontend/parser.y"
                            {
        (yyval.defList) = (yyvsp[-2].defList);
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2457 "/workspace/src/frontend/parser.cpp"
    break;

  case 29: /* ConstDef: ID Arrays ASSIGN ConstInitVal  */
#line 454 "/workspace/src/frontend/parser.y"
                                  {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.def)->arrays.swap((yyvsp[-2].arrays)->list);
        delete (yyvsp[-2].arrays);
        (yyval.def)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 2469 "/workspace/src/frontend/parser.cpp"
    break;

  case 30: /* ConstDef: ID ASSIGN Exp  */
#line 461 "/workspace/src/frontend/parser.y"
                  {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.def)->initVal = unique_ptr<InitValAST>(make_node<InitValAST>());
        (yyval.def)->initVal->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2480 "/workspace/src/frontend/parser.cpp"
    break;

  case 31: /* VarDef: ID Arrays ASSIGN InitVal  */
#line 469 "/workspace/src/frontend/parser.y"
                             {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.def)->arrays.swap((yyvsp[-2].arrays)->list);
        delete (yyvsp[-2].arrays);
        (yyval.def)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 2492 "/workspace/src/frontend/parser.cpp"
    break;

  case 32: /* VarDef: ID ASSIGN Exp  */
#line 476 "/workspace/src/frontend/parser.y"
                  {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.def)->initVal = unique_ptr<InitValAST>(make_node<InitValAST>());
        (yyval.def)->initVal->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2503 "/workspace/src/frontend/parser.cpp"
    break;

  case 33: /* VarDef: ID Arrays  */
#line 482 "/workspace/src/frontend/parser.y"
              {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-1].token));
        (yyval.def)->arrays.swap((yyvsp[0].arrays)->list);
        delete (yyvsp[0].arrays);
    }
#line 2514 "/workspace/src/frontend/parser.cpp"
    break;

  case 34: /* VarDef: ID  */
#line 488 "/workspace/src/frontend/parser.y"
       {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[0].token));
    }
#line 2523 "/workspace/src/frontend/parser.cpp"
    break;

  case 35: /* Arrays: LB Exp RB  */
#line 495 "/workspace/src/frontend/parser.y"
              {
        (yyval.arrays) = make_node<ArraysAST>();
        (yyval.arrays)->list.push_back(unique_ptr<AddExpAST>((yyvsp[-1].addExp)));
    }
#line 2532 "/workspace/src/frontend/parser.cpp"
    break;

  case 36: /* Arrays: Arrays LB Exp RB  */
#line 499 "/workspace/src/frontend/parser.y"
                     {
        (yyval.arrays) = (yyvsp[-3].arrays);
        (yyval.arrays)->list.push_back(unique_ptr<AddExpAST>((yyvsp[-1].addExp)));
    }
#line 2541 "/workspace/src/frontend/parser.cpp"
    break;

  case 37: /* Block: LC RC  */
#line 510 "/workspace/src/frontend/parser.y"
          {
        (yyval.block) = make_node<BlockAST>();
    }
#line 2549 "/workspace/src/frontend/parser.cpp"
    break;

  case 38: /* Block: LC BlockItemList RC  */
#line 513 "/workspace/src/frontend/parser.y"
                        {
        (yyval.block) = make_node<BlockAST>();
        (yyval.block)->blockItemList.swap((yyvsp[-1].blockItemList)->list);
        delete (yyvsp[-1].blockItemList);
    }
#line 2559 "/workspace/src/frontend/parser.cpp"
    break;

  case 39: /* InitVal: LC RC  */
#line 524 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2567 "/workspace/src/frontend/parser.cpp"
    break;

  case 40: /* InitVal: LC InitValList RC  */
#line 527 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->initValList.swap((yyvsp[-1].initValList)->list);
        delete (yyvsp[-1].initValList);
    }
#line 2577 "/workspace/src/frontend/parser.cpp"
    break;

  case 41: /* InitVal: NonBraceExp  */
#line 532 "/workspace/src/frontend/parser.y"
                {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2586 "/workspace/src/frontend/parser.cpp"
    break;

  case 42: /* ConstInitVal: LC RC  */
#line 538 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2594 "/workspace/src/frontend/parser.cpp"
    break;

  case 43: /* ConstInitVal: LC InitValList RC  */
#line 541 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->initValList.swap((yyvsp[-1].initValList)->list);
        delete (yyvsp[-1].initValList);
    }
#line 2604 "/workspace/src/frontend/parser.cpp"
    break;

  case 44: /* ConstInitVal: NonBraceExp  */
#line 546 "/workspace/src/frontend/parser.y"
                {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2613 "/workspace/src/frontend/parser.cpp"
    break;

  case 45: /* BraceInitVal: LC RC  */
#line 553 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2621 "/workspace/src/frontend/parser.cpp"
    break;

  case 46: /* BraceInitVal: LC InitValList RC  */
#line 556 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->initValList.swap((yyvsp[-1].initValList)->list);
        delete (yyvsp[-1].initValList);
    }
#line 2631 "/workspace/src/frontend/parser.cpp"
    break;

  case 47: /* InitValList: InitValList COMMA InitVal  */
#line 564 "/workspace/src/frontend/parser.y"
                            {
    (yyval.initValList) = (yyvsp[-2].initValList);
    (yyval.initValList)->list.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2640 "/workspace/src/frontend/parser.cpp"
    break;

  case 48: /* InitValList: InitVal  */
#line 568 "/workspace/src/frontend/parser.y"
          {
    (yyval.initValList) = make_node<InitValListAST>();
    (yyval.initValList)->list.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2649 "/workspace/src/frontend/parser.cpp"
    break;

  case 49: /* FuncDef: BType ID LP OptFuncFParamList RP Block  */
#line 575 "/workspace/src/frontend/parser.y"
                                           {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-5].type_spec);
        delete (yyvsp[-5].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-4].token));
        (yyval.funcDef)->funcFParamList.swap((yyvsp[-2].FuncFParamList)->list);
        delete (yyvsp[-2].FuncFParamList);
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2663 "/workspace/src/frontend/parser.cpp"
    break;

  case 50: /* FuncDef: VoidType ID LP OptFuncFParamList RP Block  */
#line 584 "/workspace/src/frontend/parser.y"
                                              {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-5].type_spec);
        delete (yyvsp[-5].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-4].token));
        (yyval.funcDef)->funcFParamList.swap((yyvsp[-2].FuncFParamList)->list);
        delete (yyvsp[-2].FuncFParamList);
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2677 "/workspace/src/frontend/parser.cpp"
    break;

  case 51: /* OptFuncFParamList: %empty  */
#line 595 "/workspace/src/frontend/parser.y"
           {
        (yyval.FuncFParamList) = make_node<FuncFParamListAST>();
    }
#line 2685 "/workspace/src/frontend/parser.cpp"
    break;

  case 52: /* OptFuncFParamList: FuncFParamList  */
#line 598 "/workspace/src/frontend/parser.y"
                   {
        (yyval.FuncFParamList) = (yyvsp[0].FuncFParamList);
    }
#line 2693 "/workspace/src/frontend/parser.cpp"
    break;

  case 53: /* FuncFParamList: FuncFParam  */
#line 604 "/workspace/src/frontend/parser.y"
               {
        (yyval.FuncFParamList) = make_node<FuncFParamListAST>();
        (yyval.FuncFParamList)->list.push_back(unique_ptr<FuncFParamAST>((yyvsp[0].funcFParam)));
    }
#line 2702 "/workspace/src/frontend/parser.cpp"
    break;

  case 54: /* FuncFParamList: FuncFParamList COMMA FuncFParam  */
#line 608 "/workspace/src/frontend/parser.y"
                                    {
        (yyval.FuncFParamList) = (yyvsp[-2].FuncFParamList);
        (yyval.FuncFParamList)->list.push_back(unique_ptr<FuncFParamAST>((yyvsp[0].funcFParam)));
    }
#line 2711 "/workspace/src/frontend/parser.cpp"
    break;

  case 55: /* FuncFParam: BType ID  */
#line 615 "/workspace/src/frontend/parser.y"
             {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-1].type_spec);
        delete (yyvsp[-1].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[0].token));
        (yyval.funcFParam)->isArray = false;
    }
#line 2723 "/workspace/src/frontend/parser.cpp"
    break;

  case 56: /* FuncFParam: BType ID LB RB  */
#line 622 "/workspace/src/frontend/parser.y"
                   {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-3].type_spec);
        delete (yyvsp[-3].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.funcFParam)->isArray = true;
    }
#line 2735 "/workspace/src/frontend/parser.cpp"
    break;

  case 57: /* FuncFParam: BType ID LB RB Arrays  */
#line 629 "/workspace/src/frontend/parser.y"
                          {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcFParam)->isArray = true;
        (yyval.funcFParam)->arrays.swap((yyvsp[0].arrays)->list);
        delete (yyvsp[0].arrays);
    }
#line 2749 "/workspace/src/frontend/parser.cpp"
    break;

  case 58: /* BlockItemList: BlockItem  */
#line 641 "/workspace/src/frontend/parser.y"
              {
        (yyval.blockItemList) = make_node<BlockItemListAST>();
        (yyval.blockItemList)->list.push_back(unique_ptr<BlockItemAST>((yyvsp[0].blockItem)));
    }
#line 2758 "/workspace/src/frontend/parser.cpp"
    break;

  case 59: /* BlockItemList: BlockItemList BlockItem  */
#line 645 "/workspace/src/frontend/parser.y"
                            {
        (yyval.blockItemList) = (yyvsp[-1].blockItemList);
        (yyval.blockItemList)->list.push_back(unique_ptr<BlockItemAST>((yyvsp[0].blockItem)));
    }
#line 2767 "/workspace/src/frontend/parser.cpp"
    break;

  case 60: /* BlockItem: Decl  */
#line 652 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>();
        (yyval.blockItem)->decl = unique_ptr<DeclAST>((yyvsp[0].decl));
    }
#line 2776 "/workspace/src/frontend/parser.cpp"
    break;

  case 61: /* BlockItem: Stmt  */
#line 656 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>();
        (yyval.blockItem)->stmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2785 "/workspace/src/frontend/parser.cpp"
    break;

  case 62: /* Stmt: SEMICOLON  */
#line 663 "/workspace/src/frontend/parser.y"
              {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = SEMI;
    }
#line 2794 "/workspace/src/frontend/parser.cpp"
    break;

  case 63: /* Stmt: LVal ASSIGN Exp SEMICOLON  */
#line 667 "/workspace/src/frontend/parser.y"
                              {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = ASS;
        (yyval.stmt)->lVal = unique_ptr<LValAST>((yyvsp[-3].lVal));
        (yyval.stmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2805 "/workspace/src/frontend/parser.cpp"
    break;

  case 64: /* Stmt: NonBraceExp SEMICOLON  */
#line 673 "/workspace/src/frontend/parser.y"
                          {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = EXP;
        (yyval.stmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2815 "/workspace/src/frontend/parser.cpp"
    break;

  case 65: /* Stmt: CONTINUE SEMICOLON  */
#line 678 "/workspace/src/frontend/parser.y"
                       {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = CONT;
    }
#line 2824 "/workspace/src/frontend/parser.cpp"
    break;

  case 66: /* Stmt: BREAK SEMICOLON  */
#line 682 "/workspace/src/frontend/parser.y"
                    {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = BRE;
    }
#line 2833 "/workspace/src/frontend/parser.cpp"
    break;

  case 67: /* Stmt: Block  */
#line 686 "/workspace/src/frontend/parser.y"
          {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = BLK;
        (yyval.stmt)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2843 "/workspace/src/frontend/parser.cpp"
    break;

  case 68: /* Stmt: ReturnStmt  */
#line 691 "/workspace/src/frontend/parser.y"
               {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = RET;
        (yyval.stmt)->returnStmt = unique_ptr<ReturnStmtAST>((yyvsp[0].returnStmt));
    }
#line 2853 "/workspace/src/frontend/parser.cpp"
    break;

  case 69: /* Stmt: SelectStmt  */
#line 696 "/workspace/src/frontend/parser.y"
               {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = SEL;
        (yyval.stmt)->selectStmt = unique_ptr<SelectStmtAST>((yyvsp[0].selectStmt));
    }
#line 2863 "/workspace/src/frontend/parser.cpp"
    break;

  case 70: /* Stmt: IterationStmt  */
#line 701 "/workspace/src/frontend/parser.y"
                  {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = ITER;
        (yyval.stmt)->iterationStmt = unique_ptr<IterationStmtAST>((yyvsp[0].iterationStmt));
    }
#line 2873 "/workspace/src/frontend/parser.cpp"
    break;

  case 71: /* SelectStmt: IF LP Cond RP Stmt  */
#line 709 "/workspace/src/frontend/parser.y"
                                             {
        (yyval.selectStmt) = make_node<SelectStmtAST>();
        (yyval.selectStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.selectStmt)->ifStmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2883 "/workspace/src/frontend/parser.cpp"
    break;

  case 72: /* SelectStmt: IF LP Cond RP Stmt ELSE Stmt  */
#line 714 "/workspace/src/frontend/parser.y"
                                 {
        (yyval.selectStmt) = make_node<SelectStmtAST>();
        (yyval.selectStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-4].lOrExp));
        (yyval.selectStmt)->ifStmt = unique_ptr<StmtAST>((yyvsp[-2].stmt));
        (yyval.selectStmt)->elseStmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2894 "/workspace/src/frontend/parser.cpp"
    break;

  case 73: /* IterationStmt: WHILE LP Cond RP Stmt  */
#line 723 "/workspace/src/frontend/parser.y"
                          {
        (yyval.iterationStmt) = make_node<IterationStmtAST>();
        (yyval.iterationStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.iterationStmt)->stmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2904 "/workspace/src/frontend/parser.cpp"
    break;

  case 74: /* ReturnStmt: RETURN Exp SEMICOLON  */
#line 731 "/workspace/src/frontend/parser.y"
                         {
        (yyval.returnStmt) = make_node<ReturnStmtAST>();
        (yyval.returnStmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2913 "/workspace/src/frontend/parser.cpp"
    break;

  case 75: /* ReturnStmt: RETURN SEMICOLON  */
#line 735 "/workspace/src/frontend/parser.y"
                     {
        (yyval.returnStmt) = make_node<ReturnStmtAST>();
    }
#line 2921 "/workspace/src/frontend/parser.cpp"
    break;

  case 76: /* Exp: AddExp  */
#line 741 "/workspace/src/frontend/parser.y"
           {
        (yyval.addExp) = (yyvsp[0].addExp);
    }
#line 2929 "/workspace/src/frontend/parser.cpp"
    break;

  case 77: /* NonBraceExp: NonBraceAddExp  */
#line 750 "/workspace/src/frontend/parser.y"
                   { (yyval.addExp) = (yyvsp[0].addExp); }
#line 2935 "/workspace/src/frontend/parser.cpp"
    break;

  case 78: /* NonBraceAddExp: NonBraceMulExp  */
#line 753 "/workspace/src/frontend/parser.y"
                   {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2944 "/workspace/src/frontend/parser.cpp"
    break;

  case 79: /* NonBraceAddExp: NonBraceAddExp ADD MulExp  */
#line 757 "/workspace/src/frontend/parser.y"
                              {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_ADD;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2955 "/workspace/src/frontend/parser.cpp"
    break;

  case 80: /* NonBraceAddExp: NonBraceAddExp MINUS MulExp  */
#line 763 "/workspace/src/frontend/parser.y"
                                {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_MINUS;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2966 "/workspace/src/frontend/parser.cpp"
    break;

  case 81: /* NonBraceMulExp: NonBraceUnaryExp  */
#line 771 "/workspace/src/frontend/parser.y"
                     {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2975 "/workspace/src/frontend/parser.cpp"
    break;

  case 82: /* NonBraceMulExp: NonBraceMulExp MUL UnaryExp  */
#line 775 "/workspace/src/frontend/parser.y"
                                {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MUL;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2986 "/workspace/src/frontend/parser.cpp"
    break;

  case 83: /* NonBraceMulExp: NonBraceMulExp DIV UnaryExp  */
#line 781 "/workspace/src/frontend/parser.y"
                                {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_DIV;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2997 "/workspace/src/frontend/parser.cpp"
    break;

  case 84: /* NonBraceMulExp: NonBraceMulExp MOD UnaryExp  */
#line 787 "/workspace/src/frontend/parser.y"
                                {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MOD;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3008 "/workspace/src/frontend/parser.cpp"
    break;

  case 85: /* NonBraceUnaryExp: LP Exp RP  */
#line 795 "/workspace/src/frontend/parser.y"
              {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
        (yyval.unaryExp)->primaryExp = unique_ptr<PrimaryExpAST>(primary);
    }
#line 3019 "/workspace/src/frontend/parser.cpp"
    break;

  case 86: /* NonBraceUnaryExp: LVal  */
#line 801 "/workspace/src/frontend/parser.y"
         {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->lval = unique_ptr<LValAST>((yyvsp[0].lVal));
        (yyval.unaryExp)->primaryExp = unique_ptr<PrimaryExpAST>(primary);
    }
#line 3030 "/workspace/src/frontend/parser.cpp"
    break;

  case 87: /* NonBraceUnaryExp: Number  */
#line 807 "/workspace/src/frontend/parser.y"
           {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->number = unique_ptr<NumberAST>((yyvsp[0].number));
        (yyval.unaryExp)->primaryExp = unique_ptr<PrimaryExpAST>(primary);
    }
#line 3041 "/workspace/src/frontend/parser.cpp"
    break;

  case 88: /* NonBraceUnaryExp: Call  */
#line 813 "/workspace/src/frontend/parser.y"
         {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->call = unique_ptr<CallAST>((yyvsp[0].call));
    }
#line 3050 "/workspace/src/frontend/parser.cpp"
    break;

  case 89: /* NonBraceUnaryExp: Call LB Exp RB  */
#line 817 "/workspace/src/frontend/parser.y"
                   {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *base = make_node<UnaryExpAST>();
        base->call = unique_ptr<CallAST>((yyvsp[-3].call));
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>(base);
        (yyval.unaryExp)->subscript = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 3062 "/workspace/src/frontend/parser.cpp"
    break;

  case 90: /* NonBraceUnaryExp: LP Exp RP LB Exp RB  */
#line 824 "/workspace/src/frontend/parser.y"
                        {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>((yyvsp[-4].addExp));
        auto *base = make_node<UnaryExpAST>();
        base->primaryExp = unique_ptr<PrimaryExpAST>(primary);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>(base);
        (yyval.unaryExp)->subscript = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 3076 "/workspace/src/frontend/parser.cpp"
    break;

  case 91: /* NonBraceUnaryExp: UnaryOp UnaryExp  */
#line 833 "/workspace/src/frontend/parser.y"
                     {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->op = (yyvsp[-1].op);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3086 "/workspace/src/frontend/parser.cpp"
    break;

  case 92: /* Cond: LOrExp  */
#line 841 "/workspace/src/frontend/parser.y"
           {
        (yyval.lOrExp) = (yyvsp[0].lOrExp);
    }
#line 3094 "/workspace/src/frontend/parser.cpp"
    break;

  case 93: /* LVal: ID  */
#line 847 "/workspace/src/frontend/parser.y"
       {
        (yyval.lVal) = make_node<LValAST>();
        (yyval.lVal)->id = unique_ptr<string>((yyvsp[0].token));
    }
#line 3103 "/workspace/src/frontend/parser.cpp"
    break;

  case 94: /* LVal: ID Arrays  */
#line 851 "/workspace/src/frontend/parser.y"
              {
        (yyval.lVal) = make_node<LValAST>();
        (yyval.lVal)->id = unique_ptr<string>((yyvsp[-1].token));
        (yyval.lVal)->arrays.swap((yyvsp[0].arrays)->list);
        delete (yyvsp[0].arrays);
    }
#line 3114 "/workspace/src/frontend/parser.cpp"
    break;

  case 95: /* PrimaryExp: LP Exp RP  */
#line 860 "/workspace/src/frontend/parser.y"
              {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 3123 "/workspace/src/frontend/parser.cpp"
    break;

  case 96: /* PrimaryExp: LVal  */
#line 864 "/workspace/src/frontend/parser.y"
         {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->lval = unique_ptr<LValAST>((yyvsp[0].lVal));
    }
#line 3132 "/workspace/src/frontend/parser.cpp"
    break;

  case 97: /* PrimaryExp: Number  */
#line 868 "/workspace/src/frontend/parser.y"
           {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->number = unique_ptr<NumberAST>((yyvsp[0].number));
    }
#line 3141 "/workspace/src/frontend/parser.cpp"
    break;

  case 98: /* PrimaryExp: BraceInitVal  */
#line 872 "/workspace/src/frontend/parser.y"
                 {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 3150 "/workspace/src/frontend/parser.cpp"
    break;

  case 99: /* Number: INT  */
#line 879 "/workspace/src/frontend/parser.y"
        {
        (yyval.number) = make_node<NumberAST>();
        (yyval.number)->isInt = true;
        (yyval.number)->intval = (yyvsp[0].int_val);
    }
#line 3160 "/workspace/src/frontend/parser.cpp"
    break;

  case 100: /* Number: FLOAT  */
#line 884 "/workspace/src/frontend/parser.y"
          {
        (yyval.number) = make_node<NumberAST>();
        (yyval.number)->isInt = false;
        (yyval.number)->floatval = (yyvsp[0].float_val);
    }
#line 3170 "/workspace/src/frontend/parser.cpp"
    break;

  case 101: /* UnaryExp: PrimaryExp  */
#line 894 "/workspace/src/frontend/parser.y"
               {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->primaryExp = unique_ptr<PrimaryExpAST>((yyvsp[0].primaryExp));
    }
#line 3179 "/workspace/src/frontend/parser.cpp"
    break;

  case 102: /* UnaryExp: Call  */
#line 898 "/workspace/src/frontend/parser.y"
         {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->call = unique_ptr<CallAST>((yyvsp[0].call));
    }
#line 3188 "/workspace/src/frontend/parser.cpp"
    break;

  case 103: /* UnaryExp: Call LB Exp RB  */
#line 902 "/workspace/src/frontend/parser.y"
                   {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *base = make_node<UnaryExpAST>();
        base->call = unique_ptr<CallAST>((yyvsp[-3].call));
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>(base);
        (yyval.unaryExp)->subscript = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 3200 "/workspace/src/frontend/parser.cpp"
    break;

  case 104: /* UnaryExp: LP Exp RP LB Exp RB  */
#line 909 "/workspace/src/frontend/parser.y"
                        {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>((yyvsp[-4].addExp));
        auto *base = make_node<UnaryExpAST>();
        base->primaryExp = unique_ptr<PrimaryExpAST>(primary);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>(base);
        (yyval.unaryExp)->subscript = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 3214 "/workspace/src/frontend/parser.cpp"
    break;

  case 105: /* UnaryExp: UnaryOp UnaryExp  */
#line 918 "/workspace/src/frontend/parser.y"
                     {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->op = (yyvsp[-1].op);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3224 "/workspace/src/frontend/parser.cpp"
    break;

  case 106: /* Call: ID LP RP  */
#line 926 "/workspace/src/frontend/parser.y"
             {
        (yyval.call) = make_node<CallAST>();
        (yyval.call)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.call)->lineno = (yylsp[-2]).first_line;
    }
#line 3234 "/workspace/src/frontend/parser.cpp"
    break;

  case 107: /* Call: ID LP FuncCParamList RP  */
#line 931 "/workspace/src/frontend/parser.y"
                            {
        (yyval.call) = make_node<CallAST>();
        (yyval.call)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.call)->funcCParamList.swap((yyvsp[-1].funcCParamList)->list);
        delete (yyvsp[-1].funcCParamList);
        (yyval.call)->lineno = (yylsp[-3]).first_line;
    }
#line 3246 "/workspace/src/frontend/parser.cpp"
    break;

  case 108: /* UnaryOp: ADD  */
#line 941 "/workspace/src/frontend/parser.y"
        {
        (yyval.op) = UOP_ADD;
    }
#line 3254 "/workspace/src/frontend/parser.cpp"
    break;

  case 109: /* UnaryOp: MINUS  */
#line 944 "/workspace/src/frontend/parser.y"
          {
        (yyval.op) = UOP_MINUS;
    }
#line 3262 "/workspace/src/frontend/parser.cpp"
    break;

  case 110: /* UnaryOp: NOT  */
#line 947 "/workspace/src/frontend/parser.y"
        {
        (yyval.op) = UOP_NOT;
    }
#line 3270 "/workspace/src/frontend/parser.cpp"
    break;

  case 111: /* FuncCParamList: FuncCParam  */
#line 953 "/workspace/src/frontend/parser.y"
               {
        (yyval.funcCParamList) = make_node<FuncCParamListAST>();
        (yyval.funcCParamList)->list.push_back(unique_ptr<CallArgAST>((yyvsp[0].callArg)));
    }
#line 3279 "/workspace/src/frontend/parser.cpp"
    break;

  case 112: /* FuncCParamList: FuncCParamList COMMA FuncCParam  */
#line 957 "/workspace/src/frontend/parser.y"
                                    {
        (yyval.funcCParamList) = (yyvsp[-2].funcCParamList);
        (yyval.funcCParamList)->list.push_back(unique_ptr<CallArgAST>((yyvsp[0].callArg)));
    }
#line 3288 "/workspace/src/frontend/parser.cpp"
    break;

  case 113: /* FuncCParam: Exp  */
#line 963 "/workspace/src/frontend/parser.y"
        {
        (yyval.callArg) = make_node<CallArgAST>();
        (yyval.callArg)->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 3297 "/workspace/src/frontend/parser.cpp"
    break;

  case 114: /* FuncCParam: STRING_LITERAL  */
#line 967 "/workspace/src/frontend/parser.y"
                   {
        string decoded;
        if (!decodeStringLiteral(*(yyvsp[0].token), decoded)) {
            yyerror("invalid escape sequence in string literal");
            delete (yyvsp[0].token);
            YYERROR;
        }
        (yyval.callArg) = make_node<CallArgAST>();
        (yyval.callArg)->stringLiteral = unique_ptr<string>(new string(std::move(decoded)));
        delete (yyvsp[0].token);
    }
#line 3313 "/workspace/src/frontend/parser.cpp"
    break;

  case 115: /* MulExp: UnaryExp  */
#line 981 "/workspace/src/frontend/parser.y"
             {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3322 "/workspace/src/frontend/parser.cpp"
    break;

  case 116: /* MulExp: MulExp MUL UnaryExp  */
#line 985 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MUL;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3333 "/workspace/src/frontend/parser.cpp"
    break;

  case 117: /* MulExp: MulExp DIV UnaryExp  */
#line 991 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_DIV;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3344 "/workspace/src/frontend/parser.cpp"
    break;

  case 118: /* MulExp: MulExp MOD UnaryExp  */
#line 997 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MOD;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 3355 "/workspace/src/frontend/parser.cpp"
    break;

  case 119: /* AddExp: MulExp  */
#line 1006 "/workspace/src/frontend/parser.y"
           {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 3364 "/workspace/src/frontend/parser.cpp"
    break;

  case 120: /* AddExp: AddExp ADD MulExp  */
#line 1010 "/workspace/src/frontend/parser.y"
                      {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_ADD;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 3375 "/workspace/src/frontend/parser.cpp"
    break;

  case 121: /* AddExp: AddExp MINUS MulExp  */
#line 1016 "/workspace/src/frontend/parser.y"
                        {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_MINUS;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 3386 "/workspace/src/frontend/parser.cpp"
    break;

  case 122: /* RelExp: AddExp  */
#line 1025 "/workspace/src/frontend/parser.y"
           {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 3395 "/workspace/src/frontend/parser.cpp"
    break;

  case 123: /* RelExp: RelExp GTE AddExp  */
#line 1029 "/workspace/src/frontend/parser.y"
                      {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_GTE;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 3406 "/workspace/src/frontend/parser.cpp"
    break;

  case 124: /* RelExp: RelExp LTE AddExp  */
#line 1035 "/workspace/src/frontend/parser.y"
                      {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_LTE;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 3417 "/workspace/src/frontend/parser.cpp"
    break;

  case 125: /* RelExp: RelExp GT AddExp  */
#line 1041 "/workspace/src/frontend/parser.y"
                     {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_GT;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 3428 "/workspace/src/frontend/parser.cpp"
    break;

  case 126: /* RelExp: RelExp LT AddExp  */
#line 1047 "/workspace/src/frontend/parser.y"
                     {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_LT;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 3439 "/workspace/src/frontend/parser.cpp"
    break;

  case 127: /* EqExp: RelExp  */
#line 1056 "/workspace/src/frontend/parser.y"
           {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 3448 "/workspace/src/frontend/parser.cpp"
    break;

  case 128: /* EqExp: EqExp EQ RelExp  */
#line 1060 "/workspace/src/frontend/parser.y"
                    {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[-2].eqExp));
        (yyval.eqExp)->op = EOP_EQ;
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 3459 "/workspace/src/frontend/parser.cpp"
    break;

  case 129: /* EqExp: EqExp NEQ RelExp  */
#line 1066 "/workspace/src/frontend/parser.y"
                     {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[-2].eqExp));
        (yyval.eqExp)->op = EOP_NEQ;
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 3470 "/workspace/src/frontend/parser.cpp"
    break;

  case 130: /* LAndExp: EqExp  */
#line 1075 "/workspace/src/frontend/parser.y"
          {
        (yyval.lAndExp) = make_node<LAndExpAST>();
        (yyval.lAndExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[0].eqExp));
    }
#line 3479 "/workspace/src/frontend/parser.cpp"
    break;

  case 131: /* LAndExp: LAndExp AND EqExp  */
#line 1079 "/workspace/src/frontend/parser.y"
                      {
        (yyval.lAndExp) = make_node<LAndExpAST>();
        (yyval.lAndExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[-2].lAndExp));
        (yyval.lAndExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[0].eqExp));
    }
#line 3489 "/workspace/src/frontend/parser.cpp"
    break;

  case 132: /* LOrExp: LAndExp  */
#line 1087 "/workspace/src/frontend/parser.y"
            {
        (yyval.lOrExp) = make_node<LOrExpAST>();
        (yyval.lOrExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[0].lAndExp));
    }
#line 3498 "/workspace/src/frontend/parser.cpp"
    break;

  case 133: /* LOrExp: LOrExp OR LAndExp  */
#line 1091 "/workspace/src/frontend/parser.y"
                      {
        (yyval.lOrExp) = make_node<LOrExpAST>();
        (yyval.lOrExp)->lOrExp = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.lOrExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[0].lAndExp));
    }
#line 3508 "/workspace/src/frontend/parser.cpp"
    break;


#line 3512 "/workspace/src/frontend/parser.cpp"

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

#line 1096 "/workspace/src/frontend/parser.y"


void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
