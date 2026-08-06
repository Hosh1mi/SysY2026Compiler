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

#line 96 "/workspace/src/frontend/parser.cpp"

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
  YYSYMBOL_GTE = 6,                        /* GTE  */
  YYSYMBOL_LTE = 7,                        /* LTE  */
  YYSYMBOL_GT = 8,                         /* GT  */
  YYSYMBOL_LT = 9,                         /* LT  */
  YYSYMBOL_EQ = 10,                        /* EQ  */
  YYSYMBOL_NEQ = 11,                       /* NEQ  */
  YYSYMBOL_INTTYPE = 12,                   /* INTTYPE  */
  YYSYMBOL_FLOATTYPE = 13,                 /* FLOATTYPE  */
  YYSYMBOL_VOID = 14,                      /* VOID  */
  YYSYMBOL_INTVECTYPE = 15,                /* INTVECTYPE  */
  YYSYMBOL_FLOATVECTYPE = 16,              /* FLOATVECTYPE  */
  YYSYMBOL_VECWIDTH = 17,                  /* VECWIDTH  */
  YYSYMBOL_VECTOR = 18,                    /* VECTOR  */
  YYSYMBOL_DYNINTVECTYPE = 19,             /* DYNINTVECTYPE  */
  YYSYMBOL_DYNFLOATVECTYPE = 20,           /* DYNFLOATVECTYPE  */
  YYSYMBOL_CONST = 21,                     /* CONST  */
  YYSYMBOL_RETURN = 22,                    /* RETURN  */
  YYSYMBOL_IF = 23,                        /* IF  */
  YYSYMBOL_ELSE = 24,                      /* ELSE  */
  YYSYMBOL_WHILE = 25,                     /* WHILE  */
  YYSYMBOL_BREAK = 26,                     /* BREAK  */
  YYSYMBOL_CONTINUE = 27,                  /* CONTINUE  */
  YYSYMBOL_LP = 28,                        /* LP  */
  YYSYMBOL_RP = 29,                        /* RP  */
  YYSYMBOL_LB = 30,                        /* LB  */
  YYSYMBOL_RB = 31,                        /* RB  */
  YYSYMBOL_LC = 32,                        /* LC  */
  YYSYMBOL_RC = 33,                        /* RC  */
  YYSYMBOL_COMMA = 34,                     /* COMMA  */
  YYSYMBOL_SEMICOLON = 35,                 /* SEMICOLON  */
  YYSYMBOL_NOT = 36,                       /* NOT  */
  YYSYMBOL_ASSIGN = 37,                    /* ASSIGN  */
  YYSYMBOL_MINUS = 38,                     /* MINUS  */
  YYSYMBOL_ADD = 39,                       /* ADD  */
  YYSYMBOL_MUL = 40,                       /* MUL  */
  YYSYMBOL_DIV = 41,                       /* DIV  */
  YYSYMBOL_MOD = 42,                       /* MOD  */
  YYSYMBOL_AND = 43,                       /* AND  */
  YYSYMBOL_OR = 44,                        /* OR  */
  YYSYMBOL_LOWER_THEN_ELSE = 45,           /* LOWER_THEN_ELSE  */
  YYSYMBOL_YYACCEPT = 46,                  /* $accept  */
  YYSYMBOL_Program = 47,                   /* Program  */
  YYSYMBOL_CompUnit = 48,                  /* CompUnit  */
  YYSYMBOL_DeclDef = 49,                   /* DeclDef  */
  YYSYMBOL_Decl = 50,                      /* Decl  */
  YYSYMBOL_BType = 51,                     /* BType  */
  YYSYMBOL_VecType = 52,                   /* VecType  */
  YYSYMBOL_VoidType = 53,                  /* VoidType  */
  YYSYMBOL_DefList = 54,                   /* DefList  */
  YYSYMBOL_Def = 55,                       /* Def  */
  YYSYMBOL_Arrays = 56,                    /* Arrays  */
  YYSYMBOL_InitVal = 57,                   /* InitVal  */
  YYSYMBOL_InitValList = 58,               /* InitValList  */
  YYSYMBOL_FuncDef = 59,                   /* FuncDef  */
  YYSYMBOL_FuncFParamList = 60,            /* FuncFParamList  */
  YYSYMBOL_FuncFParam = 61,                /* FuncFParam  */
  YYSYMBOL_Block = 62,                     /* Block  */
  YYSYMBOL_BlockItemList = 63,             /* BlockItemList  */
  YYSYMBOL_BlockItem = 64,                 /* BlockItem  */
  YYSYMBOL_Stmt = 65,                      /* Stmt  */
  YYSYMBOL_SelectStmt = 66,                /* SelectStmt  */
  YYSYMBOL_IterationStmt = 67,             /* IterationStmt  */
  YYSYMBOL_ReturnStmt = 68,                /* ReturnStmt  */
  YYSYMBOL_Exp = 69,                       /* Exp  */
  YYSYMBOL_Cond = 70,                      /* Cond  */
  YYSYMBOL_LVal = 71,                      /* LVal  */
  YYSYMBOL_PrimaryExp = 72,                /* PrimaryExp  */
  YYSYMBOL_Number = 73,                    /* Number  */
  YYSYMBOL_UnaryExp = 74,                  /* UnaryExp  */
  YYSYMBOL_Call = 75,                      /* Call  */
  YYSYMBOL_UnaryOp = 76,                   /* UnaryOp  */
  YYSYMBOL_FuncCParamList = 77,            /* FuncCParamList  */
  YYSYMBOL_MulExp = 78,                    /* MulExp  */
  YYSYMBOL_AddExp = 79,                    /* AddExp  */
  YYSYMBOL_RelExp = 80,                    /* RelExp  */
  YYSYMBOL_EqExp = 81,                     /* EqExp  */
  YYSYMBOL_LAndExp = 82,                   /* LAndExp  */
  YYSYMBOL_LOrExp = 83                     /* LOrExp  */
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
#define YYFINAL  28
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   316

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  46
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  38
/* YYNRULES -- Number of rules.  */
#define YYNRULES  118
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  226

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   300


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
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   139,   139,   145,   149,   156,   160,   167,   174,   184,
     187,   190,   198,   201,   204,   207,   210,   213,   216,   219,
     222,   225,   228,   231,   234,   237,   240,   243,   246,   249,
     252,   255,   258,   261,   264,   267,   270,   273,   279,   285,
     289,   296,   302,   307,   312,   319,   323,   331,   335,   338,
     345,   349,   356,   364,   371,   379,   389,   393,   400,   407,
     414,   425,   428,   435,   439,   446,   450,   457,   461,   467,
     472,   476,   480,   485,   490,   495,   503,   508,   517,   525,
     529,   535,   541,   547,   551,   559,   563,   567,   574,   579,
     587,   591,   595,   603,   608,   617,   620,   623,   629,   633,
     640,   644,   650,   656,   665,   669,   675,   684,   688,   694,
     700,   706,   715,   719,   725,   734,   738,   746,   750
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
  "GTE", "LTE", "GT", "LT", "EQ", "NEQ", "INTTYPE", "FLOATTYPE", "VOID",
  "INTVECTYPE", "FLOATVECTYPE", "VECWIDTH", "VECTOR", "DYNINTVECTYPE",
  "DYNFLOATVECTYPE", "CONST", "RETURN", "IF", "ELSE", "WHILE", "BREAK",
  "CONTINUE", "LP", "RP", "LB", "RB", "LC", "RC", "COMMA", "SEMICOLON",
  "NOT", "ASSIGN", "MINUS", "ADD", "MUL", "DIV", "MOD", "AND", "OR",
  "LOWER_THEN_ELSE", "$accept", "Program", "CompUnit", "DeclDef", "Decl",
  "BType", "VecType", "VoidType", "DefList", "Def", "Arrays", "InitVal",
  "InitValList", "FuncDef", "FuncFParamList", "FuncFParam", "Block",
  "BlockItemList", "BlockItem", "Stmt", "SelectStmt", "IterationStmt",
  "ReturnStmt", "Exp", "Cond", "LVal", "PrimaryExp", "Number", "UnaryExp",
  "Call", "UnaryOp", "FuncCParamList", "MulExp", "AddExp", "RelExp",
  "EqExp", "LAndExp", "LOrExp", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-178)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     259,     9,    14,  -178,  -178,  -178,    19,    -1,  -178,  -178,
     269,    63,   259,  -178,  -178,    85,  -178,    95,  -178,    59,
       3,    75,    10,    18,    12,   107,   112,   100,  -178,  -178,
      65,   118,  -178,    42,   104,  -178,    99,  -178,   125,  -178,
     116,  -178,   149,   152,   133,     6,     8,   -12,    50,   140,
     150,    20,   178,   226,    93,    49,    43,   100,  -178,   241,
    -178,  -178,  -178,  -178,  -178,  -178,   204,  -178,   194,  -178,
     197,  -178,   200,  -178,   220,   237,   246,  -178,   218,   247,
      60,  -178,  -178,  -178,   138,    93,  -178,  -178,  -178,   231,
    -178,  -178,  -178,  -178,  -178,    93,   -21,   180,    71,  -178,
    -178,    93,    49,  -178,   218,    77,   243,   255,   256,   257,
     238,   239,   235,   252,   123,  -178,   260,   218,   269,   186,
     261,   240,  -178,  -178,    93,    93,    93,    93,    93,  -178,
    -178,   187,   262,  -178,  -178,   218,  -178,  -178,  -178,  -178,
    -178,  -178,  -178,  -178,   166,   264,   266,   263,   265,  -178,
    -178,  -178,   100,  -178,   160,  -178,  -178,  -178,  -178,  -178,
     267,   258,   268,  -178,  -178,  -178,  -178,    89,  -178,  -178,
    -178,  -178,   -21,   -21,  -178,    49,  -178,  -178,  -178,   270,
      93,    93,  -178,  -178,  -178,  -178,  -178,    93,   271,  -178,
      93,  -178,  -178,   274,   180,   202,   216,   253,   272,   275,
     273,   261,  -178,    33,    93,    93,    93,    93,    93,    93,
      93,    93,    33,  -178,   282,   180,   180,   180,   180,   202,
     202,   216,   253,  -178,    33,  -178
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     9,    10,    38,    12,    13,     0,     0,    32,    33,
       0,     0,     2,     4,     5,     0,    11,     0,     6,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     1,     3,
      44,     0,    39,     0,     0,    34,     0,    36,     0,    35,
       0,    37,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    44,     0,     0,     0,     0,    43,     0,     8,     0,
      24,    26,    25,    27,    22,    23,     0,    28,     0,    29,
       0,    30,     0,    31,     0,     0,     0,     7,     0,     0,
       0,    56,    88,    89,    83,     0,    97,    96,    95,     0,
      86,    90,    87,   100,    91,     0,   104,    81,     0,    42,
      47,     0,     0,    40,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    53,    58,     0,     0,     0,
      84,     0,    45,    92,     0,     0,     0,     0,     0,    48,
      51,     0,     0,    41,    55,     0,    16,    17,    14,    15,
      18,    19,    20,    21,     0,     0,     0,     0,     0,    61,
      67,    65,     0,    72,     0,    63,    66,    74,    75,    73,
       0,    86,     0,    52,    57,    93,    98,     0,    85,   101,
     102,   103,   106,   105,    49,     0,    46,    54,    80,     0,
       0,     0,    71,    70,    62,    64,    69,     0,    59,    94,
       0,    50,    79,     0,   107,   112,   115,   117,    82,     0,
       0,    60,    99,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    68,    76,   108,   109,   110,   111,   113,
     114,   116,   118,    78,     0,    77
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -178,  -178,  -178,   285,  -103,     0,  -178,  -178,   280,   254,
     -80,   -93,  -178,  -178,   250,   192,   -71,  -178,   158,  -177,
    -178,  -178,  -178,   -53,   132,  -111,  -178,  -178,    -9,  -178,
    -178,  -178,   101,    26,    39,   105,   103,  -178
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    11,    12,    13,    14,    79,    16,    17,    31,    32,
      56,    99,   131,    18,    80,    81,   153,   154,   155,   156,
     157,   158,   159,   160,   193,    90,    91,    92,    93,    94,
      95,   167,    96,    97,   195,   196,   197,   198
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      15,    89,   100,   161,   120,   130,    36,   115,    24,   133,
      27,   151,    15,    40,    67,    44,    69,    71,    19,   124,
     125,   126,    72,    21,    45,    46,   214,    25,    23,    26,
      42,    43,   121,   134,    37,   223,    82,    83,    84,    20,
      68,    41,    70,   161,    22,   100,   163,   225,   132,   100,
      54,   151,    82,    83,    84,   144,   145,    55,   146,   147,
     148,    85,    34,    28,   177,   114,   166,    35,   150,    86,
      59,    87,    88,   101,    82,    83,    84,    85,    38,    73,
     102,    98,   191,    39,    74,    86,   123,    87,    88,   117,
      30,   179,   161,    53,   118,    54,    82,    83,    84,    85,
      33,   161,    55,    98,   129,    51,   135,    86,   201,    87,
      88,   118,    60,   161,   152,   169,   170,   171,   189,    47,
      48,    85,   100,   190,    49,    50,    82,    83,    84,    86,
      61,    87,    88,    62,   200,     1,     2,   202,     4,     5,
       6,     7,     8,     9,    10,   144,   145,    63,   146,   147,
     148,    85,    57,    58,   152,   114,   149,    64,   150,    86,
      65,    87,    88,    82,    83,    84,   119,    66,    54,    82,
      83,    84,     1,     2,    75,     4,     5,     6,     7,     8,
       9,    10,   144,   145,    76,   146,   147,   148,    85,    82,
      83,    84,   114,   184,    85,   150,    86,   108,    87,    88,
     109,   178,    86,   110,    87,    88,   194,   194,   204,   205,
     206,   207,    57,    77,    85,   165,   106,   107,   127,   128,
     174,   175,    86,   111,    87,    88,   208,   209,   172,   173,
     215,   216,   217,   218,   194,   194,   194,   194,     1,     2,
     112,     4,     5,     6,     7,     8,     9,   219,   220,   113,
     114,   136,   116,     1,     2,    78,     4,     5,     6,     7,
       8,     9,   122,   137,   138,   139,   142,   140,   141,   168,
     104,     1,     2,     3,     4,     5,     6,     7,     8,     9,
      10,     1,     2,   143,     4,     5,     6,     7,     8,     9,
     162,   101,   180,   176,   181,   187,   210,    29,   182,   188,
     183,    54,   186,   203,   212,   192,   224,    52,   213,   105,
     164,   103,   185,   199,   222,   221,   211
};

static const yytype_uint8 yycheck[] =
{
       0,    54,    55,   114,    84,    98,     3,    78,     9,   102,
      10,   114,    12,     3,     8,     3,     8,    29,     9,    40,
      41,    42,    34,     9,    12,    13,   203,    28,     9,    30,
      12,    13,    85,   104,    31,   212,     3,     4,     5,    30,
      34,    31,    34,   154,    30,    98,   117,   224,   101,   102,
      30,   154,     3,     4,     5,    22,    23,    37,    25,    26,
      27,    28,     3,     0,   135,    32,   119,     8,    35,    36,
      28,    38,    39,    30,     3,     4,     5,    28,     3,    29,
      37,    32,   175,     8,    34,    36,    95,    38,    39,    29,
       5,   144,   203,    28,    34,    30,     3,     4,     5,    28,
       5,   212,    37,    32,    33,     5,    29,    36,   188,    38,
      39,    34,     8,   224,   114,   124,   125,   126,    29,    12,
      13,    28,   175,    34,    12,    13,     3,     4,     5,    36,
      31,    38,    39,     8,   187,    12,    13,   190,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    31,    25,    26,
      27,    28,    34,    35,   154,    32,    33,     8,    35,    36,
       8,    38,    39,     3,     4,     5,    28,    34,    30,     3,
       4,     5,    12,    13,    34,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    34,    25,    26,    27,    28,     3,
       4,     5,    32,    33,    28,    35,    36,     3,    38,    39,
       3,    35,    36,     3,    38,    39,   180,   181,     6,     7,
       8,     9,    34,    35,    28,    29,    12,    13,    38,    39,
      33,    34,    36,     3,    38,    39,    10,    11,   127,   128,
     204,   205,   206,   207,   208,   209,   210,   211,    12,    13,
       3,    15,    16,    17,    18,    19,    20,   208,   209,     3,
      32,     8,     5,    12,    13,    29,    15,    16,    17,    18,
      19,    20,    31,     8,     8,     8,    31,    29,    29,    29,
      29,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    12,    13,    31,    15,    16,    17,    18,    19,    20,
      30,    30,    28,    31,    28,    37,    43,    12,    35,    31,
      35,    30,    35,    29,    29,    35,    24,    27,    35,    59,
     118,    57,   154,   181,   211,   210,    44
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    47,    48,    49,    50,    51,    52,    53,    59,     9,
      30,     9,    30,     9,     9,    28,    30,    51,     0,    49,
       5,    54,    55,     5,     3,     8,     3,    31,     3,     8,
       3,    31,    12,    13,     3,    12,    13,    12,    13,    12,
      13,     5,    54,    28,    30,    37,    56,    34,    35,    28,
       8,    31,     8,    31,     8,     8,    34,     8,    34,     8,
      34,    29,    34,    29,    34,    34,    34,    35,    29,    51,
      60,    61,     3,     4,     5,    28,    36,    38,    39,    69,
      71,    72,    73,    74,    75,    76,    78,    79,    32,    57,
      69,    30,    37,    55,    29,    60,    12,    13,     3,     3,
       3,     3,     3,     3,    32,    62,     5,    29,    34,    28,
      56,    69,    31,    74,    40,    41,    42,    38,    39,    33,
      57,    58,    69,    57,    62,    29,     8,     8,     8,     8,
      29,    29,    31,    31,    22,    23,    25,    26,    27,    33,
      35,    50,    51,    62,    63,    64,    65,    66,    67,    68,
      69,    71,    30,    62,    61,    29,    69,    77,    29,    74,
      74,    74,    78,    78,    33,    34,    31,    62,    35,    69,
      28,    28,    35,    35,    33,    64,    35,    37,    31,    29,
      34,    57,    35,    70,    79,    80,    81,    82,    83,    70,
      69,    56,    69,    29,     6,     7,     8,     9,    10,    11,
      43,    44,    29,    35,    65,    79,    79,    79,    79,    80,
      80,    81,    82,    65,    24,    65
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    46,    47,    48,    48,    49,    49,    50,    50,    51,
      51,    51,    52,    52,    52,    52,    52,    52,    52,    52,
      52,    52,    52,    52,    52,    52,    52,    52,    52,    52,
      52,    52,    52,    52,    52,    52,    52,    52,    53,    54,
      54,    55,    55,    55,    55,    56,    56,    57,    57,    57,
      58,    58,    59,    59,    59,    59,    60,    60,    61,    61,
      61,    62,    62,    63,    63,    64,    64,    65,    65,    65,
      65,    65,    65,    65,    65,    65,    66,    66,    67,    68,
      68,    69,    70,    71,    71,    72,    72,    72,    73,    73,
      74,    74,    74,    75,    75,    76,    76,    76,    77,    77,
      78,    78,    78,    78,    79,    79,    79,    80,    80,    80,
      80,    80,    81,    81,    81,    82,    82,    83,    83
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     4,     3,     1,
       1,     1,     1,     1,     6,     6,     6,     6,     6,     6,
       6,     6,     4,     4,     4,     4,     4,     4,     4,     4,
       4,     4,     1,     1,     3,     3,     3,     3,     1,     1,
       3,     4,     3,     2,     1,     3,     4,     1,     2,     3,
       3,     1,     6,     5,     6,     5,     1,     3,     2,     4,
       5,     2,     3,     1,     2,     1,     1,     1,     4,     2,
       2,     2,     1,     1,     1,     1,     5,     7,     5,     3,
       2,     1,     1,     1,     2,     3,     1,     1,     1,     1,
       1,     1,     2,     3,     4,     1,     1,     1,     1,     3,
       1,     3,     3,     3,     1,     3,     3,     1,     3,     3,
       3,     3,     1,     3,     3,     1,     3,     1,     3
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
  YY_USE (yykind);
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
#line 139 "/workspace/src/frontend/parser.y"
             {
        root = unique_ptr<CompUnitAST>((yyvsp[0].compUnit));
    }
#line 1724 "/workspace/src/frontend/parser.cpp"
    break;

  case 3: /* CompUnit: CompUnit DeclDef  */
#line 145 "/workspace/src/frontend/parser.y"
                     {
        (yyval.compUnit) = (yyvsp[-1].compUnit);
        (yyval.compUnit)->declDefList.push_back(unique_ptr<DeclDefAST>((yyvsp[0].declDef)));
    }
#line 1733 "/workspace/src/frontend/parser.cpp"
    break;

  case 4: /* CompUnit: DeclDef  */
#line 149 "/workspace/src/frontend/parser.y"
            {
        (yyval.compUnit) = make_node<CompUnitAST>();
        (yyval.compUnit)->declDefList.push_back(unique_ptr<DeclDefAST>((yyvsp[0].declDef)));
    }
#line 1742 "/workspace/src/frontend/parser.cpp"
    break;

  case 5: /* DeclDef: Decl  */
#line 156 "/workspace/src/frontend/parser.y"
         {
        (yyval.declDef) = make_node<DeclDefAST>();
        (yyval.declDef)->Decl = unique_ptr<DeclAST>((yyvsp[0].decl));
    }
#line 1751 "/workspace/src/frontend/parser.cpp"
    break;

  case 6: /* DeclDef: FuncDef  */
#line 160 "/workspace/src/frontend/parser.y"
            {
        (yyval.declDef) = make_node<DeclDefAST>();
        (yyval.declDef)->funcDef = unique_ptr<FuncDefAST>((yyvsp[0].funcDef));
    }
#line 1760 "/workspace/src/frontend/parser.cpp"
    break;

  case 7: /* Decl: CONST BType DefList SEMICOLON  */
#line 167 "/workspace/src/frontend/parser.y"
                                  {
        (yyval.decl) = make_node<DeclAST>();
        (yyval.decl)->isConst = true;
        (yyval.decl)->bType = *(yyvsp[-2].type_spec);
        delete (yyvsp[-2].type_spec);
        (yyval.decl)->defList.swap((yyvsp[-1].defList)->list);
    }
#line 1772 "/workspace/src/frontend/parser.cpp"
    break;

  case 8: /* Decl: BType DefList SEMICOLON  */
#line 174 "/workspace/src/frontend/parser.y"
                            {
        (yyval.decl) = make_node<DeclAST>();
        (yyval.decl)->isConst = false;
        (yyval.decl)->bType = *(yyvsp[-2].type_spec);
        delete (yyvsp[-2].type_spec);
        (yyval.decl)->defList.swap((yyvsp[-1].defList)->list);
    }
#line 1784 "/workspace/src/frontend/parser.cpp"
    break;

  case 9: /* BType: INTTYPE  */
#line 184 "/workspace/src/frontend/parser.y"
            {
        (yyval.type_spec) = new TypeSpec(TYPE_INT);
    }
#line 1792 "/workspace/src/frontend/parser.cpp"
    break;

  case 10: /* BType: FLOATTYPE  */
#line 187 "/workspace/src/frontend/parser.y"
              {
        (yyval.type_spec) = new TypeSpec(TYPE_FLOAT);
    }
#line 1800 "/workspace/src/frontend/parser.cpp"
    break;

  case 11: /* BType: VecType  */
#line 190 "/workspace/src/frontend/parser.y"
            {
        (yyval.type_spec) = (yyvsp[0].type_spec);
    }
#line 1808 "/workspace/src/frontend/parser.cpp"
    break;

  case 12: /* VecType: INTVECTYPE  */
#line 198 "/workspace/src/frontend/parser.y"
               {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[0].int_val)));
    }
#line 1816 "/workspace/src/frontend/parser.cpp"
    break;

  case 13: /* VecType: FLOATVECTYPE  */
#line 201 "/workspace/src/frontend/parser.y"
                 {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[0].int_val)));
    }
#line 1824 "/workspace/src/frontend/parser.cpp"
    break;

  case 14: /* VecType: VECTOR LT INTTYPE COMMA INT GT  */
#line 204 "/workspace/src/frontend/parser.y"
                                   {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-1].int_val)));
    }
#line 1832 "/workspace/src/frontend/parser.cpp"
    break;

  case 15: /* VecType: VECTOR LT FLOATTYPE COMMA INT GT  */
#line 207 "/workspace/src/frontend/parser.y"
                                     {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-1].int_val)));
    }
#line 1840 "/workspace/src/frontend/parser.cpp"
    break;

  case 16: /* VecType: VECTOR LT INT COMMA INTTYPE GT  */
#line 210 "/workspace/src/frontend/parser.y"
                                   {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-3].int_val)));
    }
#line 1848 "/workspace/src/frontend/parser.cpp"
    break;

  case 17: /* VecType: VECTOR LT INT COMMA FLOATTYPE GT  */
#line 213 "/workspace/src/frontend/parser.y"
                                     {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-3].int_val)));
    }
#line 1856 "/workspace/src/frontend/parser.cpp"
    break;

  case 18: /* VecType: VECTOR LP INTTYPE COMMA INT RP  */
#line 216 "/workspace/src/frontend/parser.y"
                                   {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-1].int_val)));
    }
#line 1864 "/workspace/src/frontend/parser.cpp"
    break;

  case 19: /* VecType: VECTOR LP FLOATTYPE COMMA INT RP  */
#line 219 "/workspace/src/frontend/parser.y"
                                     {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-1].int_val)));
    }
#line 1872 "/workspace/src/frontend/parser.cpp"
    break;

  case 20: /* VecType: VECTOR LB INTTYPE COMMA INT RB  */
#line 222 "/workspace/src/frontend/parser.y"
                                   {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-1].int_val)));
    }
#line 1880 "/workspace/src/frontend/parser.cpp"
    break;

  case 21: /* VecType: VECTOR LB FLOATTYPE COMMA INT RB  */
#line 225 "/workspace/src/frontend/parser.y"
                                     {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-1].int_val)));
    }
#line 1888 "/workspace/src/frontend/parser.cpp"
    break;

  case 22: /* VecType: VECWIDTH LT INTTYPE GT  */
#line 228 "/workspace/src/frontend/parser.y"
                           {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-3].int_val)));
    }
#line 1896 "/workspace/src/frontend/parser.cpp"
    break;

  case 23: /* VecType: VECWIDTH LT FLOATTYPE GT  */
#line 231 "/workspace/src/frontend/parser.y"
                             {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-3].int_val)));
    }
#line 1904 "/workspace/src/frontend/parser.cpp"
    break;

  case 24: /* VecType: INTTYPE LT INT GT  */
#line 234 "/workspace/src/frontend/parser.y"
                      {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-1].int_val)));
    }
#line 1912 "/workspace/src/frontend/parser.cpp"
    break;

  case 25: /* VecType: FLOATTYPE LT INT GT  */
#line 237 "/workspace/src/frontend/parser.y"
                        {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-1].int_val)));
    }
#line 1920 "/workspace/src/frontend/parser.cpp"
    break;

  case 26: /* VecType: INTTYPE LB INT RB  */
#line 240 "/workspace/src/frontend/parser.y"
                      {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-1].int_val)));
    }
#line 1928 "/workspace/src/frontend/parser.cpp"
    break;

  case 27: /* VecType: FLOATTYPE LB INT RB  */
#line 243 "/workspace/src/frontend/parser.y"
                        {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-1].int_val)));
    }
#line 1936 "/workspace/src/frontend/parser.cpp"
    break;

  case 28: /* VecType: VECTOR LT INTTYPE GT  */
#line 246 "/workspace/src/frontend/parser.y"
                         {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 1944 "/workspace/src/frontend/parser.cpp"
    break;

  case 29: /* VecType: VECTOR LT FLOATTYPE GT  */
#line 249 "/workspace/src/frontend/parser.y"
                           {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 1952 "/workspace/src/frontend/parser.cpp"
    break;

  case 30: /* VecType: VECTOR LP INTTYPE RP  */
#line 252 "/workspace/src/frontend/parser.y"
                         {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 1960 "/workspace/src/frontend/parser.cpp"
    break;

  case 31: /* VecType: VECTOR LP FLOATTYPE RP  */
#line 255 "/workspace/src/frontend/parser.y"
                           {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 1968 "/workspace/src/frontend/parser.cpp"
    break;

  case 32: /* VecType: DYNINTVECTYPE  */
#line 258 "/workspace/src/frontend/parser.y"
                  {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 1976 "/workspace/src/frontend/parser.cpp"
    break;

  case 33: /* VecType: DYNFLOATVECTYPE  */
#line 261 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 1984 "/workspace/src/frontend/parser.cpp"
    break;

  case 34: /* VecType: INTTYPE LT GT  */
#line 264 "/workspace/src/frontend/parser.y"
                  {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 1992 "/workspace/src/frontend/parser.cpp"
    break;

  case 35: /* VecType: FLOATTYPE LT GT  */
#line 267 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 2000 "/workspace/src/frontend/parser.cpp"
    break;

  case 36: /* VecType: INTTYPE LB RB  */
#line 270 "/workspace/src/frontend/parser.y"
                  {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 2008 "/workspace/src/frontend/parser.cpp"
    break;

  case 37: /* VecType: FLOATTYPE LB RB  */
#line 273 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 2016 "/workspace/src/frontend/parser.cpp"
    break;

  case 38: /* VoidType: VOID  */
#line 279 "/workspace/src/frontend/parser.y"
         {
        (yyval.type_spec) = new TypeSpec(TYPE_VOID);
    }
#line 2024 "/workspace/src/frontend/parser.cpp"
    break;

  case 39: /* DefList: Def  */
#line 285 "/workspace/src/frontend/parser.y"
        {
        (yyval.defList) = make_node<DefListAST>();
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2033 "/workspace/src/frontend/parser.cpp"
    break;

  case 40: /* DefList: DefList COMMA Def  */
#line 289 "/workspace/src/frontend/parser.y"
                      {
        (yyval.defList) = (yyvsp[-2].defList);
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2042 "/workspace/src/frontend/parser.cpp"
    break;

  case 41: /* Def: ID Arrays ASSIGN InitVal  */
#line 296 "/workspace/src/frontend/parser.y"
                             {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.def)->arrays.swap((yyvsp[-2].arrays)->list);
        (yyval.def)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 2053 "/workspace/src/frontend/parser.cpp"
    break;

  case 42: /* Def: ID ASSIGN InitVal  */
#line 302 "/workspace/src/frontend/parser.y"
                      {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.def)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 2063 "/workspace/src/frontend/parser.cpp"
    break;

  case 43: /* Def: ID Arrays  */
#line 307 "/workspace/src/frontend/parser.y"
              {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-1].token));
        (yyval.def)->arrays.swap((yyvsp[0].arrays)->list);
    }
#line 2073 "/workspace/src/frontend/parser.cpp"
    break;

  case 44: /* Def: ID  */
#line 312 "/workspace/src/frontend/parser.y"
       {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[0].token));
    }
#line 2082 "/workspace/src/frontend/parser.cpp"
    break;

  case 45: /* Arrays: LB Exp RB  */
#line 319 "/workspace/src/frontend/parser.y"
              {
        (yyval.arrays) = make_node<ArraysAST>();
        (yyval.arrays)->list.push_back(unique_ptr<AddExpAST>((yyvsp[-1].addExp)));
    }
#line 2091 "/workspace/src/frontend/parser.cpp"
    break;

  case 46: /* Arrays: Arrays LB Exp RB  */
#line 323 "/workspace/src/frontend/parser.y"
                     {
        (yyval.arrays) = (yyvsp[-3].arrays);
        (yyval.arrays)->list.push_back(unique_ptr<AddExpAST>((yyvsp[-1].addExp)));
    }
#line 2100 "/workspace/src/frontend/parser.cpp"
    break;

  case 47: /* InitVal: Exp  */
#line 331 "/workspace/src/frontend/parser.y"
        {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2109 "/workspace/src/frontend/parser.cpp"
    break;

  case 48: /* InitVal: LC RC  */
#line 335 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2117 "/workspace/src/frontend/parser.cpp"
    break;

  case 49: /* InitVal: LC InitValList RC  */
#line 338 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->initValList.swap((yyvsp[-1].initValList)->list);
    }
#line 2126 "/workspace/src/frontend/parser.cpp"
    break;

  case 50: /* InitValList: InitValList COMMA InitVal  */
#line 345 "/workspace/src/frontend/parser.y"
                            {
    (yyval.initValList) = (yyvsp[-2].initValList);
    (yyval.initValList)->list.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2135 "/workspace/src/frontend/parser.cpp"
    break;

  case 51: /* InitValList: InitVal  */
#line 349 "/workspace/src/frontend/parser.y"
          {
    (yyval.initValList) = make_node<InitValListAST>();
    (yyval.initValList)->list.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2144 "/workspace/src/frontend/parser.cpp"
    break;

  case 52: /* FuncDef: BType ID LP FuncFParamList RP Block  */
#line 356 "/workspace/src/frontend/parser.y"
                                        {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-5].type_spec);
        delete (yyvsp[-5].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-4].token));
        (yyval.funcDef)->funcFParamList.swap((yyvsp[-2].FuncFParamList)->list);
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2157 "/workspace/src/frontend/parser.cpp"
    break;

  case 53: /* FuncDef: BType ID LP RP Block  */
#line 364 "/workspace/src/frontend/parser.y"
                         {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2169 "/workspace/src/frontend/parser.cpp"
    break;

  case 54: /* FuncDef: VoidType ID LP FuncFParamList RP Block  */
#line 371 "/workspace/src/frontend/parser.y"
                                           {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-5].type_spec);
        delete (yyvsp[-5].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-4].token));
        (yyval.funcDef)->funcFParamList.swap((yyvsp[-2].FuncFParamList)->list);
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2182 "/workspace/src/frontend/parser.cpp"
    break;

  case 55: /* FuncDef: VoidType ID LP RP Block  */
#line 379 "/workspace/src/frontend/parser.y"
                            {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2194 "/workspace/src/frontend/parser.cpp"
    break;

  case 56: /* FuncFParamList: FuncFParam  */
#line 389 "/workspace/src/frontend/parser.y"
               {
        (yyval.FuncFParamList) = make_node<FuncFParamListAST>();
        (yyval.FuncFParamList)->list.push_back(unique_ptr<FuncFParamAST>((yyvsp[0].funcFParam)));
    }
#line 2203 "/workspace/src/frontend/parser.cpp"
    break;

  case 57: /* FuncFParamList: FuncFParamList COMMA FuncFParam  */
#line 393 "/workspace/src/frontend/parser.y"
                                    {
        (yyval.FuncFParamList) = (yyvsp[-2].FuncFParamList);
        (yyval.FuncFParamList)->list.push_back(unique_ptr<FuncFParamAST>((yyvsp[0].funcFParam)));
    }
#line 2212 "/workspace/src/frontend/parser.cpp"
    break;

  case 58: /* FuncFParam: BType ID  */
#line 400 "/workspace/src/frontend/parser.y"
             {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-1].type_spec);
        delete (yyvsp[-1].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[0].token));
        (yyval.funcFParam)->isArray = false;
    }
#line 2224 "/workspace/src/frontend/parser.cpp"
    break;

  case 59: /* FuncFParam: BType ID LB RB  */
#line 407 "/workspace/src/frontend/parser.y"
                   {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-3].type_spec);
        delete (yyvsp[-3].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.funcFParam)->isArray = true;
    }
#line 2236 "/workspace/src/frontend/parser.cpp"
    break;

  case 60: /* FuncFParam: BType ID LB RB Arrays  */
#line 414 "/workspace/src/frontend/parser.y"
                          {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcFParam)->isArray = true;
        (yyval.funcFParam)->arrays.swap((yyvsp[0].arrays)->list);
    }
#line 2249 "/workspace/src/frontend/parser.cpp"
    break;

  case 61: /* Block: LC RC  */
#line 425 "/workspace/src/frontend/parser.y"
          {
        (yyval.block) = make_node<BlockAST>();
    }
#line 2257 "/workspace/src/frontend/parser.cpp"
    break;

  case 62: /* Block: LC BlockItemList RC  */
#line 428 "/workspace/src/frontend/parser.y"
                        {
        (yyval.block) = make_node<BlockAST>();
        (yyval.block)->blockItemList.swap((yyvsp[-1].blockItemList)->list);
    }
#line 2266 "/workspace/src/frontend/parser.cpp"
    break;

  case 63: /* BlockItemList: BlockItem  */
#line 435 "/workspace/src/frontend/parser.y"
              {
        (yyval.blockItemList) = make_node<BlockItemListAST>();
        (yyval.blockItemList)->list.push_back(unique_ptr<BlockItemAST>((yyvsp[0].blockItem)));
    }
#line 2275 "/workspace/src/frontend/parser.cpp"
    break;

  case 64: /* BlockItemList: BlockItemList BlockItem  */
#line 439 "/workspace/src/frontend/parser.y"
                            {
        (yyval.blockItemList) = (yyvsp[-1].blockItemList);
        (yyval.blockItemList)->list.push_back(unique_ptr<BlockItemAST>((yyvsp[0].blockItem)));
    }
#line 2284 "/workspace/src/frontend/parser.cpp"
    break;

  case 65: /* BlockItem: Decl  */
#line 446 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>();
        (yyval.blockItem)->decl = unique_ptr<DeclAST>((yyvsp[0].decl));
    }
#line 2293 "/workspace/src/frontend/parser.cpp"
    break;

  case 66: /* BlockItem: Stmt  */
#line 450 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>();
        (yyval.blockItem)->stmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2302 "/workspace/src/frontend/parser.cpp"
    break;

  case 67: /* Stmt: SEMICOLON  */
#line 457 "/workspace/src/frontend/parser.y"
              {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = SEMI;
    }
#line 2311 "/workspace/src/frontend/parser.cpp"
    break;

  case 68: /* Stmt: LVal ASSIGN Exp SEMICOLON  */
#line 461 "/workspace/src/frontend/parser.y"
                              {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = ASS;
        (yyval.stmt)->lVal = unique_ptr<LValAST>((yyvsp[-3].lVal));
        (yyval.stmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2322 "/workspace/src/frontend/parser.cpp"
    break;

  case 69: /* Stmt: Exp SEMICOLON  */
#line 467 "/workspace/src/frontend/parser.y"
                  {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = EXP;
        (yyval.stmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2332 "/workspace/src/frontend/parser.cpp"
    break;

  case 70: /* Stmt: CONTINUE SEMICOLON  */
#line 472 "/workspace/src/frontend/parser.y"
                       {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = CONT;
    }
#line 2341 "/workspace/src/frontend/parser.cpp"
    break;

  case 71: /* Stmt: BREAK SEMICOLON  */
#line 476 "/workspace/src/frontend/parser.y"
                    {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = BRE;
    }
#line 2350 "/workspace/src/frontend/parser.cpp"
    break;

  case 72: /* Stmt: Block  */
#line 480 "/workspace/src/frontend/parser.y"
          {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = BLK;
        (yyval.stmt)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2360 "/workspace/src/frontend/parser.cpp"
    break;

  case 73: /* Stmt: ReturnStmt  */
#line 485 "/workspace/src/frontend/parser.y"
               {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = RET;
        (yyval.stmt)->returnStmt = unique_ptr<ReturnStmtAST>((yyvsp[0].returnStmt));
    }
#line 2370 "/workspace/src/frontend/parser.cpp"
    break;

  case 74: /* Stmt: SelectStmt  */
#line 490 "/workspace/src/frontend/parser.y"
               {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = SEL;
        (yyval.stmt)->selectStmt = unique_ptr<SelectStmtAST>((yyvsp[0].selectStmt));
    }
#line 2380 "/workspace/src/frontend/parser.cpp"
    break;

  case 75: /* Stmt: IterationStmt  */
#line 495 "/workspace/src/frontend/parser.y"
                  {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = ITER;
        (yyval.stmt)->iterationStmt = unique_ptr<IterationStmtAST>((yyvsp[0].iterationStmt));
    }
#line 2390 "/workspace/src/frontend/parser.cpp"
    break;

  case 76: /* SelectStmt: IF LP Cond RP Stmt  */
#line 503 "/workspace/src/frontend/parser.y"
                                             {
        (yyval.selectStmt) = make_node<SelectStmtAST>();
        (yyval.selectStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.selectStmt)->ifStmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2400 "/workspace/src/frontend/parser.cpp"
    break;

  case 77: /* SelectStmt: IF LP Cond RP Stmt ELSE Stmt  */
#line 508 "/workspace/src/frontend/parser.y"
                                 {
        (yyval.selectStmt) = make_node<SelectStmtAST>();
        (yyval.selectStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-4].lOrExp));
        (yyval.selectStmt)->ifStmt = unique_ptr<StmtAST>((yyvsp[-2].stmt));
        (yyval.selectStmt)->elseStmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2411 "/workspace/src/frontend/parser.cpp"
    break;

  case 78: /* IterationStmt: WHILE LP Cond RP Stmt  */
#line 517 "/workspace/src/frontend/parser.y"
                          {
        (yyval.iterationStmt) = make_node<IterationStmtAST>();
        (yyval.iterationStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.iterationStmt)->stmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2421 "/workspace/src/frontend/parser.cpp"
    break;

  case 79: /* ReturnStmt: RETURN Exp SEMICOLON  */
#line 525 "/workspace/src/frontend/parser.y"
                         {
        (yyval.returnStmt) = make_node<ReturnStmtAST>();
        (yyval.returnStmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2430 "/workspace/src/frontend/parser.cpp"
    break;

  case 80: /* ReturnStmt: RETURN SEMICOLON  */
#line 529 "/workspace/src/frontend/parser.y"
                     {
        (yyval.returnStmt) = make_node<ReturnStmtAST>();
    }
#line 2438 "/workspace/src/frontend/parser.cpp"
    break;

  case 81: /* Exp: AddExp  */
#line 535 "/workspace/src/frontend/parser.y"
           {
        (yyval.addExp) = (yyvsp[0].addExp);
    }
#line 2446 "/workspace/src/frontend/parser.cpp"
    break;

  case 82: /* Cond: LOrExp  */
#line 541 "/workspace/src/frontend/parser.y"
           {
        (yyval.lOrExp) = (yyvsp[0].lOrExp);
    }
#line 2454 "/workspace/src/frontend/parser.cpp"
    break;

  case 83: /* LVal: ID  */
#line 547 "/workspace/src/frontend/parser.y"
       {
        (yyval.lVal) = make_node<LValAST>();
        (yyval.lVal)->id = unique_ptr<string>((yyvsp[0].token));
    }
#line 2463 "/workspace/src/frontend/parser.cpp"
    break;

  case 84: /* LVal: ID Arrays  */
#line 551 "/workspace/src/frontend/parser.y"
              {
        (yyval.lVal) = make_node<LValAST>();
        (yyval.lVal)->id = unique_ptr<string>((yyvsp[-1].token));
        (yyval.lVal)->arrays.swap((yyvsp[0].arrays)->list);
    }
#line 2473 "/workspace/src/frontend/parser.cpp"
    break;

  case 85: /* PrimaryExp: LP Exp RP  */
#line 559 "/workspace/src/frontend/parser.y"
              {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2482 "/workspace/src/frontend/parser.cpp"
    break;

  case 86: /* PrimaryExp: LVal  */
#line 563 "/workspace/src/frontend/parser.y"
         {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->lval = unique_ptr<LValAST>((yyvsp[0].lVal));
    }
#line 2491 "/workspace/src/frontend/parser.cpp"
    break;

  case 87: /* PrimaryExp: Number  */
#line 567 "/workspace/src/frontend/parser.y"
           {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->number = unique_ptr<NumberAST>((yyvsp[0].number));
    }
#line 2500 "/workspace/src/frontend/parser.cpp"
    break;

  case 88: /* Number: INT  */
#line 574 "/workspace/src/frontend/parser.y"
        {
        (yyval.number) = make_node<NumberAST>();
        (yyval.number)->isInt = true;
        (yyval.number)->intval = (yyvsp[0].int_val);
    }
#line 2510 "/workspace/src/frontend/parser.cpp"
    break;

  case 89: /* Number: FLOAT  */
#line 579 "/workspace/src/frontend/parser.y"
          {
        (yyval.number) = make_node<NumberAST>();
        (yyval.number)->isInt = false;
        (yyval.number)->floatval = (yyvsp[0].float_val);
    }
#line 2520 "/workspace/src/frontend/parser.cpp"
    break;

  case 90: /* UnaryExp: PrimaryExp  */
#line 587 "/workspace/src/frontend/parser.y"
               {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->primaryExp = unique_ptr<PrimaryExpAST>((yyvsp[0].primaryExp));
    }
#line 2529 "/workspace/src/frontend/parser.cpp"
    break;

  case 91: /* UnaryExp: Call  */
#line 591 "/workspace/src/frontend/parser.y"
         {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->call = unique_ptr<CallAST>((yyvsp[0].call));
    }
#line 2538 "/workspace/src/frontend/parser.cpp"
    break;

  case 92: /* UnaryExp: UnaryOp UnaryExp  */
#line 595 "/workspace/src/frontend/parser.y"
                     {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->op = (yyvsp[-1].op);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2548 "/workspace/src/frontend/parser.cpp"
    break;

  case 93: /* Call: ID LP RP  */
#line 603 "/workspace/src/frontend/parser.y"
             {
        (yyval.call) = make_node<CallAST>();
        (yyval.call)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.call)->lineno = (yylsp[-2]).first_line;
    }
#line 2558 "/workspace/src/frontend/parser.cpp"
    break;

  case 94: /* Call: ID LP FuncCParamList RP  */
#line 608 "/workspace/src/frontend/parser.y"
                            {
        (yyval.call) = make_node<CallAST>();
        (yyval.call)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.call)->funcCParamList.swap((yyvsp[-1].funcCParamList)->list);
        (yyval.call)->lineno = (yylsp[-3]).first_line;
    }
#line 2569 "/workspace/src/frontend/parser.cpp"
    break;

  case 95: /* UnaryOp: ADD  */
#line 617 "/workspace/src/frontend/parser.y"
        {
        (yyval.op) = UOP_ADD;
    }
#line 2577 "/workspace/src/frontend/parser.cpp"
    break;

  case 96: /* UnaryOp: MINUS  */
#line 620 "/workspace/src/frontend/parser.y"
          {
        (yyval.op) = UOP_MINUS;
    }
#line 2585 "/workspace/src/frontend/parser.cpp"
    break;

  case 97: /* UnaryOp: NOT  */
#line 623 "/workspace/src/frontend/parser.y"
        {
        (yyval.op) = UOP_NOT;
    }
#line 2593 "/workspace/src/frontend/parser.cpp"
    break;

  case 98: /* FuncCParamList: Exp  */
#line 629 "/workspace/src/frontend/parser.y"
        {
        (yyval.funcCParamList) = make_node<FuncCParamListAST>();
        (yyval.funcCParamList)->list.push_back(unique_ptr<AddExpAST>((yyvsp[0].addExp)));
    }
#line 2602 "/workspace/src/frontend/parser.cpp"
    break;

  case 99: /* FuncCParamList: FuncCParamList COMMA Exp  */
#line 633 "/workspace/src/frontend/parser.y"
                             {
        (yyval.funcCParamList) = (FuncCParamListAST*) (yyvsp[-2].funcCParamList);
        (yyval.funcCParamList)->list.push_back(unique_ptr<AddExpAST>((yyvsp[0].addExp)));
    }
#line 2611 "/workspace/src/frontend/parser.cpp"
    break;

  case 100: /* MulExp: UnaryExp  */
#line 640 "/workspace/src/frontend/parser.y"
             {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2620 "/workspace/src/frontend/parser.cpp"
    break;

  case 101: /* MulExp: MulExp MUL UnaryExp  */
#line 644 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MUL;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2631 "/workspace/src/frontend/parser.cpp"
    break;

  case 102: /* MulExp: MulExp DIV UnaryExp  */
#line 650 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_DIV;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2642 "/workspace/src/frontend/parser.cpp"
    break;

  case 103: /* MulExp: MulExp MOD UnaryExp  */
#line 656 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MOD;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2653 "/workspace/src/frontend/parser.cpp"
    break;

  case 104: /* AddExp: MulExp  */
#line 665 "/workspace/src/frontend/parser.y"
           {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2662 "/workspace/src/frontend/parser.cpp"
    break;

  case 105: /* AddExp: AddExp ADD MulExp  */
#line 669 "/workspace/src/frontend/parser.y"
                      {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_ADD;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2673 "/workspace/src/frontend/parser.cpp"
    break;

  case 106: /* AddExp: AddExp MINUS MulExp  */
#line 675 "/workspace/src/frontend/parser.y"
                        {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_MINUS;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2684 "/workspace/src/frontend/parser.cpp"
    break;

  case 107: /* RelExp: AddExp  */
#line 684 "/workspace/src/frontend/parser.y"
           {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2693 "/workspace/src/frontend/parser.cpp"
    break;

  case 108: /* RelExp: RelExp GTE AddExp  */
#line 688 "/workspace/src/frontend/parser.y"
                      {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_GTE;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2704 "/workspace/src/frontend/parser.cpp"
    break;

  case 109: /* RelExp: RelExp LTE AddExp  */
#line 694 "/workspace/src/frontend/parser.y"
                      {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_LTE;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2715 "/workspace/src/frontend/parser.cpp"
    break;

  case 110: /* RelExp: RelExp GT AddExp  */
#line 700 "/workspace/src/frontend/parser.y"
                     {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_GT;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2726 "/workspace/src/frontend/parser.cpp"
    break;

  case 111: /* RelExp: RelExp LT AddExp  */
#line 706 "/workspace/src/frontend/parser.y"
                     {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_LT;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2737 "/workspace/src/frontend/parser.cpp"
    break;

  case 112: /* EqExp: RelExp  */
#line 715 "/workspace/src/frontend/parser.y"
           {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 2746 "/workspace/src/frontend/parser.cpp"
    break;

  case 113: /* EqExp: EqExp EQ RelExp  */
#line 719 "/workspace/src/frontend/parser.y"
                    {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[-2].eqExp));
        (yyval.eqExp)->op = EOP_EQ;
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 2757 "/workspace/src/frontend/parser.cpp"
    break;

  case 114: /* EqExp: EqExp NEQ RelExp  */
#line 725 "/workspace/src/frontend/parser.y"
                     {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[-2].eqExp));
        (yyval.eqExp)->op = EOP_NEQ;
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 2768 "/workspace/src/frontend/parser.cpp"
    break;

  case 115: /* LAndExp: EqExp  */
#line 734 "/workspace/src/frontend/parser.y"
          {
        (yyval.lAndExp) = make_node<LAndExpAST>();
        (yyval.lAndExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[0].eqExp));
    }
#line 2777 "/workspace/src/frontend/parser.cpp"
    break;

  case 116: /* LAndExp: LAndExp AND EqExp  */
#line 738 "/workspace/src/frontend/parser.y"
                      {
        (yyval.lAndExp) = make_node<LAndExpAST>();
        (yyval.lAndExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[-2].lAndExp));
        (yyval.lAndExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[0].eqExp));
    }
#line 2787 "/workspace/src/frontend/parser.cpp"
    break;

  case 117: /* LOrExp: LAndExp  */
#line 746 "/workspace/src/frontend/parser.y"
            {
        (yyval.lOrExp) = make_node<LOrExpAST>();
        (yyval.lOrExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[0].lAndExp));
    }
#line 2796 "/workspace/src/frontend/parser.cpp"
    break;

  case 118: /* LOrExp: LOrExp OR LAndExp  */
#line 750 "/workspace/src/frontend/parser.y"
                      {
        (yyval.lOrExp) = make_node<LOrExpAST>();
        (yyval.lOrExp)->lOrExp = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.lOrExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[0].lAndExp));
    }
#line 2806 "/workspace/src/frontend/parser.cpp"
    break;


#line 2810 "/workspace/src/frontend/parser.cpp"

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

#line 755 "/workspace/src/frontend/parser.y"


void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
