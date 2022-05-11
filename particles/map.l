%{
#include <string.h>
#include "map.h"
#include "map.tab.h"
%}

%option warn nodefault nounput noinput noyywrap

%%
[[:alnum:]_-]+          yylval.str = strdup(yytext); return WORD;
\".*\"                  yylval.str = strndup(yytext + 1, strlen(yytext) - 2); return STRING;
==                      yylval.op = MAP_OP_EQ; return CMP_OP;
!=                      yylval.op = MAP_OP_NE; return CMP_OP;
\<=                     yylval.op = MAP_OP_LE; return CMP_OP;
\<                      yylval.op = MAP_OP_LT; return CMP_OP;
>=                      yylval.op = MAP_OP_GE; return CMP_OP;
>                       yylval.op = MAP_OP_GT; return CMP_OP;
&&                      yylval.op = MAP_OP_AND; return BOOL_OP;
\|\|                    yylval.op = MAP_OP_OR; return BOOL_OP;
~                       return NOT;
\(                      return L_PAR;
\)                      return R_PAR;
[ \t\n]                 ;
.                       yylval.str = strdup(yytext); return STRING;
%%
