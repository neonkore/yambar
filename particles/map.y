%{
#include <stdio.h>
#include <string.h>

#include "map.h"

struct map_condition *MAP_CONDITION_PARSE_RESULT;
char *MAP_PARSER_ERROR_MSG;

static const int NUM_TOKENS = 7;
int yylex();
void yyerror(const char *str);
%}

%define parse.lac full
%define parse.error custom

%union {
    char *str;
    struct map_condition *condition;
    enum map_op op;
}

%token WORD STRING CMP_OP L_PAR R_PAR
%left BOOL_OP
%precedence NOT

%destructor { free_map_condition($<condition>$); } condition
%destructor { free($<str>$); } WORD
%destructor { free($<str>$); } STRING

%%
result: condition { MAP_CONDITION_PARSE_RESULT = $<condition>1; };

condition:
         WORD {
         $<condition>$ = malloc(sizeof(struct map_condition));
         $<condition>$->tag = $<str>1; 
         $<condition>$->op = MAP_OP_SELF;
         }
         |
         WORD CMP_OP WORD {
         $<condition>$ = malloc(sizeof(struct map_condition));
         $<condition>$->tag = $<str>1; 
         $<condition>$->op = $<op>2;
         $<condition>$->value = $<str>3; 
         }
         |
         WORD CMP_OP STRING {
         $<condition>$ = malloc(sizeof(struct map_condition));
         $<condition>$->tag = $<str>1; 
         $<condition>$->op = $<op>2;
         $<condition>$->value = $<str>3; 
         }
         |
         L_PAR condition R_PAR { $<condition>$ = $<condition>2; }
         |
         NOT condition { 
         $<condition>$ = malloc(sizeof(struct map_condition));
         $<condition>$->cond1 = $<condition>2;
         $<condition>$->op = MAP_OP_NOT;
         }
         |
         condition BOOL_OP condition {
         $<condition>$ = malloc(sizeof(struct map_condition));
         $<condition>$->cond1 = $<condition>1;
         $<condition>$->op = $<op>2;
         $<condition>$->cond2 = $<condition>3;
         }
         ;
%%

void yyerror(const char *str)
{
    fprintf(stderr, "error: %s\n", str);
}

static char const*
token_to_str(yysymbol_kind_t tkn)
{
    switch (tkn) {
    case YYSYMBOL_CMP_OP: return "==, !=, <=, <, >=, >";
    case YYSYMBOL_BOOL_OP: return "||, &&";
    case YYSYMBOL_L_PAR: return "(";
    case YYSYMBOL_R_PAR: return ")";
    case YYSYMBOL_NOT: return "~";
    default: return yysymbol_name(tkn);
    }
}

static int
yyreport_syntax_error (const yypcontext_t *ctx)
{
    int res = 0;
    char *errmsg = malloc(1024);
    errmsg[0] = '\0';

    // Report the tokens expected at this point.
    yysymbol_kind_t expected[NUM_TOKENS];
    int n = yypcontext_expected_tokens(ctx, expected, NUM_TOKENS);
    if (n < 0)
        res = n; // Forward errors to yyparse.
    else {
        for (int i = 0; i < n; ++i) {
            strcat(errmsg, i == 0 ? "expected [" : ", ");
            strcat(errmsg, token_to_str(expected[i]));
        }
        strcat(errmsg, "]");
    }

    // Report the unexpected token.
    yysymbol_kind_t lookahead = yypcontext_token(ctx);
    if (lookahead != YYSYMBOL_YYEMPTY) {
        strcat(errmsg, ", found ");
        if (!(lookahead == YYSYMBOL_STRING || lookahead == YYSYMBOL_WORD))
            strcat(errmsg, yysymbol_name(lookahead));
        else if (yylval.str != NULL)
            strcat(errmsg, yylval.str);
        else
            strcat(errmsg, "nothing");
    }

    MAP_PARSER_ERROR_MSG = errmsg;
    return res;
}
