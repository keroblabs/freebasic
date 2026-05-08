#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "parser.h"
#include "symtable.h"
#include "value.h"
#include "ast.h"
#include "callframe.h"
#include "udt.h"
#include "fileio.h"
#include "platform.h"

#define MAX_GOSUB_STACK 256
#define MAX_FOR_STACK   64

/* FOR loop tracker */
typedef struct {
    int         stmt_index;   /* Index of the AST_FOR statement */
    double      limit;
    double      step;
    char        var_name[42];
} ForFrame;

/* Interpreter state */
typedef struct Interpreter {
    Program*     prog;
    int          pc;           /* Program counter (statement index) */
    int          running;
    Scope*       global_scope;
    Scope*       current_scope;

    /* GOSUB stack */
    int          gosub_stack[MAX_GOSUB_STACK];
    int          gosub_sp;

    /* FOR stack */
    ForFrame     for_stack[MAX_FOR_STACK];
    int          for_sp;

    /* CALL stack (Phase 4 proper call frames) */
    CallStack    call_stack;

    /* DATA pointer */
    int          data_ptr;

    /* Error handling */
    int          on_error_target;  /* -1 = no handler */
    int          error_pc;
    int          in_error_handler;
    int          err_code;          /* Most recent error code (ERR) */
    int          err_line;          /* Line where error occurred (ERL) */

    /* TRON/TROFF */
    int          trace_on;

    /* EXIT flag */
    int          exit_for;
    int          exit_do;
    int          exit_while;
    int          exit_sub;
    int          exit_function;
    int          exit_procedure;  /* Generic flag for EXIT SUB/FUNCTION/DEF */

    /* File I/O */
    FBFileTable  file_table;

    /* RND seed */
    unsigned int rnd_seed;
    double       last_rnd;

    /* OPTION BASE */
    int          option_base;

    /* Console state */
    int          print_col;
    int          current_fg;
    int          current_bg;
    int          screen_width;
    int          screen_height;
    int          scroll_top;
    int          scroll_bottom;

    /* DEF FN definitions */
    struct {
        char      name[42];
        ASTNode*  node;
    }*           def_fns;
    int          def_fn_count;
    int          def_fn_cap;

    /* UDT Registry */
    UDTRegistry  udt_registry;

    /* Phase 7: System API */
    const FBSysOps* sys_ops;       /* Platform abstraction vtable */
    char*        command_line;     /* COMMAND$ value (uppercased argv) */
    int          def_seg;          /* DEF SEG segment (stub) */

} Interpreter;

void interp_init(Interpreter* interp, Program* prog);
void interp_set_command_line(Interpreter* interp, int argc, char** argv);
void interp_run(Interpreter* interp);
void interp_free(Interpreter* interp);
FBValue interp_eval(Interpreter* interp, ASTNode* node);

#endif
