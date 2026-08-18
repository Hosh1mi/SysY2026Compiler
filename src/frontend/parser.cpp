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
#line 25 "src/frontend/parser.y"

    #include <cstdio>
    #include <cstdlib>
    #include <cctype>
    #include <iostream>
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

#line 151 "src/frontend/parser.cpp"

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
  YYSYMBOL_TENSOR = 23,                    /* TENSOR  */
  YYSYMBOL_LP = 24,                        /* LP  */
  YYSYMBOL_RP = 25,                        /* RP  */
  YYSYMBOL_LB = 26,                        /* LB  */
  YYSYMBOL_RB = 27,                        /* RB  */
  YYSYMBOL_LC = 28,                        /* LC  */
  YYSYMBOL_RC = 29,                        /* RC  */
  YYSYMBOL_COMMA = 30,                     /* COMMA  */
  YYSYMBOL_SEMICOLON = 31,                 /* SEMICOLON  */
  YYSYMBOL_NOT = 32,                       /* NOT  */
  YYSYMBOL_ASSIGN = 33,                    /* ASSIGN  */
  YYSYMBOL_MINUS = 34,                     /* MINUS  */
  YYSYMBOL_ADD = 35,                       /* ADD  */
  YYSYMBOL_MUL = 36,                       /* MUL  */
  YYSYMBOL_DIV = 37,                       /* DIV  */
  YYSYMBOL_MOD = 38,                       /* MOD  */
  YYSYMBOL_AND = 39,                       /* AND  */
  YYSYMBOL_OR = 40,                        /* OR  */
  YYSYMBOL_MATMUL = 41,                    /* MATMUL  */
  YYSYMBOL_LOWER_THEN_ELSE = 42,           /* LOWER_THEN_ELSE  */
  YYSYMBOL_YYACCEPT = 43,                  /* $accept  */
  YYSYMBOL_Program = 44,                   /* Program  */
  YYSYMBOL_CompUnit = 45,                  /* CompUnit  */
  YYSYMBOL_DeclDef = 46,                   /* DeclDef  */
  YYSYMBOL_Decl = 47,                      /* Decl  */
  YYSYMBOL_BType = 48,                     /* BType  */
  YYSYMBOL_TensorType = 49,                /* TensorType  */
  YYSYMBOL_VoidType = 50,                  /* VoidType  */
  YYSYMBOL_ConstDefList = 51,              /* ConstDefList  */
  YYSYMBOL_VarDefList = 52,                /* VarDefList  */
  YYSYMBOL_ConstDef = 53,                  /* ConstDef  */
  YYSYMBOL_VarDef = 54,                    /* VarDef  */
  YYSYMBOL_Arrays = 55,                    /* Arrays  */
  YYSYMBOL_Block = 56,                     /* Block  */
  YYSYMBOL_InitVal = 57,                   /* InitVal  */
  YYSYMBOL_ConstInitVal = 58,              /* ConstInitVal  */
  YYSYMBOL_BraceInitVal = 59,              /* BraceInitVal  */
  YYSYMBOL_InitValList = 60,               /* InitValList  */
  YYSYMBOL_FuncDef = 61,                   /* FuncDef  */
  YYSYMBOL_OptFuncFParamList = 62,         /* OptFuncFParamList  */
  YYSYMBOL_FuncFParamList = 63,            /* FuncFParamList  */
  YYSYMBOL_FuncFParam = 64,                /* FuncFParam  */
  YYSYMBOL_BlockItemList = 65,             /* BlockItemList  */
  YYSYMBOL_BlockItem = 66,                 /* BlockItem  */
  YYSYMBOL_Stmt = 67,                      /* Stmt  */
  YYSYMBOL_SelectStmt = 68,                /* SelectStmt  */
  YYSYMBOL_IterationStmt = 69,             /* IterationStmt  */
  YYSYMBOL_ReturnStmt = 70,                /* ReturnStmt  */
  YYSYMBOL_Exp = 71,                       /* Exp  */
  YYSYMBOL_NonBraceExp = 72,               /* NonBraceExp  */
  YYSYMBOL_NonBraceAddExp = 73,            /* NonBraceAddExp  */
  YYSYMBOL_NonBraceMulExp = 74,            /* NonBraceMulExp  */
  YYSYMBOL_NonBraceUnaryExp = 75,          /* NonBraceUnaryExp  */
  YYSYMBOL_Cond = 76,                      /* Cond  */
  YYSYMBOL_LVal = 77,                      /* LVal  */
  YYSYMBOL_PrimaryExp = 78,                /* PrimaryExp  */
  YYSYMBOL_Number = 79,                    /* Number  */
  YYSYMBOL_UnaryExp = 80,                  /* UnaryExp  */
  YYSYMBOL_Call = 81,                      /* Call  */
  YYSYMBOL_UnaryOp = 82,                   /* UnaryOp  */
  YYSYMBOL_FuncCParamList = 83,            /* FuncCParamList  */
  YYSYMBOL_FuncCParam = 84,                /* FuncCParam  */
  YYSYMBOL_MulExp = 85,                    /* MulExp  */
  YYSYMBOL_AddExp = 86,                    /* AddExp  */
  YYSYMBOL_RelExp = 87,                    /* RelExp  */
  YYSYMBOL_EqExp = 88,                     /* EqExp  */
  YYSYMBOL_LAndExp = 89,                   /* LAndExp  */
  YYSYMBOL_LOrExp = 90                     /* LOrExp  */
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
#define YYFINAL  15
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   388

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  43
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  48
/* YYNRULES -- Number of rules.  */
#define YYNRULES  123
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  218

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   297


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
      35,    36,    37,    38,    39,    40,    41,    42
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   190,   190,   196,   201,   209,   210,   214,   218,   225,
     228,   233,   239,   245,   249,   255,   259,   268,   273,   282,
     287,   294,   298,   305,   309,   320,   323,   334,   337,   341,
     346,   349,   353,   359,   362,   369,   373,   380,   385,   392,
     395,   401,   405,   412,   416,   420,   428,   433,   441,   444,
     451,   454,   458,   461,   464,   467,   470,   471,   472,   476,
     480,   488,   495,   498,   504,   513,   516,   517,   522,   529,
     530,   535,   540,   545,   552,   553,   554,   555,   556,   560,
     564,   570,   576,   580,   589,   590,   591,   592,   598,   601,
     609,   610,   611,   615,   619,   625,   629,   638,   641,   644,
     650,   655,   662,   665,   678,   679,   684,   689,   694,   702,
     703,   708,   716,   717,   722,   727,   732,   740,   741,   746,
     754,   755,   763,   764
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
  "CONTINUE", "TENSOR", "LP", "RP", "LB", "RB", "LC", "RC", "COMMA",
  "SEMICOLON", "NOT", "ASSIGN", "MINUS", "ADD", "MUL", "DIV", "MOD", "AND",
  "OR", "MATMUL", "LOWER_THEN_ELSE", "$accept", "Program", "CompUnit",
  "DeclDef", "Decl", "BType", "TensorType", "VoidType", "ConstDefList",
  "VarDefList", "ConstDef", "VarDef", "Arrays", "Block", "InitVal",
  "ConstInitVal", "BraceInitVal", "InitValList", "FuncDef",
  "OptFuncFParamList", "FuncFParamList", "FuncFParam", "BlockItemList",
  "BlockItem", "Stmt", "SelectStmt", "IterationStmt", "ReturnStmt", "Exp",
  "NonBraceExp", "NonBraceAddExp", "NonBraceMulExp", "NonBraceUnaryExp",
  "Cond", "LVal", "PrimaryExp", "Number", "UnaryExp", "Call", "UnaryOp",
  "FuncCParamList", "FuncCParam", "MulExp", "AddExp", "RelExp", "EqExp",
  "LAndExp", "LOrExp", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-127)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      78,  -127,  -127,     2,    -1,    46,    78,  -127,  -127,    23,
    -127,    79,  -127,   105,  -127,  -127,  -127,    87,    45,  -127,
      88,    12,    66,  -127,     2,   339,   339,    16,   109,  -127,
       2,   339,    17,   105,  -127,   118,   102,    99,  -127,  -127,
    -127,     3,   339,   250,  -127,  -127,  -127,  -127,   106,  -127,
    -127,  -127,  -127,   111,   339,    28,    13,  -127,   339,   344,
      37,  -127,   115,  -127,   353,  -127,   129,   103,     2,   241,
     133,   124,   339,   259,  -127,  -127,    70,  -127,   123,    42,
    -127,  -127,  -127,   151,   339,  -127,   339,  -127,   339,   339,
     339,   339,   339,   339,   162,  -127,   103,   292,  -127,  -127,
     167,   175,  -127,  -127,  -127,  -127,  -127,    68,  -127,   176,
     180,  -127,   152,  -127,   344,   339,   339,   339,   339,   339,
     339,   339,  -127,   181,  -127,  -127,  -127,  -127,    28,    28,
    -127,  -127,  -127,   154,   188,   301,   191,   192,   186,   187,
    -127,  -127,  -127,   109,  -127,   208,  -127,  -127,  -127,  -127,
    -127,   189,   190,  -127,   306,   339,   193,  -127,  -127,    28,
      28,  -127,  -127,  -127,  -127,   195,  -127,  -127,   133,  -127,
     196,   339,   339,  -127,  -127,  -127,  -127,  -127,   339,  -127,
     206,   339,  -127,  -127,   209,    13,   138,   174,   199,   201,
     210,   217,  -127,   222,   104,   339,   339,   339,   339,   339,
     339,   339,   339,   104,  -127,  -127,   231,    13,    13,    13,
      13,   138,   138,   174,   199,  -127,   104,  -127
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     9,    12,     0,     0,     0,     2,     4,     5,     0,
      10,     0,     6,     0,    11,     1,     3,    22,     0,    15,
       0,     0,     0,    13,    39,     0,     0,    21,     0,     8,
      39,     0,     0,     0,     7,     0,     0,    40,    41,    88,
      89,    82,     0,     0,    99,    98,    97,    87,     0,    85,
      90,    86,   104,    91,     0,   109,    64,    20,     0,     0,
      22,    16,     0,    18,     0,    14,    43,     0,     0,     0,
      83,     0,     0,     0,    33,    36,     0,    29,    65,    66,
      69,    75,    76,    77,     0,    23,     0,    94,     0,     0,
       0,     0,     0,     0,     0,    19,     0,     0,    17,    32,
       0,     0,    37,    42,   103,    95,   102,     0,   100,    84,
       0,    27,     0,    34,     0,     0,     0,     0,     0,     0,
       0,     0,    80,     0,   105,   106,   107,   108,   111,   110,
      24,    38,    30,     0,    44,     0,     0,     0,     0,     0,
      25,    50,    48,     0,    55,     0,    46,    49,    57,    58,
      56,     0,    75,    96,     0,     0,    74,    28,    35,    68,
      67,    70,    71,    72,    73,     0,    92,    31,    45,    63,
       0,     0,     0,    54,    53,    26,    47,    52,     0,   101,
       0,     0,    78,    62,     0,   112,   117,   120,   122,    81,
       0,     0,    93,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    51,    79,    59,   113,   114,   115,
     116,   118,   119,   121,   123,    61,     0,    60
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -127,  -127,  -127,   245,   -92,     5,  -127,  -127,  -127,  -127,
     219,   228,   -15,   -57,   -52,  -127,  -127,   -53,  -127,   227,
    -127,   200,  -127,   113,  -126,  -127,  -127,  -127,     9,   -60,
    -127,  -127,  -127,    89,   -43,  -127,   -42,    -2,   -41,   -40,
    -127,   116,   -79,   -30,     1,    58,    65,  -127
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     5,     6,     7,     8,    35,    10,    11,    22,    18,
      23,    19,    27,   144,    75,    98,    47,    76,    12,    36,
      37,    38,   145,   146,   147,   148,   149,   150,   106,    77,
      78,    79,    80,   184,    49,    50,    51,    52,    53,    54,
     107,   108,    55,    56,   186,   187,   188,   189
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      81,    82,    83,    84,    99,     9,    32,    95,    13,   142,
     102,     9,    14,   128,   129,     1,    81,    82,    83,    84,
     112,    81,    82,    83,    84,     4,    70,    69,    17,    25,
      81,    82,    83,    84,    48,    57,   159,   160,    25,   131,
      63,   151,    58,    58,   133,    31,    15,    92,    93,    59,
      64,    71,    87,   142,    81,    82,    83,    84,   152,    82,
      83,    84,   158,    25,    88,    89,    90,    94,   206,    91,
      26,    81,    82,    83,    84,    28,    29,   215,   117,   118,
     119,   110,   122,   120,    20,   151,   124,   125,   126,   127,
     217,     1,     2,   153,     3,   123,    33,    34,   154,   113,
     114,     4,   152,    82,    83,    84,   143,    39,    40,    41,
      21,    24,    30,    25,    60,   161,   162,   163,   164,   168,
      26,   135,   136,    66,   137,   138,   139,    67,    72,    68,
     165,   101,   101,    85,   151,   141,    44,    86,    45,    46,
      96,   185,   185,   151,   170,   195,   196,   197,   198,   109,
     143,   152,    82,    83,    84,   100,   151,   115,   116,    58,
     152,    82,    83,    84,   180,   207,   208,   209,   210,   185,
     185,   185,   185,   152,    82,    83,    84,   121,    39,    40,
      41,   157,   114,   167,   114,   199,   200,   191,     1,   130,
     193,     3,   135,   136,   134,   137,   138,   139,     4,    72,
     211,   212,   155,   101,   140,   156,   141,    44,   166,    45,
      46,    39,    40,    41,    25,   171,   172,   173,   174,   181,
     177,     1,   182,   178,     3,   135,   136,   183,   137,   138,
     139,     4,    72,   192,   194,   203,   101,   175,   201,   141,
      44,   202,    45,    46,    39,    40,    41,   104,   204,   205,
     216,    16,    65,    39,    40,    41,    61,    62,   176,   213,
       0,   190,    39,    40,    41,    42,   105,   214,   103,    43,
     179,     0,     0,    44,    72,    45,    46,     0,    73,    74,
       0,     0,    44,    72,    45,    46,     0,    73,   111,     0,
       0,    44,     0,    45,    46,    39,    40,    41,     0,     0,
       0,     0,     0,     0,    39,    40,    41,     0,     0,    39,
      40,    41,   104,     0,     0,     0,    72,     0,     0,     0,
      73,   132,     0,     0,    44,    42,    45,    46,     0,    43,
      42,     0,   169,    44,    43,    45,    46,     0,    44,     0,
      45,    46,    39,    40,    41,     0,     0,    39,    40,    41,
       0,     0,     0,     0,     0,     0,    39,    40,    41,     0,
       0,     0,     0,    42,     0,     0,     0,    43,    72,     0,
       0,    44,    73,    45,    46,     0,    44,    72,    45,    46,
       0,    97,     0,     0,     0,    44,     0,    45,    46
};

static const yytype_int16 yycheck[] =
{
      43,    43,    43,    43,    64,     0,    21,    59,     3,   101,
      67,     6,    13,    92,    93,    13,    59,    59,    59,    59,
      73,    64,    64,    64,    64,    23,    41,    24,     5,    26,
      73,    73,    73,    73,    25,    26,   115,   116,    26,    96,
      31,   101,    26,    26,    97,    33,     0,    34,    35,    33,
      33,    42,    54,   145,    97,    97,    97,    97,   101,   101,
     101,   101,   114,    26,    36,    37,    38,    58,   194,    41,
      33,   114,   114,   114,   114,    30,    31,   203,    36,    37,
      38,    72,    84,    41,     5,   145,    88,    89,    90,    91,
     216,    13,    14,    25,    16,    86,    30,    31,    30,    29,
      30,    23,   145,   145,   145,   145,   101,     3,     4,     5,
       5,    24,    24,    26,     5,   117,   118,   119,   120,   134,
      33,    17,    18,     5,    20,    21,    22,    25,    24,    30,
     121,    28,    28,    27,   194,    31,    32,    26,    34,    35,
      25,   171,   172,   203,   135,     7,     8,     9,    10,    25,
     145,   194,   194,   194,   194,    26,   216,    34,    35,    26,
     203,   203,   203,   203,   155,   195,   196,   197,   198,   199,
     200,   201,   202,   216,   216,   216,   216,    26,     3,     4,
       5,    29,    30,    29,    30,    11,    12,   178,    13,    27,
     181,    16,    17,    18,    27,    20,    21,    22,    23,    24,
     199,   200,    26,    28,    29,    25,    31,    32,    27,    34,
      35,     3,     4,     5,    26,    24,    24,    31,    31,    26,
      31,    13,    27,    33,    16,    17,    18,    31,    20,    21,
      22,    23,    24,    27,    25,    25,    28,    29,    39,    31,
      32,    40,    34,    35,     3,     4,     5,     6,    31,    27,
      19,     6,    33,     3,     4,     5,    28,    30,   145,   201,
      -1,   172,     3,     4,     5,    24,    25,   202,    68,    28,
     154,    -1,    -1,    32,    24,    34,    35,    -1,    28,    29,
      -1,    -1,    32,    24,    34,    35,    -1,    28,    29,    -1,
      -1,    32,    -1,    34,    35,     3,     4,     5,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,    -1,    -1,     3,
       4,     5,     6,    -1,    -1,    -1,    24,    -1,    -1,    -1,
      28,    29,    -1,    -1,    32,    24,    34,    35,    -1,    28,
      24,    -1,    31,    32,    28,    34,    35,    -1,    32,    -1,
      34,    35,     3,     4,     5,    -1,    -1,     3,     4,     5,
      -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,    -1,
      -1,    -1,    -1,    24,    -1,    -1,    -1,    28,    24,    -1,
      -1,    32,    28,    34,    35,    -1,    32,    24,    34,    35,
      -1,    28,    -1,    -1,    -1,    32,    -1,    34,    35
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    13,    14,    16,    23,    44,    45,    46,    47,    48,
      49,    50,    61,    48,    13,     0,    46,     5,    52,    54,
       5,     5,    51,    53,    24,    26,    33,    55,    30,    31,
      24,    33,    55,    30,    31,    48,    62,    63,    64,     3,
       4,     5,    24,    28,    32,    34,    35,    59,    71,    77,
      78,    79,    80,    81,    82,    85,    86,    71,    26,    33,
       5,    54,    62,    71,    33,    53,     5,    25,    30,    24,
      55,    71,    24,    28,    29,    57,    60,    72,    73,    74,
      75,    77,    79,    81,    82,    27,    26,    80,    36,    37,
      38,    41,    34,    35,    71,    57,    25,    28,    58,    72,
      26,    28,    56,    64,     6,    25,    71,    83,    84,    25,
      71,    29,    60,    29,    30,    34,    35,    36,    37,    38,
      41,    26,    80,    71,    80,    80,    80,    80,    85,    85,
      27,    56,    29,    60,    27,    17,    18,    20,    21,    22,
      29,    31,    47,    48,    56,    65,    66,    67,    68,    69,
      70,    72,    77,    25,    30,    26,    25,    29,    57,    85,
      85,    80,    80,    80,    80,    71,    27,    29,    55,    31,
      71,    24,    24,    31,    31,    29,    66,    31,    33,    84,
      71,    26,    27,    31,    76,    86,    87,    88,    89,    90,
      76,    71,    27,    71,    25,     7,     8,     9,    10,    11,
      12,    39,    40,    25,    31,    27,    67,    86,    86,    86,
      86,    87,    87,    88,    89,    67,    19,    67
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    43,    44,    45,    45,    46,    46,    47,    47,    48,
      48,    49,    50,    51,    51,    52,    52,    53,    53,    54,
      54,    54,    54,    55,    55,    56,    56,    57,    57,    57,
      58,    58,    58,    59,    59,    60,    60,    61,    61,    62,
      62,    63,    63,    64,    64,    64,    65,    65,    66,    66,
      67,    67,    67,    67,    67,    67,    67,    67,    67,    68,
      68,    69,    70,    70,    71,    72,    73,    73,    73,    74,
      74,    74,    74,    74,    75,    75,    75,    75,    75,    75,
      75,    76,    77,    77,    78,    78,    78,    78,    79,    79,
      80,    80,    80,    80,    80,    81,    81,    82,    82,    82,
      83,    83,    84,    84,    85,    85,    85,    85,    85,    86,
      86,    86,    87,    87,    87,    87,    87,    88,    88,    88,
      89,    89,    90,    90
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     4,     3,     1,
       1,     2,     1,     1,     3,     1,     3,     4,     3,     4,
       3,     2,     1,     3,     4,     2,     3,     2,     3,     1,
       2,     3,     1,     2,     3,     3,     1,     6,     6,     0,
       1,     1,     3,     2,     4,     5,     1,     2,     1,     1,
       1,     4,     2,     2,     2,     1,     1,     1,     1,     5,
       7,     5,     3,     2,     1,     1,     1,     3,     3,     1,
       3,     3,     3,     3,     3,     1,     1,     1,     4,     6,
       2,     1,     1,     2,     3,     1,     1,     1,     1,     1,
       1,     1,     4,     6,     2,     3,     4,     1,     1,     1,
       1,     3,     1,     1,     1,     3,     3,     3,     3,     1,
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
#line 181 "src/frontend/parser.y"
            { delete ((*yyvaluep).token); }
#line 1513 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_STRING_LITERAL: /* STRING_LITERAL  */
#line 181 "src/frontend/parser.y"
            { delete ((*yyvaluep).token); }
#line 1519 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_CompUnit: /* CompUnit  */
#line 176 "src/frontend/parser.y"
            { delete ((*yyvaluep).compUnit); }
#line 1525 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_DeclDef: /* DeclDef  */
#line 176 "src/frontend/parser.y"
            { delete ((*yyvaluep).topLevel); }
#line 1531 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Decl: /* Decl  */
#line 176 "src/frontend/parser.y"
            { delete ((*yyvaluep).decl); }
#line 1537 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BType: /* BType  */
#line 181 "src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1543 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_TensorType: /* TensorType  */
#line 181 "src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1549 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VoidType: /* VoidType  */
#line 181 "src/frontend/parser.y"
            { delete ((*yyvaluep).type_spec); }
#line 1555 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstDefList: /* ConstDefList  */
#line 176 "src/frontend/parser.y"
            { delete ((*yyvaluep).objectDefList); }
#line 1561 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VarDefList: /* VarDefList  */
#line 176 "src/frontend/parser.y"
            { delete ((*yyvaluep).objectDefList); }
#line 1567 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstDef: /* ConstDef  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).objectDef); }
#line 1573 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_VarDef: /* VarDef  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).objectDef); }
#line 1579 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Arrays: /* Arrays  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).exprList); }
#line 1585 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Block: /* Block  */
#line 178 "src/frontend/parser.y"
            { delete ((*yyvaluep).block); }
#line 1591 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_InitVal: /* InitVal  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1597 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ConstInitVal: /* ConstInitVal  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1603 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BraceInitVal: /* BraceInitVal  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).initVal); }
#line 1609 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_InitValList: /* InitValList  */
#line 177 "src/frontend/parser.y"
            { delete ((*yyvaluep).initValList); }
#line 1615 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncDef: /* FuncDef  */
#line 178 "src/frontend/parser.y"
            { delete ((*yyvaluep).funcDef); }
#line 1621 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_OptFuncFParamList: /* OptFuncFParamList  */
#line 178 "src/frontend/parser.y"
            { delete ((*yyvaluep).funcParamList); }
#line 1627 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncFParamList: /* FuncFParamList  */
#line 178 "src/frontend/parser.y"
            { delete ((*yyvaluep).funcParamList); }
#line 1633 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncFParam: /* FuncFParam  */
#line 178 "src/frontend/parser.y"
            { delete ((*yyvaluep).funcParam); }
#line 1639 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BlockItemList: /* BlockItemList  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).blockItemList); }
#line 1645 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_BlockItem: /* BlockItem  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).blockItem); }
#line 1651 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Stmt: /* Stmt  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1657 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_SelectStmt: /* SelectStmt  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1663 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_IterationStmt: /* IterationStmt  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1669 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_ReturnStmt: /* ReturnStmt  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).stmt); }
#line 1675 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Exp: /* Exp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1681 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceExp: /* NonBraceExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1687 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceAddExp: /* NonBraceAddExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1693 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceMulExp: /* NonBraceMulExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1699 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_NonBraceUnaryExp: /* NonBraceUnaryExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1705 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Cond: /* Cond  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1711 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LVal: /* LVal  */
#line 179 "src/frontend/parser.y"
            { delete ((*yyvaluep).lValue); }
#line 1717 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_PrimaryExp: /* PrimaryExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1723 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Number: /* Number  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1729 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_UnaryExp: /* UnaryExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1735 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_Call: /* Call  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).callExpr); }
#line 1741 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncCParamList: /* FuncCParamList  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).callArgList); }
#line 1747 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_FuncCParam: /* FuncCParam  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).callArg); }
#line 1753 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_MulExp: /* MulExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1759 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_AddExp: /* AddExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1765 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_RelExp: /* RelExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1771 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_EqExp: /* EqExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1777 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LAndExp: /* LAndExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1783 "src/frontend/parser.cpp"
        break;

    case YYSYMBOL_LOrExp: /* LOrExp  */
#line 180 "src/frontend/parser.y"
            { delete ((*yyvaluep).expr); }
#line 1789 "src/frontend/parser.cpp"
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
#line 190 "src/frontend/parser.y"
             {
        root = unique_ptr<CompUnitAST>((yyvsp[0].compUnit));
    }
#line 2089 "src/frontend/parser.cpp"
    break;

  case 3: /* CompUnit: CompUnit DeclDef  */
#line 196 "src/frontend/parser.y"
                     {
        (yyval.compUnit) = (yyvsp[-1].compUnit);
        (yyval.compUnit)->items.push_back(std::move(*(yyvsp[0].topLevel)));
        delete (yyvsp[0].topLevel);
    }
#line 2099 "src/frontend/parser.cpp"
    break;

  case 4: /* CompUnit: DeclDef  */
#line 201 "src/frontend/parser.y"
            {
        (yyval.compUnit) = make_node<CompUnitAST>();
        (yyval.compUnit)->items.push_back(std::move(*(yyvsp[0].topLevel)));
        delete (yyvsp[0].topLevel);
    }
#line 2109 "src/frontend/parser.cpp"
    break;

  case 5: /* DeclDef: Decl  */
#line 209 "src/frontend/parser.y"
         { (yyval.topLevel) = make_node<TopLevelItem>(unique_ptr<DeclAST>((yyvsp[0].decl))); }
#line 2115 "src/frontend/parser.cpp"
    break;

  case 6: /* DeclDef: FuncDef  */
#line 210 "src/frontend/parser.y"
            { (yyval.topLevel) = make_node<TopLevelItem>(unique_ptr<FuncDefAST>((yyvsp[0].funcDef))); }
#line 2121 "src/frontend/parser.cpp"
    break;

  case 7: /* Decl: CONST BType ConstDefList SEMICOLON  */
#line 214 "src/frontend/parser.y"
                                       {
        (yyval.decl) = make_node<DeclAST>((yyvsp[-2].type_spec)->type, (yyvsp[-2].type_spec)->tensor, true, std::move((yyvsp[-1].objectDefList)->values));
        delete (yyvsp[-2].type_spec); delete (yyvsp[-1].objectDefList);
    }
#line 2130 "src/frontend/parser.cpp"
    break;

  case 8: /* Decl: BType VarDefList SEMICOLON  */
#line 218 "src/frontend/parser.y"
                               {
        (yyval.decl) = make_node<DeclAST>((yyvsp[-2].type_spec)->type, (yyvsp[-2].type_spec)->tensor, false, std::move((yyvsp[-1].objectDefList)->values));
        delete (yyvsp[-2].type_spec); delete (yyvsp[-1].objectDefList);
    }
#line 2139 "src/frontend/parser.cpp"
    break;

  case 9: /* BType: BASICTYPE  */
#line 225 "src/frontend/parser.y"
              {
        (yyval.type_spec) = new ParsedType{static_cast<TYPE>((yyvsp[0].int_val)), false};
    }
#line 2147 "src/frontend/parser.cpp"
    break;

  case 10: /* BType: TensorType  */
#line 228 "src/frontend/parser.y"
              {
        (yyval.type_spec) = (yyvsp[0].type_spec);
    }
#line 2155 "src/frontend/parser.cpp"
    break;

  case 11: /* TensorType: TENSOR BASICTYPE  */
#line 233 "src/frontend/parser.y"
                    {
        (yyval.type_spec) = new ParsedType{static_cast<TYPE>((yyvsp[0].int_val)), true};
    }
#line 2163 "src/frontend/parser.cpp"
    break;

  case 12: /* VoidType: VOID  */
#line 239 "src/frontend/parser.y"
         {
        (yyval.type_spec) = new ParsedType{TYPE_VOID, false};
    }
#line 2171 "src/frontend/parser.cpp"
    break;

  case 13: /* ConstDefList: ConstDef  */
#line 245 "src/frontend/parser.y"
             {
        (yyval.objectDefList) = make_node<ObjectDefList>();
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2180 "src/frontend/parser.cpp"
    break;

  case 14: /* ConstDefList: ConstDefList COMMA ConstDef  */
#line 249 "src/frontend/parser.y"
                                {
        (yyval.objectDefList) = (yyvsp[-2].objectDefList);
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2189 "src/frontend/parser.cpp"
    break;

  case 15: /* VarDefList: VarDef  */
#line 255 "src/frontend/parser.y"
           {
        (yyval.objectDefList) = make_node<ObjectDefList>();
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2198 "src/frontend/parser.cpp"
    break;

  case 16: /* VarDefList: VarDefList COMMA VarDef  */
#line 259 "src/frontend/parser.y"
                            {
        (yyval.objectDefList) = (yyvsp[-2].objectDefList);
        (yyval.objectDefList)->values.push_back(unique_ptr<ObjectDefAST>((yyvsp[0].objectDef)));
    }
#line 2207 "src/frontend/parser.cpp"
    break;

  case 17: /* ConstDef: ID Arrays ASSIGN ConstInitVal  */
#line 268 "src/frontend/parser.y"
                                  {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[-3].token)), std::move((yyvsp[-2].exprList)->values),
                                     unique_ptr<InitValAST>((yyvsp[0].initVal)));
        delete (yyvsp[-3].token); delete (yyvsp[-2].exprList);
    }
#line 2217 "src/frontend/parser.cpp"
    break;

  case 18: /* ConstDef: ID ASSIGN Exp  */
#line 273 "src/frontend/parser.y"
                  {
        (yyval.objectDef) = make_node<ObjectDefAST>(
            std::move(*(yyvsp[-2].token)), std::vector<unique_ptr<ExprAST>>{},
            unique_ptr<InitValAST>(make_node<InitValAST>(
                unique_ptr<ExprAST>((yyvsp[0].expr)))));
        delete (yyvsp[-2].token);
    }
#line 2229 "src/frontend/parser.cpp"
    break;

  case 19: /* VarDef: ID Arrays ASSIGN InitVal  */
#line 282 "src/frontend/parser.y"
                             {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[-3].token)), std::move((yyvsp[-2].exprList)->values),
                                     unique_ptr<InitValAST>((yyvsp[0].initVal)));
        delete (yyvsp[-3].token); delete (yyvsp[-2].exprList);
    }
#line 2239 "src/frontend/parser.cpp"
    break;

  case 20: /* VarDef: ID ASSIGN Exp  */
#line 287 "src/frontend/parser.y"
                  {
        (yyval.objectDef) = make_node<ObjectDefAST>(
            std::move(*(yyvsp[-2].token)), std::vector<unique_ptr<ExprAST>>{},
            unique_ptr<InitValAST>(make_node<InitValAST>(
                unique_ptr<ExprAST>((yyvsp[0].expr)))));
        delete (yyvsp[-2].token);
    }
#line 2251 "src/frontend/parser.cpp"
    break;

  case 21: /* VarDef: ID Arrays  */
#line 294 "src/frontend/parser.y"
              {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[-1].token)), std::move((yyvsp[0].exprList)->values));
        delete (yyvsp[-1].token); delete (yyvsp[0].exprList);
    }
#line 2260 "src/frontend/parser.cpp"
    break;

  case 22: /* VarDef: ID  */
#line 298 "src/frontend/parser.y"
       {
        (yyval.objectDef) = make_node<ObjectDefAST>(std::move(*(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 2269 "src/frontend/parser.cpp"
    break;

  case 23: /* Arrays: LB Exp RB  */
#line 305 "src/frontend/parser.y"
              {
        (yyval.exprList) = make_node<ExprList>();
        (yyval.exprList)->values.push_back(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2278 "src/frontend/parser.cpp"
    break;

  case 24: /* Arrays: Arrays LB Exp RB  */
#line 309 "src/frontend/parser.y"
                     {
        (yyval.exprList) = (yyvsp[-3].exprList);
        (yyval.exprList)->values.push_back(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2287 "src/frontend/parser.cpp"
    break;

  case 25: /* Block: LC RC  */
#line 320 "src/frontend/parser.y"
          {
        (yyval.block) = make_node<BlockAST>();
    }
#line 2295 "src/frontend/parser.cpp"
    break;

  case 26: /* Block: LC BlockItemList RC  */
#line 323 "src/frontend/parser.y"
                        {
        (yyval.block) = make_node<BlockAST>();
        (yyval.block)->items.swap(*(yyvsp[-1].blockItemList));
        delete (yyvsp[-1].blockItemList);
    }
#line 2305 "src/frontend/parser.cpp"
    break;

  case 27: /* InitVal: LC RC  */
#line 334 "src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2313 "src/frontend/parser.cpp"
    break;

  case 28: /* InitVal: LC InitValList RC  */
#line 337 "src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>(std::move((yyvsp[-1].initValList)->values));
        delete (yyvsp[-1].initValList);
    }
#line 2322 "src/frontend/parser.cpp"
    break;

  case 29: /* InitVal: NonBraceExp  */
#line 341 "src/frontend/parser.y"
                {
        (yyval.initVal) = make_node<InitValAST>(unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2330 "src/frontend/parser.cpp"
    break;

  case 30: /* ConstInitVal: LC RC  */
#line 346 "src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2338 "src/frontend/parser.cpp"
    break;

  case 31: /* ConstInitVal: LC InitValList RC  */
#line 349 "src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>(std::move((yyvsp[-1].initValList)->values));
        delete (yyvsp[-1].initValList);
    }
#line 2347 "src/frontend/parser.cpp"
    break;

  case 32: /* ConstInitVal: NonBraceExp  */
#line 353 "src/frontend/parser.y"
                {
        (yyval.initVal) = make_node<InitValAST>(unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2355 "src/frontend/parser.cpp"
    break;

  case 33: /* BraceInitVal: LC RC  */
#line 359 "src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2363 "src/frontend/parser.cpp"
    break;

  case 34: /* BraceInitVal: LC InitValList RC  */
#line 362 "src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>(std::move((yyvsp[-1].initValList)->values));
        delete (yyvsp[-1].initValList);
    }
#line 2372 "src/frontend/parser.cpp"
    break;

  case 35: /* InitValList: InitValList COMMA InitVal  */
#line 369 "src/frontend/parser.y"
                            {
    (yyval.initValList) = (yyvsp[-2].initValList);
    (yyval.initValList)->values.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2381 "src/frontend/parser.cpp"
    break;

  case 36: /* InitValList: InitVal  */
#line 373 "src/frontend/parser.y"
          {
    (yyval.initValList) = make_node<InitValList>();
    (yyval.initValList)->values.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2390 "src/frontend/parser.cpp"
    break;

  case 37: /* FuncDef: BType ID LP OptFuncFParamList RP Block  */
#line 380 "src/frontend/parser.y"
                                           {
        (yyval.funcDef) = make_node<FuncDefAST>((yyvsp[-5].type_spec)->type, (yyvsp[-5].type_spec)->tensor, std::move(*(yyvsp[-4].token)),
            std::move((yyvsp[-2].funcParamList)->values), unique_ptr<BlockAST>((yyvsp[0].block)));
        delete (yyvsp[-5].type_spec); delete (yyvsp[-4].token); delete (yyvsp[-2].funcParamList);
    }
#line 2400 "src/frontend/parser.cpp"
    break;

  case 38: /* FuncDef: VoidType ID LP OptFuncFParamList RP Block  */
#line 385 "src/frontend/parser.y"
                                              {
        (yyval.funcDef) = make_node<FuncDefAST>((yyvsp[-5].type_spec)->type, (yyvsp[-5].type_spec)->tensor, std::move(*(yyvsp[-4].token)),
            std::move((yyvsp[-2].funcParamList)->values), unique_ptr<BlockAST>((yyvsp[0].block)));
        delete (yyvsp[-5].type_spec); delete (yyvsp[-4].token); delete (yyvsp[-2].funcParamList);
    }
#line 2410 "src/frontend/parser.cpp"
    break;

  case 39: /* OptFuncFParamList: %empty  */
#line 392 "src/frontend/parser.y"
           {
        (yyval.funcParamList) = make_node<FuncParamList>();
    }
#line 2418 "src/frontend/parser.cpp"
    break;

  case 40: /* OptFuncFParamList: FuncFParamList  */
#line 395 "src/frontend/parser.y"
                   {
        (yyval.funcParamList) = (yyvsp[0].funcParamList);
    }
#line 2426 "src/frontend/parser.cpp"
    break;

  case 41: /* FuncFParamList: FuncFParam  */
#line 401 "src/frontend/parser.y"
               {
        (yyval.funcParamList) = make_node<FuncParamList>();
        (yyval.funcParamList)->values.push_back(unique_ptr<FuncParamAST>((yyvsp[0].funcParam)));
    }
#line 2435 "src/frontend/parser.cpp"
    break;

  case 42: /* FuncFParamList: FuncFParamList COMMA FuncFParam  */
#line 405 "src/frontend/parser.y"
                                    {
        (yyval.funcParamList) = (yyvsp[-2].funcParamList);
        (yyval.funcParamList)->values.push_back(unique_ptr<FuncParamAST>((yyvsp[0].funcParam)));
    }
#line 2444 "src/frontend/parser.cpp"
    break;

  case 43: /* FuncFParam: BType ID  */
#line 412 "src/frontend/parser.y"
             {
        (yyval.funcParam) = make_node<FuncParamAST>((yyvsp[-1].type_spec)->type, (yyvsp[-1].type_spec)->tensor, std::move(*(yyvsp[0].token)));
        delete (yyvsp[-1].type_spec); delete (yyvsp[0].token);
    }
#line 2453 "src/frontend/parser.cpp"
    break;

  case 44: /* FuncFParam: BType ID LB RB  */
#line 416 "src/frontend/parser.y"
                   {
        (yyval.funcParam) = make_node<FuncParamAST>((yyvsp[-3].type_spec)->type, (yyvsp[-3].type_spec)->tensor, std::move(*(yyvsp[-2].token)), true);
        delete (yyvsp[-3].type_spec); delete (yyvsp[-2].token);
    }
#line 2462 "src/frontend/parser.cpp"
    break;

  case 45: /* FuncFParam: BType ID LB RB Arrays  */
#line 420 "src/frontend/parser.y"
                          {
        (yyval.funcParam) = make_node<FuncParamAST>((yyvsp[-4].type_spec)->type, (yyvsp[-4].type_spec)->tensor, std::move(*(yyvsp[-3].token)), true,
                                     std::move((yyvsp[0].exprList)->values));
        delete (yyvsp[-4].type_spec); delete (yyvsp[-3].token); delete (yyvsp[0].exprList);
    }
#line 2472 "src/frontend/parser.cpp"
    break;

  case 46: /* BlockItemList: BlockItem  */
#line 428 "src/frontend/parser.y"
              {
        (yyval.blockItemList) = make_node<BlockItemList>();
        (yyval.blockItemList)->push_back(std::move(*(yyvsp[0].blockItem)));
        delete (yyvsp[0].blockItem);
    }
#line 2482 "src/frontend/parser.cpp"
    break;

  case 47: /* BlockItemList: BlockItemList BlockItem  */
#line 433 "src/frontend/parser.y"
                            {
        (yyval.blockItemList) = (yyvsp[-1].blockItemList);
        (yyval.blockItemList)->push_back(std::move(*(yyvsp[0].blockItem)));
        delete (yyvsp[0].blockItem);
    }
#line 2492 "src/frontend/parser.cpp"
    break;

  case 48: /* BlockItem: Decl  */
#line 441 "src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>(unique_ptr<DeclAST>((yyvsp[0].decl)));
    }
#line 2500 "src/frontend/parser.cpp"
    break;

  case 49: /* BlockItem: Stmt  */
#line 444 "src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>(unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2508 "src/frontend/parser.cpp"
    break;

  case 50: /* Stmt: SEMICOLON  */
#line 451 "src/frontend/parser.y"
              {
        (yyval.stmt) = make_node<EmptyStmtAST>();
    }
#line 2516 "src/frontend/parser.cpp"
    break;

  case 51: /* Stmt: LVal ASSIGN Exp SEMICOLON  */
#line 454 "src/frontend/parser.y"
                              {
        (yyval.stmt) = make_node<AssignStmtAST>(unique_ptr<LValueAST>((yyvsp[-3].lValue)),
                                      unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2525 "src/frontend/parser.cpp"
    break;

  case 52: /* Stmt: NonBraceExp SEMICOLON  */
#line 458 "src/frontend/parser.y"
                          {
        (yyval.stmt) = make_node<ExprStmtAST>(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2533 "src/frontend/parser.cpp"
    break;

  case 53: /* Stmt: CONTINUE SEMICOLON  */
#line 461 "src/frontend/parser.y"
                       {
        (yyval.stmt) = make_node<ContinueStmtAST>();
    }
#line 2541 "src/frontend/parser.cpp"
    break;

  case 54: /* Stmt: BREAK SEMICOLON  */
#line 464 "src/frontend/parser.y"
                    {
        (yyval.stmt) = make_node<BreakStmtAST>();
    }
#line 2549 "src/frontend/parser.cpp"
    break;

  case 55: /* Stmt: Block  */
#line 467 "src/frontend/parser.y"
          {
        (yyval.stmt) = make_node<BlockStmtAST>(unique_ptr<BlockAST>((yyvsp[0].block)));
    }
#line 2557 "src/frontend/parser.cpp"
    break;

  case 56: /* Stmt: ReturnStmt  */
#line 470 "src/frontend/parser.y"
               { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2563 "src/frontend/parser.cpp"
    break;

  case 57: /* Stmt: SelectStmt  */
#line 471 "src/frontend/parser.y"
               { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2569 "src/frontend/parser.cpp"
    break;

  case 58: /* Stmt: IterationStmt  */
#line 472 "src/frontend/parser.y"
                  { (yyval.stmt) = (yyvsp[0].stmt); }
#line 2575 "src/frontend/parser.cpp"
    break;

  case 59: /* SelectStmt: IF LP Cond RP Stmt  */
#line 476 "src/frontend/parser.y"
                                             {
        (yyval.stmt) = make_node<IfStmtAST>(unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                  unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2584 "src/frontend/parser.cpp"
    break;

  case 60: /* SelectStmt: IF LP Cond RP Stmt ELSE Stmt  */
#line 480 "src/frontend/parser.y"
                                 {
        (yyval.stmt) = make_node<IfStmtAST>(unique_ptr<ExprAST>((yyvsp[-4].expr)),
                                  unique_ptr<StmtAST>((yyvsp[-2].stmt)),
                                  unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2594 "src/frontend/parser.cpp"
    break;

  case 61: /* IterationStmt: WHILE LP Cond RP Stmt  */
#line 488 "src/frontend/parser.y"
                          {
        (yyval.stmt) = make_node<WhileStmtAST>(unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                     unique_ptr<StmtAST>((yyvsp[0].stmt)));
    }
#line 2603 "src/frontend/parser.cpp"
    break;

  case 62: /* ReturnStmt: RETURN Exp SEMICOLON  */
#line 495 "src/frontend/parser.y"
                         {
        (yyval.stmt) = make_node<ReturnStmtAST>(unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2611 "src/frontend/parser.cpp"
    break;

  case 63: /* ReturnStmt: RETURN SEMICOLON  */
#line 498 "src/frontend/parser.y"
                     {
        (yyval.stmt) = make_node<ReturnStmtAST>();
    }
#line 2619 "src/frontend/parser.cpp"
    break;

  case 64: /* Exp: AddExp  */
#line 504 "src/frontend/parser.y"
           {
        (yyval.expr) = (yyvsp[0].expr);
    }
#line 2627 "src/frontend/parser.cpp"
    break;

  case 65: /* NonBraceExp: NonBraceAddExp  */
#line 513 "src/frontend/parser.y"
                   { (yyval.expr) = (yyvsp[0].expr); }
#line 2633 "src/frontend/parser.cpp"
    break;

  case 66: /* NonBraceAddExp: NonBraceMulExp  */
#line 516 "src/frontend/parser.y"
                   { (yyval.expr) = (yyvsp[0].expr); }
#line 2639 "src/frontend/parser.cpp"
    break;

  case 67: /* NonBraceAddExp: NonBraceAddExp ADD MulExp  */
#line 517 "src/frontend/parser.y"
                              {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Add,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2649 "src/frontend/parser.cpp"
    break;

  case 68: /* NonBraceAddExp: NonBraceAddExp MINUS MulExp  */
#line 522 "src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Subtract,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2659 "src/frontend/parser.cpp"
    break;

  case 69: /* NonBraceMulExp: NonBraceUnaryExp  */
#line 529 "src/frontend/parser.y"
                     { (yyval.expr) = (yyvsp[0].expr); }
#line 2665 "src/frontend/parser.cpp"
    break;

  case 70: /* NonBraceMulExp: NonBraceMulExp MUL UnaryExp  */
#line 530 "src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Multiply,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2675 "src/frontend/parser.cpp"
    break;

  case 71: /* NonBraceMulExp: NonBraceMulExp DIV UnaryExp  */
#line 535 "src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Divide,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2685 "src/frontend/parser.cpp"
    break;

  case 72: /* NonBraceMulExp: NonBraceMulExp MOD UnaryExp  */
#line 540 "src/frontend/parser.y"
                                {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Remainder,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2695 "src/frontend/parser.cpp"
    break;

  case 73: /* NonBraceMulExp: NonBraceMulExp MATMUL UnaryExp  */
#line 545 "src/frontend/parser.y"
                                   {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Matmul,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2705 "src/frontend/parser.cpp"
    break;

  case 74: /* NonBraceUnaryExp: LP Exp RP  */
#line 552 "src/frontend/parser.y"
              { (yyval.expr) = (yyvsp[-1].expr); }
#line 2711 "src/frontend/parser.cpp"
    break;

  case 75: /* NonBraceUnaryExp: LVal  */
#line 553 "src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].lValue); }
#line 2717 "src/frontend/parser.cpp"
    break;

  case 76: /* NonBraceUnaryExp: Number  */
#line 554 "src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 2723 "src/frontend/parser.cpp"
    break;

  case 77: /* NonBraceUnaryExp: Call  */
#line 555 "src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].callExpr); }
#line 2729 "src/frontend/parser.cpp"
    break;

  case 78: /* NonBraceUnaryExp: Call LB Exp RB  */
#line 556 "src/frontend/parser.y"
                   {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-3].callExpr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2738 "src/frontend/parser.cpp"
    break;

  case 79: /* NonBraceUnaryExp: LP Exp RP LB Exp RB  */
#line 560 "src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-4].expr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2747 "src/frontend/parser.cpp"
    break;

  case 80: /* NonBraceUnaryExp: UnaryOp UnaryExp  */
#line 564 "src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<UnaryExprAST>((yyvsp[-1].unaryOp), unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2755 "src/frontend/parser.cpp"
    break;

  case 81: /* Cond: LOrExp  */
#line 570 "src/frontend/parser.y"
           {
        (yyval.expr) = (yyvsp[0].expr);
    }
#line 2763 "src/frontend/parser.cpp"
    break;

  case 82: /* LVal: ID  */
#line 576 "src/frontend/parser.y"
       {
        (yyval.lValue) = make_node<LValueAST>(std::move(*(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 2772 "src/frontend/parser.cpp"
    break;

  case 83: /* LVal: ID Arrays  */
#line 580 "src/frontend/parser.y"
              {
        (yyval.lValue) = make_node<LValueAST>(std::move(*(yyvsp[-1].token)));
        delete (yyvsp[-1].token);
        (yyval.lValue)->indices.swap((yyvsp[0].exprList)->values);
        delete (yyvsp[0].exprList);
    }
#line 2783 "src/frontend/parser.cpp"
    break;

  case 84: /* PrimaryExp: LP Exp RP  */
#line 589 "src/frontend/parser.y"
              { (yyval.expr) = (yyvsp[-1].expr); }
#line 2789 "src/frontend/parser.cpp"
    break;

  case 85: /* PrimaryExp: LVal  */
#line 590 "src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].lValue); }
#line 2795 "src/frontend/parser.cpp"
    break;

  case 86: /* PrimaryExp: Number  */
#line 591 "src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 2801 "src/frontend/parser.cpp"
    break;

  case 87: /* PrimaryExp: BraceInitVal  */
#line 592 "src/frontend/parser.y"
                 {
        (yyval.expr) = make_node<AggregateExprAST>(unique_ptr<InitValAST>((yyvsp[0].initVal)));
    }
#line 2809 "src/frontend/parser.cpp"
    break;

  case 88: /* Number: INT  */
#line 598 "src/frontend/parser.y"
        {
        (yyval.expr) = make_node<LiteralExprAST>((yyvsp[0].int_val));
    }
#line 2817 "src/frontend/parser.cpp"
    break;

  case 89: /* Number: FLOAT  */
#line 601 "src/frontend/parser.y"
          {
        (yyval.expr) = make_node<LiteralExprAST>((yyvsp[0].float_val));
    }
#line 2825 "src/frontend/parser.cpp"
    break;

  case 90: /* UnaryExp: PrimaryExp  */
#line 609 "src/frontend/parser.y"
               { (yyval.expr) = (yyvsp[0].expr); }
#line 2831 "src/frontend/parser.cpp"
    break;

  case 91: /* UnaryExp: Call  */
#line 610 "src/frontend/parser.y"
         { (yyval.expr) = (yyvsp[0].callExpr); }
#line 2837 "src/frontend/parser.cpp"
    break;

  case 92: /* UnaryExp: Call LB Exp RB  */
#line 611 "src/frontend/parser.y"
                   {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-3].callExpr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2846 "src/frontend/parser.cpp"
    break;

  case 93: /* UnaryExp: LP Exp RP LB Exp RB  */
#line 615 "src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<SubscriptExprAST>(unique_ptr<ExprAST>((yyvsp[-4].expr)),
                                         unique_ptr<ExprAST>((yyvsp[-1].expr)));
    }
#line 2855 "src/frontend/parser.cpp"
    break;

  case 94: /* UnaryExp: UnaryOp UnaryExp  */
#line 619 "src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<UnaryExprAST>((yyvsp[-1].unaryOp), unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2863 "src/frontend/parser.cpp"
    break;

  case 95: /* Call: ID LP RP  */
#line 625 "src/frontend/parser.y"
             {
        (yyval.callExpr) = make_node<CallExprAST>(std::move(*(yyvsp[-2].token)), (yylsp[-2]).first_line);
        delete (yyvsp[-2].token);
    }
#line 2872 "src/frontend/parser.cpp"
    break;

  case 96: /* Call: ID LP FuncCParamList RP  */
#line 629 "src/frontend/parser.y"
                            {
        (yyval.callExpr) = make_node<CallExprAST>(std::move(*(yyvsp[-3].token)), (yylsp[-3]).first_line);
        delete (yyvsp[-3].token);
        (yyval.callExpr)->arguments.swap(*(yyvsp[-1].callArgList));
        delete (yyvsp[-1].callArgList);
    }
#line 2883 "src/frontend/parser.cpp"
    break;

  case 97: /* UnaryOp: ADD  */
#line 638 "src/frontend/parser.y"
        {
        (yyval.unaryOp) = UnaryOp::Plus;
    }
#line 2891 "src/frontend/parser.cpp"
    break;

  case 98: /* UnaryOp: MINUS  */
#line 641 "src/frontend/parser.y"
          {
        (yyval.unaryOp) = UnaryOp::Minus;
    }
#line 2899 "src/frontend/parser.cpp"
    break;

  case 99: /* UnaryOp: NOT  */
#line 644 "src/frontend/parser.y"
        {
        (yyval.unaryOp) = UnaryOp::LogicalNot;
    }
#line 2907 "src/frontend/parser.cpp"
    break;

  case 100: /* FuncCParamList: FuncCParam  */
#line 650 "src/frontend/parser.y"
               {
        (yyval.callArgList) = make_node<CallArgList>();
        (yyval.callArgList)->push_back(std::move(*(yyvsp[0].callArg)));
        delete (yyvsp[0].callArg);
    }
#line 2917 "src/frontend/parser.cpp"
    break;

  case 101: /* FuncCParamList: FuncCParamList COMMA FuncCParam  */
#line 655 "src/frontend/parser.y"
                                    {
        (yyval.callArgList) = (yyvsp[-2].callArgList);
        (yyval.callArgList)->push_back(std::move(*(yyvsp[0].callArg)));
        delete (yyvsp[0].callArg);
    }
#line 2927 "src/frontend/parser.cpp"
    break;

  case 102: /* FuncCParam: Exp  */
#line 662 "src/frontend/parser.y"
        {
        (yyval.callArg) = make_node<CallArgumentAST>(unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2935 "src/frontend/parser.cpp"
    break;

  case 103: /* FuncCParam: STRING_LITERAL  */
#line 665 "src/frontend/parser.y"
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
#line 2950 "src/frontend/parser.cpp"
    break;

  case 104: /* MulExp: UnaryExp  */
#line 678 "src/frontend/parser.y"
             { (yyval.expr) = (yyvsp[0].expr); }
#line 2956 "src/frontend/parser.cpp"
    break;

  case 105: /* MulExp: MulExp MUL UnaryExp  */
#line 679 "src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Multiply,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2966 "src/frontend/parser.cpp"
    break;

  case 106: /* MulExp: MulExp DIV UnaryExp  */
#line 684 "src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Divide,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2976 "src/frontend/parser.cpp"
    break;

  case 107: /* MulExp: MulExp MOD UnaryExp  */
#line 689 "src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Remainder,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2986 "src/frontend/parser.cpp"
    break;

  case 108: /* MulExp: MulExp MATMUL UnaryExp  */
#line 694 "src/frontend/parser.y"
                          {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Matmul,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 2996 "src/frontend/parser.cpp"
    break;

  case 109: /* AddExp: MulExp  */
#line 702 "src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3002 "src/frontend/parser.cpp"
    break;

  case 110: /* AddExp: AddExp ADD MulExp  */
#line 703 "src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Add,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3012 "src/frontend/parser.cpp"
    break;

  case 111: /* AddExp: AddExp MINUS MulExp  */
#line 708 "src/frontend/parser.y"
                        {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Subtract,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3022 "src/frontend/parser.cpp"
    break;

  case 112: /* RelExp: AddExp  */
#line 716 "src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3028 "src/frontend/parser.cpp"
    break;

  case 113: /* RelExp: RelExp GTE AddExp  */
#line 717 "src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::GreaterEqual,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3038 "src/frontend/parser.cpp"
    break;

  case 114: /* RelExp: RelExp LTE AddExp  */
#line 722 "src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::LessEqual,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3048 "src/frontend/parser.cpp"
    break;

  case 115: /* RelExp: RelExp GT AddExp  */
#line 727 "src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Greater,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3058 "src/frontend/parser.cpp"
    break;

  case 116: /* RelExp: RelExp LT AddExp  */
#line 732 "src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Less,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3068 "src/frontend/parser.cpp"
    break;

  case 117: /* EqExp: RelExp  */
#line 740 "src/frontend/parser.y"
           { (yyval.expr) = (yyvsp[0].expr); }
#line 3074 "src/frontend/parser.cpp"
    break;

  case 118: /* EqExp: EqExp EQ RelExp  */
#line 741 "src/frontend/parser.y"
                    {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::Equal,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3084 "src/frontend/parser.cpp"
    break;

  case 119: /* EqExp: EqExp NEQ RelExp  */
#line 746 "src/frontend/parser.y"
                     {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::NotEqual,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3094 "src/frontend/parser.cpp"
    break;

  case 120: /* LAndExp: EqExp  */
#line 754 "src/frontend/parser.y"
          { (yyval.expr) = (yyvsp[0].expr); }
#line 3100 "src/frontend/parser.cpp"
    break;

  case 121: /* LAndExp: LAndExp AND EqExp  */
#line 755 "src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::LogicalAnd,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3110 "src/frontend/parser.cpp"
    break;

  case 122: /* LOrExp: LAndExp  */
#line 763 "src/frontend/parser.y"
            { (yyval.expr) = (yyvsp[0].expr); }
#line 3116 "src/frontend/parser.cpp"
    break;

  case 123: /* LOrExp: LOrExp OR LAndExp  */
#line 764 "src/frontend/parser.y"
                      {
        (yyval.expr) = make_node<BinaryExprAST>(BinaryOp::LogicalOr,
                                      unique_ptr<ExprAST>((yyvsp[-2].expr)),
                                      unique_ptr<ExprAST>((yyvsp[0].expr)));
    }
#line 3126 "src/frontend/parser.cpp"
    break;


#line 3130 "src/frontend/parser.cpp"

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

#line 769 "src/frontend/parser.y"


void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
