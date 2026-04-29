%option noyywrap
%option yylineno

%{
#include "parser.hpp"
#include <string>
#include <cstdio>

int yycolumn = 1;
/* Update yycolumn whenever a token is matched */
#define YY_USER_ACTION \
  yylloc.first_line = yylloc.last_line = yylineno; \
  yylloc.first_column = yycolumn; \
  yylloc.last_column = yycolumn + yyleng - 1; \
  yycolumn += yyleng;

#ifdef DEBUG_LEXER
#define LEXER_DEBUG_TOKEN(name) \
  std::fprintf(stderr, "[Lexer] %-12s : %s (line %d, col %d-%d)\n", name, yytext, yylloc.first_line, yylloc.first_column, yylloc.last_column)
#else
#define LEXER_DEBUG_TOKEN(name) do {} while (0)
#endif
%}

INT_LIT [1-9][0-9]*|0[xX][0-9A-Fa-f]+|0[0-7]*|0

FLOAT_SUFFIX  [fFlL]
DEC_EXP       [Ee][-+]?[0-9]+
DEC_FLOAT_LIT (([0-9]*\.[0-9]+|[0-9]+\.){DEC_EXP}?|[0-9]+{DEC_EXP}){FLOAT_SUFFIX}?
HEX_EXP       [Pp][-+]?[0-9]+
HEX_FLOAT_LIT (0[xX]([a-fA-F0-9]*\.[a-fA-F0-9]+|[a-fA-F0-9]+\.?){HEX_EXP}){FLOAT_SUFFIX}?
FLOAT_LIT     {DEC_FLOAT_LIT}|{HEX_FLOAT_LIT}

Identifier [A-Za-z_][A-Za-z0-9_]*

MultilineComment "/*"([^*]|[*][^/])*"*/"
SingleLineComment "//"[^\n]*

%%

{MultilineComment} {}
{SingleLineComment} {}

{INT_LIT}       { yylval.int_val = strtol(yytext, nullptr, 0); LEXER_DEBUG_TOKEN("INT"); return INT; }
{FLOAT_LIT}     { yylval.float_val = strtof(yytext, nullptr); LEXER_DEBUG_TOKEN("FLOAT"); return FLOAT; }

"int"           { LEXER_DEBUG_TOKEN("INTTYPE"); return INTTYPE; }
"float"         { LEXER_DEBUG_TOKEN("FLOATTYPE"); return FLOATTYPE; }
"void"          { LEXER_DEBUG_TOKEN("VOID"); return VOID; }
"const"         { LEXER_DEBUG_TOKEN("CONST"); return CONST; }
"return"        { LEXER_DEBUG_TOKEN("RETURN"); return RETURN; }
"if"            { LEXER_DEBUG_TOKEN("IF"); return IF; }
"else"          { LEXER_DEBUG_TOKEN("ELSE"); return ELSE; }
"while"         { LEXER_DEBUG_TOKEN("WHILE"); return WHILE; }
"break"         { LEXER_DEBUG_TOKEN("BREAK"); return BREAK; }
"continue"      { LEXER_DEBUG_TOKEN("CONTINUE"); return CONTINUE; }

{Identifier}    { yylval.token = new string(yytext); LEXER_DEBUG_TOKEN("ID"); return ID; }

">="            { LEXER_DEBUG_TOKEN("GTE"); return GTE; }
"<="            { LEXER_DEBUG_TOKEN("LTE"); return LTE; }
">"             { LEXER_DEBUG_TOKEN("GT"); return GT; }
"<"             { LEXER_DEBUG_TOKEN("LT"); return LT; }
"=="            { LEXER_DEBUG_TOKEN("EQ"); return EQ; }
"!="            { LEXER_DEBUG_TOKEN("NEQ"); return NEQ; }

"("             { LEXER_DEBUG_TOKEN("LP"); return LP; }
")"             { LEXER_DEBUG_TOKEN("RP"); return RP; }
"["             { LEXER_DEBUG_TOKEN("LB"); return LB; }
"]"             { LEXER_DEBUG_TOKEN("RB"); return RB; }
"{"             { LEXER_DEBUG_TOKEN("LC"); return LC; }
"}"             { LEXER_DEBUG_TOKEN("RC"); return RC; }
","             { LEXER_DEBUG_TOKEN("COMMA"); return COMMA; }
";"             { LEXER_DEBUG_TOKEN("SEMICOLON"); return SEMICOLON; }

"!"             { LEXER_DEBUG_TOKEN("NOT"); return NOT; }
"="             { LEXER_DEBUG_TOKEN("ASSIGN"); return ASSIGN; }
"-"             { LEXER_DEBUG_TOKEN("MINUS"); return MINUS; }
"+"             { LEXER_DEBUG_TOKEN("ADD"); return ADD; }
"*"             { LEXER_DEBUG_TOKEN("MUL"); return MUL; }
"/"             { LEXER_DEBUG_TOKEN("DIV"); return DIV; }
"%"             { LEXER_DEBUG_TOKEN("MOD"); return MOD; }
"&&"            { LEXER_DEBUG_TOKEN("AND"); return AND; }
"||"            { LEXER_DEBUG_TOKEN("OR"); return OR; }

[\n]            { yycolumn=1; }
[ \r\t]         { /* Ignore useless whitespaces */ }
.               { printf("Error : Mysterious character \"%s\"\n\t at Line %d\n", yytext, yylineno);}

%%
