#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "ast.h"
#include "value.h"

typedef struct {
    ASTNode**   statements;
    int         stmt_count;
    int         stmt_cap;

    /* Label -> statement index lookup */
    struct {
        char    name[42];
        int     stmt_index;
    }*          labels;
    int         label_count;
    int         label_cap;

    /* Line number -> statement index lookup */
    struct {
        int32_t lineno;
        int     stmt_index;
    }*          line_map;
    int         linemap_count;
    int         linemap_cap;

    /* DATA pool */
    FBValue*    data_pool;
    int         data_count;
    int         data_cap;

    /* DATA label positions: maps label/lineno to data_pool index */
    struct {
        char    label[42];
        int     lineno;
        int     data_index;
    }*          data_labels;
    int         data_label_count;
    int         data_label_cap;

    /* Procedure table (Phase 4) */
    struct {
        char    name[42];
        int     is_function;
        int     stmt_index;     /* Index of the SUB_DEF/FUNCTION_DEF node */
    }*          procedures;
    int         proc_count;
    int         proc_cap;

} Program;

int  parser_parse(const Token* tokens, int token_count, Program* prog);
void program_free(Program* prog);
int  program_find_label(const Program* prog, const char* name);
int  program_find_lineno(const Program* prog, int32_t lineno);
int  program_find_proc(const Program* prog, const char* name);

#endif
