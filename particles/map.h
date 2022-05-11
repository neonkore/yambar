#pragma once

enum map_op {
    MAP_OP_EQ,
    MAP_OP_NE,
    MAP_OP_LE,
    MAP_OP_LT,
    MAP_OP_GE,
    MAP_OP_GT,
    MAP_OP_SELF,
    MAP_OP_NOT,

    MAP_OP_AND,
    MAP_OP_OR,
};

struct map_condition {
    union {
        char *tag;
        struct map_condition *cond1;
    };
    enum map_op op;
    union {
        char *value;
        struct map_condition *cond2;
    };
};

void free_map_condition(struct map_condition *c);

typedef struct yy_buffer_state* YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_string(const char *str);
int yyparse();
void yy_delete_buffer(YY_BUFFER_STATE buffer);

extern struct map_condition *MAP_CONDITION_PARSE_RESULT;
extern char *MAP_PARSER_ERROR_MSG;
