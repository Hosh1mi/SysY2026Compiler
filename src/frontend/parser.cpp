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
  YYSYMBOL_VecWidth = 53,                  /* VecWidth  */
  YYSYMBOL_VoidType = 54,                  /* VoidType  */
  YYSYMBOL_DefList = 55,                   /* DefList  */
  YYSYMBOL_Def = 56,                       /* Def  */
  YYSYMBOL_Arrays = 57,                    /* Arrays  */
  YYSYMBOL_Block = 58,                     /* Block  */
  YYSYMBOL_InitVal = 59,                   /* InitVal  */
  YYSYMBOL_BraceInitVal = 60,              /* BraceInitVal  */
  YYSYMBOL_InitValList = 61,               /* InitValList  */
  YYSYMBOL_FuncDef = 62,                   /* FuncDef  */
  YYSYMBOL_FuncFParamList = 63,            /* FuncFParamList  */
  YYSYMBOL_FuncFParam = 64,                /* FuncFParam  */
  YYSYMBOL_BlockItemList = 65,             /* BlockItemList  */
  YYSYMBOL_BlockItem = 66,                 /* BlockItem  */
  YYSYMBOL_Stmt = 67,                      /* Stmt  */
  YYSYMBOL_SelectStmt = 68,                /* SelectStmt  */
  YYSYMBOL_IterationStmt = 69,             /* IterationStmt  */
  YYSYMBOL_ReturnStmt = 70,                /* ReturnStmt  */
  YYSYMBOL_Exp = 71,                       /* Exp  */
  YYSYMBOL_Cond = 72,                      /* Cond  */
  YYSYMBOL_LVal = 73,                      /* LVal  */
  YYSYMBOL_PrimaryExp = 74,                /* PrimaryExp  */
  YYSYMBOL_Number = 75,                    /* Number  */
  YYSYMBOL_UnaryExp = 76,                  /* UnaryExp  */
  YYSYMBOL_Call = 77,                      /* Call  */
  YYSYMBOL_UnaryOp = 78,                   /* UnaryOp  */
  YYSYMBOL_FuncCParamList = 79,            /* FuncCParamList  */
  YYSYMBOL_MulExp = 80,                    /* MulExp  */
  YYSYMBOL_AddExp = 81,                    /* AddExp  */
  YYSYMBOL_RelExp = 82,                    /* RelExp  */
  YYSYMBOL_EqExp = 83,                     /* EqExp  */
  YYSYMBOL_LAndExp = 84,                   /* LAndExp  */
  YYSYMBOL_LOrExp = 85                     /* LOrExp  */
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
#define YYLAST   417

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  46
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  40
/* YYNRULES -- Number of rules.  */
#define YYNRULES  125
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  244

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
     187,   190,   198,   201,   204,   208,   212,   216,   220,   224,
     228,   232,   236,   239,   242,   246,   250,   254,   258,   261,
     264,   267,   270,   273,   276,   279,   282,   285,   290,   293,
     300,   306,   310,   320,   326,   332,   337,   344,   348,   359,
     362,   372,   375,   379,   386,   389,   396,   400,   407,   415,
     422,   430,   440,   444,   451,   458,   465,   476,   480,   487,
     491,   498,   502,   508,   513,   517,   521,   526,   531,   536,
     544,   549,   558,   566,   570,   576,   582,   588,   592,   600,
     604,   608,   612,   619,   624,   634,   638,   642,   649,   658,
     666,   671,   680,   683,   686,   692,   696,   703,   707,   713,
     719,   728,   732,   738,   747,   751,   757,   763,   769,   778,
     782,   788,   797,   801,   809,   813
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
  "BType", "VecType", "VecWidth", "VoidType", "DefList", "Def", "Arrays",
  "Block", "InitVal", "BraceInitVal", "InitValList", "FuncDef",
  "FuncFParamList", "FuncFParam", "BlockItemList", "BlockItem", "Stmt",
  "SelectStmt", "IterationStmt", "ReturnStmt", "Exp", "Cond", "LVal",
  "PrimaryExp", "Number", "UnaryExp", "Call", "UnaryOp", "FuncCParamList",
  "MulExp", "AddExp", "RelExp", "EqExp", "LAndExp", "LOrExp", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-216)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-55)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     372,    -1,    11,  -216,  -216,  -216,     2,     0,  -216,  -216,
     382,    17,   372,  -216,  -216,    16,  -216,    20,  -216,   162,
      83,   176,    89,    50,   100,    95,   118,    34,  -216,  -216,
     292,   169,  -216,    38,  -216,  -216,  -216,    37,  -216,    65,
    -216,   108,  -216,    68,   142,   158,     4,    61,   126,    98,
     153,   139,   146,    67,   183,   339,   106,   106,    87,    34,
    -216,   354,  -216,  -216,  -216,  -216,  -216,  -216,  -216,    78,
    -216,    78,   294,  -216,    78,  -216,    78,    78,    78,  -216,
     151,   202,   168,  -216,  -216,  -216,    91,   106,    39,  -216,
    -216,  -216,  -216,   179,  -216,  -216,  -216,  -216,   204,   106,
     199,   287,  -216,   106,   311,  -216,   151,   187,   212,   236,
     239,   245,   229,   232,   250,   253,   136,  -216,   241,   151,
     382,    97,   257,   265,   285,  -216,  -216,   312,  -216,  -216,
     106,  -216,   106,   106,   106,   106,   106,   266,  -216,  -216,
     151,  -216,  -216,  -216,  -216,  -216,  -216,  -216,  -216,   299,
     282,   291,   293,   295,   173,  -216,  -216,  -216,    34,  -216,
     210,  -216,  -216,  -216,  -216,  -216,   301,   307,   317,  -216,
    -216,  -216,  -216,   190,   323,   258,   330,  -216,   311,   334,
    -216,  -216,  -216,   199,   199,  -216,  -216,  -216,   344,   106,
     106,  -216,  -216,   247,   300,   301,  -216,  -216,  -216,   106,
     350,  -216,   106,   106,   327,  -216,  -216,  -216,   352,   287,
     248,   322,   353,   338,   374,   -18,   369,   257,  -216,   375,
     273,   106,   106,   106,   106,   106,   106,   106,   106,   273,
    -216,  -216,   381,   287,   287,   287,   287,   248,   248,   322,
     353,  -216,   273,  -216
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     9,    10,    40,    12,    13,     0,     0,    32,    33,
       0,     0,     2,     4,     5,     0,    11,     0,     6,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     1,     3,
      46,     0,    41,     0,    38,    39,    34,     0,    36,     0,
      35,     0,    37,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    46,     0,     0,     0,     0,    45,     0,
       8,     0,    24,    26,    25,    27,    22,    23,    28,     0,
      29,     0,     0,    30,     0,    31,     0,     0,     0,     7,
       0,     0,     0,    62,    93,    94,    87,     0,     0,   104,
     103,   102,    92,     0,    90,    95,    91,   107,    96,     0,
     111,    85,    44,     0,     0,    42,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    59,    64,     0,
       0,     0,    88,     0,     0,    54,    57,     0,    53,    47,
       0,    99,     0,     0,     0,     0,     0,     0,    43,    61,
       0,    14,    15,    16,    17,    18,    19,    20,    21,     0,
       0,     0,     0,     0,     0,    49,    71,    69,     0,    76,
       0,    67,    70,    78,    79,    77,     0,    90,     0,    58,
      63,   100,   105,     0,    89,    54,     0,    55,     0,     0,
     108,   109,   110,   113,   112,    48,    60,    84,     0,     0,
       0,    75,    74,     0,    49,    53,    50,    68,    73,     0,
      65,   101,     0,     0,    55,    56,    97,    83,     0,   114,
     119,   122,   124,    86,     0,    49,     0,    66,   106,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      72,    98,    80,   115,   116,   117,   118,   120,   121,   123,
     125,    82,     0,    81
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -216,  -216,  -216,   395,    73,     3,  -216,    13,  -216,   383,
     349,   -82,   -70,   -98,  -216,  -117,  -216,   348,   296,  -216,
     251,  -215,  -216,  -216,  -216,   -56,   222,  -114,  -216,  -216,
     -73,  -216,  -216,  -216,   240,  -171,   152,   186,   189,  -216
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    11,    12,    13,   157,   158,    16,    37,    17,    31,
      32,    58,   159,   126,    92,   127,    18,    82,    83,   160,
     161,   162,   163,   164,   165,   166,   208,    94,    95,    96,
      97,    98,    99,   173,   100,   101,   210,   211,   212,   213
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      93,   102,   167,    15,   122,   232,   138,   176,    19,    24,
     117,    23,    68,    27,   241,    15,   -51,    28,   209,   209,
      21,    30,   -54,   -54,   -54,    33,   131,   243,    25,    20,
      26,   123,   128,    39,    41,    43,   139,    48,    69,    53,
     167,    22,    84,    85,    86,    62,   167,   137,   128,   169,
     233,   234,   235,   236,   209,   209,   209,   209,    81,   180,
     181,   182,    44,    45,    81,   172,    61,    87,   128,    70,
     186,   124,   125,    14,   179,    89,   176,    90,    91,   167,
     205,    34,   108,    35,   109,    14,    34,   112,    35,   113,
     114,   115,    34,   188,    35,    71,    63,    56,   195,    65,
      84,    85,    86,    34,    57,    35,   167,    49,    50,    84,
      85,    86,    46,    47,    38,   167,    64,   103,   217,   121,
      42,    56,   128,    81,   104,    87,   171,    73,   167,    88,
      51,    52,    74,    89,    87,    90,    91,   195,    88,    84,
      85,    86,    89,   216,    90,    91,   218,   219,     1,     2,
      66,     4,     5,     6,     7,     8,     9,    10,   149,   150,
      72,   151,   152,   153,    87,    34,    67,    35,   154,   155,
      36,   156,    89,    77,    90,    91,    84,    85,    86,    34,
      78,    35,    75,   116,    40,     1,     2,    76,     4,     5,
       6,     7,     8,     9,    10,   149,   150,   119,   151,   152,
     153,    87,   120,    59,    60,   193,   194,   118,   156,    89,
     129,    90,    91,    84,    85,    86,   140,    59,    79,   201,
     141,   120,     1,     2,   202,     4,     5,     6,     7,     8,
       9,    10,   149,   150,   130,   151,   152,   153,    87,   132,
     133,   134,   154,   196,   142,   156,    89,   143,    90,    91,
      84,    85,    86,   144,   221,   222,   223,   224,   145,     1,
       2,   146,     4,     5,     6,     7,     8,     9,    10,   149,
     150,   168,   151,   152,   153,    87,    84,    85,    86,   193,
     215,   147,   156,    89,   148,    90,    91,   103,    84,    85,
      86,   -51,   -51,   -51,   174,   149,   150,   185,   151,   152,
     153,    87,    84,    85,    86,   154,   110,   111,   156,    89,
     189,    90,    91,    87,    84,    85,    86,   124,   175,   190,
      55,    89,    56,    90,    91,   135,   136,    87,   191,    57,
     192,    88,   225,   226,   187,    89,   198,    90,    91,    87,
     -54,   -54,   -54,   124,   199,   177,   178,    89,   200,    90,
      91,     1,     2,   203,     4,     5,     6,     7,     8,     9,
     -52,   -52,   -52,   204,   178,   206,     1,     2,    80,     4,
       5,     6,     7,     8,     9,   183,   184,   237,   238,   207,
      56,   220,   228,   106,     1,     2,     3,     4,     5,     6,
       7,     8,     9,    10,     1,     2,   227,     4,     5,     6,
       7,     8,     9,   229,   230,   242,   231,    29,   105,   107,
      54,   197,   214,   239,     0,     0,   170,   240
};

static const yytype_int16 yycheck[] =
{
      56,    57,   116,     0,    86,   220,   104,   124,     9,     9,
      80,     9,     8,    10,   229,    12,    34,     0,   189,   190,
       9,     5,    40,    41,    42,     5,    99,   242,    28,    30,
      30,    87,    88,    20,    21,    22,   106,    24,    34,     5,
     154,    30,     3,     4,     5,     8,   160,   103,   104,   119,
     221,   222,   223,   224,   225,   226,   227,   228,    55,   132,
     133,   134,    12,    13,    61,   121,    28,    28,   124,     8,
     140,    32,    33,     0,   130,    36,   193,    38,    39,   193,
     178,     3,    69,     5,    71,    12,     3,    74,     5,    76,
      77,    78,     3,   149,     5,    34,    31,    30,   154,    31,
       3,     4,     5,     3,    37,     5,   220,    12,    13,     3,
       4,     5,    12,    13,    31,   229,     8,    30,   200,    28,
      31,    30,   178,   120,    37,    28,    29,    29,   242,    32,
      12,    13,    34,    36,    28,    38,    39,   193,    32,     3,
       4,     5,    36,   199,    38,    39,   202,   203,    12,    13,
       8,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      34,    25,    26,    27,    28,     3,     8,     5,    32,    33,
       8,    35,    36,    34,    38,    39,     3,     4,     5,     3,
      34,     5,    29,    32,     8,    12,    13,    34,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    29,    25,    26,
      27,    28,    34,    34,    35,    32,    33,     5,    35,    36,
      31,    38,    39,     3,     4,     5,    29,    34,    35,    29,
       8,    34,    12,    13,    34,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    30,    25,    26,    27,    28,    40,
      41,    42,    32,    33,     8,    35,    36,     8,    38,    39,
       3,     4,     5,     8,     6,     7,     8,     9,    29,    12,
      13,    29,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    30,    25,    26,    27,    28,     3,     4,     5,    32,
      33,    31,    35,    36,    31,    38,    39,    30,     3,     4,
       5,    33,    34,    35,    29,    22,    23,    31,    25,    26,
      27,    28,     3,     4,     5,    32,    12,    13,    35,    36,
      28,    38,    39,    28,     3,     4,     5,    32,    33,    28,
      28,    36,    30,    38,    39,    38,    39,    28,    35,    37,
      35,    32,    10,    11,    35,    36,    35,    38,    39,    28,
      40,    41,    42,    32,    37,    33,    34,    36,    31,    38,
      39,    12,    13,    30,    15,    16,    17,    18,    19,    20,
      33,    34,    35,    33,    34,    31,    12,    13,    29,    15,
      16,    17,    18,    19,    20,   135,   136,   225,   226,    35,
      30,    29,    44,    29,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    12,    13,    43,    15,    16,    17,
      18,    19,    20,    29,    35,    24,    31,    12,    59,    61,
      27,   160,   190,   227,    -1,    -1,   120,   228
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    47,    48,    49,    50,    51,    52,    54,    62,     9,
      30,     9,    30,     9,     9,    28,    30,    51,     0,    49,
       5,    55,    56,     5,     3,     5,     8,    53,    31,    53,
       8,    53,    31,    53,    12,    13,    12,    13,    53,    12,
      13,    12,    13,     5,    55,    28,    30,    37,    57,    34,
      35,    28,     8,    31,     8,    31,     8,     8,     8,    34,
       8,    34,    34,    29,    34,    29,    34,    34,    34,    35,
      29,    51,    63,    64,     3,     4,     5,    28,    32,    36,
      38,    39,    60,    71,    73,    74,    75,    76,    77,    78,
      80,    81,    71,    30,    37,    56,    29,    63,    53,    53,
      12,    13,    53,    53,    53,    53,    32,    58,     5,    29,
      34,    28,    57,    71,    32,    33,    59,    61,    71,    31,
      30,    76,    40,    41,    42,    38,    39,    71,    59,    58,
      29,     8,     8,     8,     8,    29,    29,    31,    31,    22,
      23,    25,    26,    27,    32,    33,    35,    50,    51,    58,
      65,    66,    67,    68,    69,    70,    71,    73,    30,    58,
      64,    29,    71,    79,    29,    33,    61,    33,    34,    71,
      76,    76,    76,    80,    80,    31,    58,    35,    71,    28,
      28,    35,    35,    32,    33,    71,    33,    66,    35,    37,
      31,    29,    34,    30,    33,    59,    31,    35,    72,    81,
      82,    83,    84,    85,    72,    33,    71,    57,    71,    71,
      29,     6,     7,     8,     9,    10,    11,    43,    44,    29,
      35,    31,    67,    81,    81,    81,    81,    82,    82,    83,
      84,    67,    24,    67
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    46,    47,    48,    48,    49,    49,    50,    50,    51,
      51,    51,    52,    52,    52,    52,    52,    52,    52,    52,
      52,    52,    52,    52,    52,    52,    52,    52,    52,    52,
      52,    52,    52,    52,    52,    52,    52,    52,    53,    53,
      54,    55,    55,    56,    56,    56,    56,    57,    57,    58,
      58,    59,    59,    59,    60,    60,    61,    61,    62,    62,
      62,    62,    63,    63,    64,    64,    64,    65,    65,    66,
      66,    67,    67,    67,    67,    67,    67,    67,    67,    67,
      68,    68,    69,    70,    70,    71,    72,    73,    73,    74,
      74,    74,    74,    75,    75,    76,    76,    76,    76,    76,
      77,    77,    78,    78,    78,    79,    79,    80,    80,    80,
      80,    81,    81,    81,    82,    82,    82,    82,    82,    83,
      83,    83,    84,    84,    85,    85
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     1,     1,     4,     3,     1,
       1,     1,     1,     1,     6,     6,     6,     6,     6,     6,
       6,     6,     4,     4,     4,     4,     4,     4,     4,     4,
       4,     4,     1,     1,     3,     3,     3,     3,     1,     1,
       1,     1,     3,     4,     3,     2,     1,     3,     4,     2,
       3,     2,     3,     1,     2,     3,     3,     1,     6,     5,
       6,     5,     1,     3,     2,     4,     5,     1,     2,     1,
       1,     1,     4,     2,     2,     2,     1,     1,     1,     1,
       5,     7,     5,     3,     2,     1,     1,     1,     2,     3,
       1,     1,     1,     1,     1,     1,     1,     4,     6,     2,
       3,     4,     1,     1,     1,     1,     3,     1,     3,     3,
       3,     1,     3,     3,     1,     3,     3,     3,     3,     1,
       3,     3,     1,     3,     1,     3
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
#line 1755 "/workspace/src/frontend/parser.cpp"
    break;

  case 3: /* CompUnit: CompUnit DeclDef  */
#line 145 "/workspace/src/frontend/parser.y"
                     {
        (yyval.compUnit) = (yyvsp[-1].compUnit);
        (yyval.compUnit)->declDefList.push_back(unique_ptr<DeclDefAST>((yyvsp[0].declDef)));
    }
#line 1764 "/workspace/src/frontend/parser.cpp"
    break;

  case 4: /* CompUnit: DeclDef  */
#line 149 "/workspace/src/frontend/parser.y"
            {
        (yyval.compUnit) = make_node<CompUnitAST>();
        (yyval.compUnit)->declDefList.push_back(unique_ptr<DeclDefAST>((yyvsp[0].declDef)));
    }
#line 1773 "/workspace/src/frontend/parser.cpp"
    break;

  case 5: /* DeclDef: Decl  */
#line 156 "/workspace/src/frontend/parser.y"
         {
        (yyval.declDef) = make_node<DeclDefAST>();
        (yyval.declDef)->Decl = unique_ptr<DeclAST>((yyvsp[0].decl));
    }
#line 1782 "/workspace/src/frontend/parser.cpp"
    break;

  case 6: /* DeclDef: FuncDef  */
#line 160 "/workspace/src/frontend/parser.y"
            {
        (yyval.declDef) = make_node<DeclDefAST>();
        (yyval.declDef)->funcDef = unique_ptr<FuncDefAST>((yyvsp[0].funcDef));
    }
#line 1791 "/workspace/src/frontend/parser.cpp"
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
#line 1803 "/workspace/src/frontend/parser.cpp"
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
#line 1815 "/workspace/src/frontend/parser.cpp"
    break;

  case 9: /* BType: INTTYPE  */
#line 184 "/workspace/src/frontend/parser.y"
            {
        (yyval.type_spec) = new TypeSpec(TYPE_INT);
    }
#line 1823 "/workspace/src/frontend/parser.cpp"
    break;

  case 10: /* BType: FLOATTYPE  */
#line 187 "/workspace/src/frontend/parser.y"
              {
        (yyval.type_spec) = new TypeSpec(TYPE_FLOAT);
    }
#line 1831 "/workspace/src/frontend/parser.cpp"
    break;

  case 11: /* BType: VecType  */
#line 190 "/workspace/src/frontend/parser.y"
            {
        (yyval.type_spec) = (yyvsp[0].type_spec);
    }
#line 1839 "/workspace/src/frontend/parser.cpp"
    break;

  case 12: /* VecType: INTVECTYPE  */
#line 198 "/workspace/src/frontend/parser.y"
               {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[0].int_val)));
    }
#line 1847 "/workspace/src/frontend/parser.cpp"
    break;

  case 13: /* VecType: FLOATVECTYPE  */
#line 201 "/workspace/src/frontend/parser.y"
                 {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[0].int_val)));
    }
#line 1855 "/workspace/src/frontend/parser.cpp"
    break;

  case 14: /* VecType: VECTOR LT INTTYPE COMMA VecWidth GT  */
#line 204 "/workspace/src/frontend/parser.y"
                                        {
        (yyvsp[-1].type_spec)->element = TYPE_INT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1864 "/workspace/src/frontend/parser.cpp"
    break;

  case 15: /* VecType: VECTOR LT FLOATTYPE COMMA VecWidth GT  */
#line 208 "/workspace/src/frontend/parser.y"
                                          {
        (yyvsp[-1].type_spec)->element = TYPE_FLOAT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1873 "/workspace/src/frontend/parser.cpp"
    break;

  case 16: /* VecType: VECTOR LT VecWidth COMMA INTTYPE GT  */
#line 212 "/workspace/src/frontend/parser.y"
                                        {
        (yyvsp[-3].type_spec)->element = TYPE_INT;
        (yyval.type_spec) = (yyvsp[-3].type_spec);
    }
#line 1882 "/workspace/src/frontend/parser.cpp"
    break;

  case 17: /* VecType: VECTOR LT VecWidth COMMA FLOATTYPE GT  */
#line 216 "/workspace/src/frontend/parser.y"
                                          {
        (yyvsp[-3].type_spec)->element = TYPE_FLOAT;
        (yyval.type_spec) = (yyvsp[-3].type_spec);
    }
#line 1891 "/workspace/src/frontend/parser.cpp"
    break;

  case 18: /* VecType: VECTOR LP INTTYPE COMMA VecWidth RP  */
#line 220 "/workspace/src/frontend/parser.y"
                                        {
        (yyvsp[-1].type_spec)->element = TYPE_INT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1900 "/workspace/src/frontend/parser.cpp"
    break;

  case 19: /* VecType: VECTOR LP FLOATTYPE COMMA VecWidth RP  */
#line 224 "/workspace/src/frontend/parser.y"
                                          {
        (yyvsp[-1].type_spec)->element = TYPE_FLOAT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1909 "/workspace/src/frontend/parser.cpp"
    break;

  case 20: /* VecType: VECTOR LB INTTYPE COMMA VecWidth RB  */
#line 228 "/workspace/src/frontend/parser.y"
                                        {
        (yyvsp[-1].type_spec)->element = TYPE_INT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1918 "/workspace/src/frontend/parser.cpp"
    break;

  case 21: /* VecType: VECTOR LB FLOATTYPE COMMA VecWidth RB  */
#line 232 "/workspace/src/frontend/parser.y"
                                          {
        (yyvsp[-1].type_spec)->element = TYPE_FLOAT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1927 "/workspace/src/frontend/parser.cpp"
    break;

  case 22: /* VecType: VECWIDTH LT INTTYPE GT  */
#line 236 "/workspace/src/frontend/parser.y"
                           {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_INT, (yyvsp[-3].int_val)));
    }
#line 1935 "/workspace/src/frontend/parser.cpp"
    break;

  case 23: /* VecType: VECWIDTH LT FLOATTYPE GT  */
#line 239 "/workspace/src/frontend/parser.y"
                             {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_FLOAT, (yyvsp[-3].int_val)));
    }
#line 1943 "/workspace/src/frontend/parser.cpp"
    break;

  case 24: /* VecType: INTTYPE LT VecWidth GT  */
#line 242 "/workspace/src/frontend/parser.y"
                           {
        (yyvsp[-1].type_spec)->element = TYPE_INT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1952 "/workspace/src/frontend/parser.cpp"
    break;

  case 25: /* VecType: FLOATTYPE LT VecWidth GT  */
#line 246 "/workspace/src/frontend/parser.y"
                             {
        (yyvsp[-1].type_spec)->element = TYPE_FLOAT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1961 "/workspace/src/frontend/parser.cpp"
    break;

  case 26: /* VecType: INTTYPE LB VecWidth RB  */
#line 250 "/workspace/src/frontend/parser.y"
                           {
        (yyvsp[-1].type_spec)->element = TYPE_INT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1970 "/workspace/src/frontend/parser.cpp"
    break;

  case 27: /* VecType: FLOATTYPE LB VecWidth RB  */
#line 254 "/workspace/src/frontend/parser.y"
                             {
        (yyvsp[-1].type_spec)->element = TYPE_FLOAT;
        (yyval.type_spec) = (yyvsp[-1].type_spec);
    }
#line 1979 "/workspace/src/frontend/parser.cpp"
    break;

  case 28: /* VecType: VECTOR LT INTTYPE GT  */
#line 258 "/workspace/src/frontend/parser.y"
                         {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 1987 "/workspace/src/frontend/parser.cpp"
    break;

  case 29: /* VecType: VECTOR LT FLOATTYPE GT  */
#line 261 "/workspace/src/frontend/parser.y"
                           {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 1995 "/workspace/src/frontend/parser.cpp"
    break;

  case 30: /* VecType: VECTOR LP INTTYPE RP  */
#line 264 "/workspace/src/frontend/parser.y"
                         {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 2003 "/workspace/src/frontend/parser.cpp"
    break;

  case 31: /* VecType: VECTOR LP FLOATTYPE RP  */
#line 267 "/workspace/src/frontend/parser.y"
                           {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 2011 "/workspace/src/frontend/parser.cpp"
    break;

  case 32: /* VecType: DYNINTVECTYPE  */
#line 270 "/workspace/src/frontend/parser.y"
                  {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 2019 "/workspace/src/frontend/parser.cpp"
    break;

  case 33: /* VecType: DYNFLOATVECTYPE  */
#line 273 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 2027 "/workspace/src/frontend/parser.cpp"
    break;

  case 34: /* VecType: INTTYPE LT GT  */
#line 276 "/workspace/src/frontend/parser.y"
                  {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 2035 "/workspace/src/frontend/parser.cpp"
    break;

  case 35: /* VecType: FLOATTYPE LT GT  */
#line 279 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 2043 "/workspace/src/frontend/parser.cpp"
    break;

  case 36: /* VecType: INTTYPE LB RB  */
#line 282 "/workspace/src/frontend/parser.y"
                  {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_INT));
    }
#line 2051 "/workspace/src/frontend/parser.cpp"
    break;

  case 37: /* VecType: FLOATTYPE LB RB  */
#line 285 "/workspace/src/frontend/parser.y"
                    {
        (yyval.type_spec) = new TypeSpec(TypeSpec::dynamic(TYPE_FLOAT));
    }
#line 2059 "/workspace/src/frontend/parser.cpp"
    break;

  case 38: /* VecWidth: INT  */
#line 290 "/workspace/src/frontend/parser.y"
        {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, (yyvsp[0].int_val)));
    }
#line 2067 "/workspace/src/frontend/parser.cpp"
    break;

  case 39: /* VecWidth: ID  */
#line 293 "/workspace/src/frontend/parser.y"
       {
        (yyval.type_spec) = new TypeSpec(TypeSpec::fixed(TYPE_VOID, *(yyvsp[0].token)));
        delete (yyvsp[0].token);
    }
#line 2076 "/workspace/src/frontend/parser.cpp"
    break;

  case 40: /* VoidType: VOID  */
#line 300 "/workspace/src/frontend/parser.y"
         {
        (yyval.type_spec) = new TypeSpec(TYPE_VOID);
    }
#line 2084 "/workspace/src/frontend/parser.cpp"
    break;

  case 41: /* DefList: Def  */
#line 306 "/workspace/src/frontend/parser.y"
        {
        (yyval.defList) = make_node<DefListAST>();
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2093 "/workspace/src/frontend/parser.cpp"
    break;

  case 42: /* DefList: DefList COMMA Def  */
#line 310 "/workspace/src/frontend/parser.y"
                      {
        (yyval.defList) = (yyvsp[-2].defList);
        (yyval.defList)->list.push_back(unique_ptr<DefAST>((yyvsp[0].def)));
    }
#line 2102 "/workspace/src/frontend/parser.cpp"
    break;

  case 43: /* Def: ID Arrays ASSIGN InitVal  */
#line 320 "/workspace/src/frontend/parser.y"
                             {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.def)->arrays.swap((yyvsp[-2].arrays)->list);
        (yyval.def)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 2113 "/workspace/src/frontend/parser.cpp"
    break;

  case 44: /* Def: ID ASSIGN Exp  */
#line 326 "/workspace/src/frontend/parser.y"
                  {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.def)->initVal = unique_ptr<InitValAST>(make_node<InitValAST>());
        (yyval.def)->initVal->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2124 "/workspace/src/frontend/parser.cpp"
    break;

  case 45: /* Def: ID Arrays  */
#line 332 "/workspace/src/frontend/parser.y"
              {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[-1].token));
        (yyval.def)->arrays.swap((yyvsp[0].arrays)->list);
    }
#line 2134 "/workspace/src/frontend/parser.cpp"
    break;

  case 46: /* Def: ID  */
#line 337 "/workspace/src/frontend/parser.y"
       {
        (yyval.def) = make_node<DefAST>();
        (yyval.def)->id = unique_ptr<string>((yyvsp[0].token));
    }
#line 2143 "/workspace/src/frontend/parser.cpp"
    break;

  case 47: /* Arrays: LB Exp RB  */
#line 344 "/workspace/src/frontend/parser.y"
              {
        (yyval.arrays) = make_node<ArraysAST>();
        (yyval.arrays)->list.push_back(unique_ptr<AddExpAST>((yyvsp[-1].addExp)));
    }
#line 2152 "/workspace/src/frontend/parser.cpp"
    break;

  case 48: /* Arrays: Arrays LB Exp RB  */
#line 348 "/workspace/src/frontend/parser.y"
                     {
        (yyval.arrays) = (yyvsp[-3].arrays);
        (yyval.arrays)->list.push_back(unique_ptr<AddExpAST>((yyvsp[-1].addExp)));
    }
#line 2161 "/workspace/src/frontend/parser.cpp"
    break;

  case 49: /* Block: LC RC  */
#line 359 "/workspace/src/frontend/parser.y"
          {
        (yyval.block) = make_node<BlockAST>();
    }
#line 2169 "/workspace/src/frontend/parser.cpp"
    break;

  case 50: /* Block: LC BlockItemList RC  */
#line 362 "/workspace/src/frontend/parser.y"
                        {
        (yyval.block) = make_node<BlockAST>();
        (yyval.block)->blockItemList.swap((yyvsp[-1].blockItemList)->list);
    }
#line 2178 "/workspace/src/frontend/parser.cpp"
    break;

  case 51: /* InitVal: LC RC  */
#line 372 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2186 "/workspace/src/frontend/parser.cpp"
    break;

  case 52: /* InitVal: LC InitValList RC  */
#line 375 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->initValList.swap((yyvsp[-1].initValList)->list);
    }
#line 2195 "/workspace/src/frontend/parser.cpp"
    break;

  case 53: /* InitVal: Exp  */
#line 379 "/workspace/src/frontend/parser.y"
        {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->exp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2204 "/workspace/src/frontend/parser.cpp"
    break;

  case 54: /* BraceInitVal: LC RC  */
#line 386 "/workspace/src/frontend/parser.y"
          {
        (yyval.initVal) = make_node<InitValAST>();
    }
#line 2212 "/workspace/src/frontend/parser.cpp"
    break;

  case 55: /* BraceInitVal: LC InitValList RC  */
#line 389 "/workspace/src/frontend/parser.y"
                      {
        (yyval.initVal) = make_node<InitValAST>();
        (yyval.initVal)->initValList.swap((yyvsp[-1].initValList)->list);
    }
#line 2221 "/workspace/src/frontend/parser.cpp"
    break;

  case 56: /* InitValList: InitValList COMMA InitVal  */
#line 396 "/workspace/src/frontend/parser.y"
                            {
    (yyval.initValList) = (yyvsp[-2].initValList);
    (yyval.initValList)->list.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2230 "/workspace/src/frontend/parser.cpp"
    break;

  case 57: /* InitValList: InitVal  */
#line 400 "/workspace/src/frontend/parser.y"
          {
    (yyval.initValList) = make_node<InitValListAST>();
    (yyval.initValList)->list.push_back(unique_ptr<InitValAST>((yyvsp[0].initVal)));
  }
#line 2239 "/workspace/src/frontend/parser.cpp"
    break;

  case 58: /* FuncDef: BType ID LP FuncFParamList RP Block  */
#line 407 "/workspace/src/frontend/parser.y"
                                        {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-5].type_spec);
        delete (yyvsp[-5].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-4].token));
        (yyval.funcDef)->funcFParamList.swap((yyvsp[-2].FuncFParamList)->list);
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2252 "/workspace/src/frontend/parser.cpp"
    break;

  case 59: /* FuncDef: BType ID LP RP Block  */
#line 415 "/workspace/src/frontend/parser.y"
                         {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2264 "/workspace/src/frontend/parser.cpp"
    break;

  case 60: /* FuncDef: VoidType ID LP FuncFParamList RP Block  */
#line 422 "/workspace/src/frontend/parser.y"
                                           {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-5].type_spec);
        delete (yyvsp[-5].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-4].token));
        (yyval.funcDef)->funcFParamList.swap((yyvsp[-2].FuncFParamList)->list);
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2277 "/workspace/src/frontend/parser.cpp"
    break;

  case 61: /* FuncDef: VoidType ID LP RP Block  */
#line 430 "/workspace/src/frontend/parser.y"
                            {
        (yyval.funcDef) = make_node<FuncDefAST>();
        (yyval.funcDef)->funcType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcDef)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcDef)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2289 "/workspace/src/frontend/parser.cpp"
    break;

  case 62: /* FuncFParamList: FuncFParam  */
#line 440 "/workspace/src/frontend/parser.y"
               {
        (yyval.FuncFParamList) = make_node<FuncFParamListAST>();
        (yyval.FuncFParamList)->list.push_back(unique_ptr<FuncFParamAST>((yyvsp[0].funcFParam)));
    }
#line 2298 "/workspace/src/frontend/parser.cpp"
    break;

  case 63: /* FuncFParamList: FuncFParamList COMMA FuncFParam  */
#line 444 "/workspace/src/frontend/parser.y"
                                    {
        (yyval.FuncFParamList) = (yyvsp[-2].FuncFParamList);
        (yyval.FuncFParamList)->list.push_back(unique_ptr<FuncFParamAST>((yyvsp[0].funcFParam)));
    }
#line 2307 "/workspace/src/frontend/parser.cpp"
    break;

  case 64: /* FuncFParam: BType ID  */
#line 451 "/workspace/src/frontend/parser.y"
             {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-1].type_spec);
        delete (yyvsp[-1].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[0].token));
        (yyval.funcFParam)->isArray = false;
    }
#line 2319 "/workspace/src/frontend/parser.cpp"
    break;

  case 65: /* FuncFParam: BType ID LB RB  */
#line 458 "/workspace/src/frontend/parser.y"
                   {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-3].type_spec);
        delete (yyvsp[-3].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.funcFParam)->isArray = true;
    }
#line 2331 "/workspace/src/frontend/parser.cpp"
    break;

  case 66: /* FuncFParam: BType ID LB RB Arrays  */
#line 465 "/workspace/src/frontend/parser.y"
                          {
        (yyval.funcFParam) = make_node<FuncFParamAST>();
        (yyval.funcFParam)->bType = *(yyvsp[-4].type_spec);
        delete (yyvsp[-4].type_spec);
        (yyval.funcFParam)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.funcFParam)->isArray = true;
        (yyval.funcFParam)->arrays.swap((yyvsp[0].arrays)->list);
    }
#line 2344 "/workspace/src/frontend/parser.cpp"
    break;

  case 67: /* BlockItemList: BlockItem  */
#line 476 "/workspace/src/frontend/parser.y"
              {
        (yyval.blockItemList) = make_node<BlockItemListAST>();
        (yyval.blockItemList)->list.push_back(unique_ptr<BlockItemAST>((yyvsp[0].blockItem)));
    }
#line 2353 "/workspace/src/frontend/parser.cpp"
    break;

  case 68: /* BlockItemList: BlockItemList BlockItem  */
#line 480 "/workspace/src/frontend/parser.y"
                            {
        (yyval.blockItemList) = (yyvsp[-1].blockItemList);
        (yyval.blockItemList)->list.push_back(unique_ptr<BlockItemAST>((yyvsp[0].blockItem)));
    }
#line 2362 "/workspace/src/frontend/parser.cpp"
    break;

  case 69: /* BlockItem: Decl  */
#line 487 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>();
        (yyval.blockItem)->decl = unique_ptr<DeclAST>((yyvsp[0].decl));
    }
#line 2371 "/workspace/src/frontend/parser.cpp"
    break;

  case 70: /* BlockItem: Stmt  */
#line 491 "/workspace/src/frontend/parser.y"
         {
        (yyval.blockItem) = make_node<BlockItemAST>();
        (yyval.blockItem)->stmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2380 "/workspace/src/frontend/parser.cpp"
    break;

  case 71: /* Stmt: SEMICOLON  */
#line 498 "/workspace/src/frontend/parser.y"
              {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = SEMI;
    }
#line 2389 "/workspace/src/frontend/parser.cpp"
    break;

  case 72: /* Stmt: LVal ASSIGN Exp SEMICOLON  */
#line 502 "/workspace/src/frontend/parser.y"
                              {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = ASS;
        (yyval.stmt)->lVal = unique_ptr<LValAST>((yyvsp[-3].lVal));
        (yyval.stmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2400 "/workspace/src/frontend/parser.cpp"
    break;

  case 73: /* Stmt: Exp SEMICOLON  */
#line 508 "/workspace/src/frontend/parser.y"
                  {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = EXP;
        (yyval.stmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2410 "/workspace/src/frontend/parser.cpp"
    break;

  case 74: /* Stmt: CONTINUE SEMICOLON  */
#line 513 "/workspace/src/frontend/parser.y"
                       {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = CONT;
    }
#line 2419 "/workspace/src/frontend/parser.cpp"
    break;

  case 75: /* Stmt: BREAK SEMICOLON  */
#line 517 "/workspace/src/frontend/parser.y"
                    {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = BRE;
    }
#line 2428 "/workspace/src/frontend/parser.cpp"
    break;

  case 76: /* Stmt: Block  */
#line 521 "/workspace/src/frontend/parser.y"
          {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = BLK;
        (yyval.stmt)->block = unique_ptr<BlockAST>((yyvsp[0].block));
    }
#line 2438 "/workspace/src/frontend/parser.cpp"
    break;

  case 77: /* Stmt: ReturnStmt  */
#line 526 "/workspace/src/frontend/parser.y"
               {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = RET;
        (yyval.stmt)->returnStmt = unique_ptr<ReturnStmtAST>((yyvsp[0].returnStmt));
    }
#line 2448 "/workspace/src/frontend/parser.cpp"
    break;

  case 78: /* Stmt: SelectStmt  */
#line 531 "/workspace/src/frontend/parser.y"
               {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = SEL;
        (yyval.stmt)->selectStmt = unique_ptr<SelectStmtAST>((yyvsp[0].selectStmt));
    }
#line 2458 "/workspace/src/frontend/parser.cpp"
    break;

  case 79: /* Stmt: IterationStmt  */
#line 536 "/workspace/src/frontend/parser.y"
                  {
        (yyval.stmt) = make_node<StmtAST>();
        (yyval.stmt)->sType = ITER;
        (yyval.stmt)->iterationStmt = unique_ptr<IterationStmtAST>((yyvsp[0].iterationStmt));
    }
#line 2468 "/workspace/src/frontend/parser.cpp"
    break;

  case 80: /* SelectStmt: IF LP Cond RP Stmt  */
#line 544 "/workspace/src/frontend/parser.y"
                                             {
        (yyval.selectStmt) = make_node<SelectStmtAST>();
        (yyval.selectStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.selectStmt)->ifStmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2478 "/workspace/src/frontend/parser.cpp"
    break;

  case 81: /* SelectStmt: IF LP Cond RP Stmt ELSE Stmt  */
#line 549 "/workspace/src/frontend/parser.y"
                                 {
        (yyval.selectStmt) = make_node<SelectStmtAST>();
        (yyval.selectStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-4].lOrExp));
        (yyval.selectStmt)->ifStmt = unique_ptr<StmtAST>((yyvsp[-2].stmt));
        (yyval.selectStmt)->elseStmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2489 "/workspace/src/frontend/parser.cpp"
    break;

  case 82: /* IterationStmt: WHILE LP Cond RP Stmt  */
#line 558 "/workspace/src/frontend/parser.y"
                          {
        (yyval.iterationStmt) = make_node<IterationStmtAST>();
        (yyval.iterationStmt)->cond = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.iterationStmt)->stmt = unique_ptr<StmtAST>((yyvsp[0].stmt));
    }
#line 2499 "/workspace/src/frontend/parser.cpp"
    break;

  case 83: /* ReturnStmt: RETURN Exp SEMICOLON  */
#line 566 "/workspace/src/frontend/parser.y"
                         {
        (yyval.returnStmt) = make_node<ReturnStmtAST>();
        (yyval.returnStmt)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2508 "/workspace/src/frontend/parser.cpp"
    break;

  case 84: /* ReturnStmt: RETURN SEMICOLON  */
#line 570 "/workspace/src/frontend/parser.y"
                     {
        (yyval.returnStmt) = make_node<ReturnStmtAST>();
    }
#line 2516 "/workspace/src/frontend/parser.cpp"
    break;

  case 85: /* Exp: AddExp  */
#line 576 "/workspace/src/frontend/parser.y"
           {
        (yyval.addExp) = (yyvsp[0].addExp);
    }
#line 2524 "/workspace/src/frontend/parser.cpp"
    break;

  case 86: /* Cond: LOrExp  */
#line 582 "/workspace/src/frontend/parser.y"
           {
        (yyval.lOrExp) = (yyvsp[0].lOrExp);
    }
#line 2532 "/workspace/src/frontend/parser.cpp"
    break;

  case 87: /* LVal: ID  */
#line 588 "/workspace/src/frontend/parser.y"
       {
        (yyval.lVal) = make_node<LValAST>();
        (yyval.lVal)->id = unique_ptr<string>((yyvsp[0].token));
    }
#line 2541 "/workspace/src/frontend/parser.cpp"
    break;

  case 88: /* LVal: ID Arrays  */
#line 592 "/workspace/src/frontend/parser.y"
              {
        (yyval.lVal) = make_node<LValAST>();
        (yyval.lVal)->id = unique_ptr<string>((yyvsp[-1].token));
        (yyval.lVal)->arrays.swap((yyvsp[0].arrays)->list);
    }
#line 2551 "/workspace/src/frontend/parser.cpp"
    break;

  case 89: /* PrimaryExp: LP Exp RP  */
#line 600 "/workspace/src/frontend/parser.y"
              {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->exp = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2560 "/workspace/src/frontend/parser.cpp"
    break;

  case 90: /* PrimaryExp: LVal  */
#line 604 "/workspace/src/frontend/parser.y"
         {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->lval = unique_ptr<LValAST>((yyvsp[0].lVal));
    }
#line 2569 "/workspace/src/frontend/parser.cpp"
    break;

  case 91: /* PrimaryExp: Number  */
#line 608 "/workspace/src/frontend/parser.y"
           {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->number = unique_ptr<NumberAST>((yyvsp[0].number));
    }
#line 2578 "/workspace/src/frontend/parser.cpp"
    break;

  case 92: /* PrimaryExp: BraceInitVal  */
#line 612 "/workspace/src/frontend/parser.y"
                 {
        (yyval.primaryExp) = make_node<PrimaryExpAST>();
        (yyval.primaryExp)->initVal = unique_ptr<InitValAST>((yyvsp[0].initVal));
    }
#line 2587 "/workspace/src/frontend/parser.cpp"
    break;

  case 93: /* Number: INT  */
#line 619 "/workspace/src/frontend/parser.y"
        {
        (yyval.number) = make_node<NumberAST>();
        (yyval.number)->isInt = true;
        (yyval.number)->intval = (yyvsp[0].int_val);
    }
#line 2597 "/workspace/src/frontend/parser.cpp"
    break;

  case 94: /* Number: FLOAT  */
#line 624 "/workspace/src/frontend/parser.y"
          {
        (yyval.number) = make_node<NumberAST>();
        (yyval.number)->isInt = false;
        (yyval.number)->floatval = (yyvsp[0].float_val);
    }
#line 2607 "/workspace/src/frontend/parser.cpp"
    break;

  case 95: /* UnaryExp: PrimaryExp  */
#line 634 "/workspace/src/frontend/parser.y"
               {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->primaryExp = unique_ptr<PrimaryExpAST>((yyvsp[0].primaryExp));
    }
#line 2616 "/workspace/src/frontend/parser.cpp"
    break;

  case 96: /* UnaryExp: Call  */
#line 638 "/workspace/src/frontend/parser.y"
         {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->call = unique_ptr<CallAST>((yyvsp[0].call));
    }
#line 2625 "/workspace/src/frontend/parser.cpp"
    break;

  case 97: /* UnaryExp: Call LB Exp RB  */
#line 642 "/workspace/src/frontend/parser.y"
                   {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *base = make_node<UnaryExpAST>();
        base->call = unique_ptr<CallAST>((yyvsp[-3].call));
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>(base);
        (yyval.unaryExp)->subscript = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2637 "/workspace/src/frontend/parser.cpp"
    break;

  case 98: /* UnaryExp: LP Exp RP LB Exp RB  */
#line 649 "/workspace/src/frontend/parser.y"
                        {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        auto *primary = make_node<PrimaryExpAST>();
        primary->exp = unique_ptr<AddExpAST>((yyvsp[-4].addExp));
        auto *base = make_node<UnaryExpAST>();
        base->primaryExp = unique_ptr<PrimaryExpAST>(primary);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>(base);
        (yyval.unaryExp)->subscript = unique_ptr<AddExpAST>((yyvsp[-1].addExp));
    }
#line 2651 "/workspace/src/frontend/parser.cpp"
    break;

  case 99: /* UnaryExp: UnaryOp UnaryExp  */
#line 658 "/workspace/src/frontend/parser.y"
                     {
        (yyval.unaryExp) = make_node<UnaryExpAST>();
        (yyval.unaryExp)->op = (yyvsp[-1].op);
        (yyval.unaryExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2661 "/workspace/src/frontend/parser.cpp"
    break;

  case 100: /* Call: ID LP RP  */
#line 666 "/workspace/src/frontend/parser.y"
             {
        (yyval.call) = make_node<CallAST>();
        (yyval.call)->id = unique_ptr<string>((yyvsp[-2].token));
        (yyval.call)->lineno = (yylsp[-2]).first_line;
    }
#line 2671 "/workspace/src/frontend/parser.cpp"
    break;

  case 101: /* Call: ID LP FuncCParamList RP  */
#line 671 "/workspace/src/frontend/parser.y"
                            {
        (yyval.call) = make_node<CallAST>();
        (yyval.call)->id = unique_ptr<string>((yyvsp[-3].token));
        (yyval.call)->funcCParamList.swap((yyvsp[-1].funcCParamList)->list);
        (yyval.call)->lineno = (yylsp[-3]).first_line;
    }
#line 2682 "/workspace/src/frontend/parser.cpp"
    break;

  case 102: /* UnaryOp: ADD  */
#line 680 "/workspace/src/frontend/parser.y"
        {
        (yyval.op) = UOP_ADD;
    }
#line 2690 "/workspace/src/frontend/parser.cpp"
    break;

  case 103: /* UnaryOp: MINUS  */
#line 683 "/workspace/src/frontend/parser.y"
          {
        (yyval.op) = UOP_MINUS;
    }
#line 2698 "/workspace/src/frontend/parser.cpp"
    break;

  case 104: /* UnaryOp: NOT  */
#line 686 "/workspace/src/frontend/parser.y"
        {
        (yyval.op) = UOP_NOT;
    }
#line 2706 "/workspace/src/frontend/parser.cpp"
    break;

  case 105: /* FuncCParamList: Exp  */
#line 692 "/workspace/src/frontend/parser.y"
        {
        (yyval.funcCParamList) = make_node<FuncCParamListAST>();
        (yyval.funcCParamList)->list.push_back(unique_ptr<AddExpAST>((yyvsp[0].addExp)));
    }
#line 2715 "/workspace/src/frontend/parser.cpp"
    break;

  case 106: /* FuncCParamList: FuncCParamList COMMA Exp  */
#line 696 "/workspace/src/frontend/parser.y"
                             {
        (yyval.funcCParamList) = (FuncCParamListAST*) (yyvsp[-2].funcCParamList);
        (yyval.funcCParamList)->list.push_back(unique_ptr<AddExpAST>((yyvsp[0].addExp)));
    }
#line 2724 "/workspace/src/frontend/parser.cpp"
    break;

  case 107: /* MulExp: UnaryExp  */
#line 703 "/workspace/src/frontend/parser.y"
             {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2733 "/workspace/src/frontend/parser.cpp"
    break;

  case 108: /* MulExp: MulExp MUL UnaryExp  */
#line 707 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MUL;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2744 "/workspace/src/frontend/parser.cpp"
    break;

  case 109: /* MulExp: MulExp DIV UnaryExp  */
#line 713 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_DIV;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2755 "/workspace/src/frontend/parser.cpp"
    break;

  case 110: /* MulExp: MulExp MOD UnaryExp  */
#line 719 "/workspace/src/frontend/parser.y"
                        {
        (yyval.mulExp) = make_node<MulExpAST>();
        (yyval.mulExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[-2].mulExp));
        (yyval.mulExp)->op = MOP_MOD;
        (yyval.mulExp)->unaryExp = unique_ptr<UnaryExpAST>((yyvsp[0].unaryExp));
    }
#line 2766 "/workspace/src/frontend/parser.cpp"
    break;

  case 111: /* AddExp: MulExp  */
#line 728 "/workspace/src/frontend/parser.y"
           {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2775 "/workspace/src/frontend/parser.cpp"
    break;

  case 112: /* AddExp: AddExp ADD MulExp  */
#line 732 "/workspace/src/frontend/parser.y"
                      {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_ADD;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2786 "/workspace/src/frontend/parser.cpp"
    break;

  case 113: /* AddExp: AddExp MINUS MulExp  */
#line 738 "/workspace/src/frontend/parser.y"
                        {
        (yyval.addExp) = make_node<AddExpAST>();
        (yyval.addExp)->addExp = unique_ptr<AddExpAST>((yyvsp[-2].addExp));
        (yyval.addExp)->op = AOP_MINUS;
        (yyval.addExp)->mulExp = unique_ptr<MulExpAST>((yyvsp[0].mulExp));
    }
#line 2797 "/workspace/src/frontend/parser.cpp"
    break;

  case 114: /* RelExp: AddExp  */
#line 747 "/workspace/src/frontend/parser.y"
           {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2806 "/workspace/src/frontend/parser.cpp"
    break;

  case 115: /* RelExp: RelExp GTE AddExp  */
#line 751 "/workspace/src/frontend/parser.y"
                      {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_GTE;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2817 "/workspace/src/frontend/parser.cpp"
    break;

  case 116: /* RelExp: RelExp LTE AddExp  */
#line 757 "/workspace/src/frontend/parser.y"
                      {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_LTE;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2828 "/workspace/src/frontend/parser.cpp"
    break;

  case 117: /* RelExp: RelExp GT AddExp  */
#line 763 "/workspace/src/frontend/parser.y"
                     {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_GT;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2839 "/workspace/src/frontend/parser.cpp"
    break;

  case 118: /* RelExp: RelExp LT AddExp  */
#line 769 "/workspace/src/frontend/parser.y"
                     {
        (yyval.relExp) = make_node<RelExpAST>();
        (yyval.relExp)->relExp = unique_ptr<RelExpAST>((yyvsp[-2].relExp));
        (yyval.relExp)->op = ROP_LT;
        (yyval.relExp)->addExp = unique_ptr<AddExpAST>((yyvsp[0].addExp));
    }
#line 2850 "/workspace/src/frontend/parser.cpp"
    break;

  case 119: /* EqExp: RelExp  */
#line 778 "/workspace/src/frontend/parser.y"
           {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 2859 "/workspace/src/frontend/parser.cpp"
    break;

  case 120: /* EqExp: EqExp EQ RelExp  */
#line 782 "/workspace/src/frontend/parser.y"
                    {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[-2].eqExp));
        (yyval.eqExp)->op = EOP_EQ;
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 2870 "/workspace/src/frontend/parser.cpp"
    break;

  case 121: /* EqExp: EqExp NEQ RelExp  */
#line 788 "/workspace/src/frontend/parser.y"
                     {
        (yyval.eqExp) = make_node<EqExpAST>();
        (yyval.eqExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[-2].eqExp));
        (yyval.eqExp)->op = EOP_NEQ;
        (yyval.eqExp)->relExp = unique_ptr<RelExpAST>((yyvsp[0].relExp));
    }
#line 2881 "/workspace/src/frontend/parser.cpp"
    break;

  case 122: /* LAndExp: EqExp  */
#line 797 "/workspace/src/frontend/parser.y"
          {
        (yyval.lAndExp) = make_node<LAndExpAST>();
        (yyval.lAndExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[0].eqExp));
    }
#line 2890 "/workspace/src/frontend/parser.cpp"
    break;

  case 123: /* LAndExp: LAndExp AND EqExp  */
#line 801 "/workspace/src/frontend/parser.y"
                      {
        (yyval.lAndExp) = make_node<LAndExpAST>();
        (yyval.lAndExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[-2].lAndExp));
        (yyval.lAndExp)->eqExp = unique_ptr<EqExpAST>((yyvsp[0].eqExp));
    }
#line 2900 "/workspace/src/frontend/parser.cpp"
    break;

  case 124: /* LOrExp: LAndExp  */
#line 809 "/workspace/src/frontend/parser.y"
            {
        (yyval.lOrExp) = make_node<LOrExpAST>();
        (yyval.lOrExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[0].lAndExp));
    }
#line 2909 "/workspace/src/frontend/parser.cpp"
    break;

  case 125: /* LOrExp: LOrExp OR LAndExp  */
#line 813 "/workspace/src/frontend/parser.y"
                      {
        (yyval.lOrExp) = make_node<LOrExpAST>();
        (yyval.lOrExp)->lOrExp = unique_ptr<LOrExpAST>((yyvsp[-2].lOrExp));
        (yyval.lOrExp)->lAndExp = unique_ptr<LAndExpAST>((yyvsp[0].lAndExp));
    }
#line 2919 "/workspace/src/frontend/parser.cpp"
    break;


#line 2923 "/workspace/src/frontend/parser.cpp"

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

#line 818 "/workspace/src/frontend/parser.y"


void initFileName(const char *name) {
    filename = name ? name : "";
}

void yyerror(const char *fmt) {
    std::cerr << filename << ':' << yylloc.first_line << ' ' << fmt << std::endl;
}
