#ifndef AST_H
#define AST_H

#include "value.h"
#include "token.h"

typedef enum {
    /* Expressions */
    AST_LITERAL,
    AST_VARIABLE,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_FUNC_CALL,
    AST_PAREN,
    AST_ARRAY_ACCESS,
    AST_UDT_MEMBER,

    /* Statements */
    AST_PRINT,
    AST_LET,
    AST_DIM,
    AST_CONST_DECL,
    AST_IF,
    AST_FOR,
    AST_WHILE,
    AST_DO_LOOP,
    AST_SELECT_CASE,
    AST_GOTO,
    AST_GOSUB,
    AST_RETURN,
    AST_END,
    AST_STOP,
    AST_SYSTEM,
    AST_REM,
    AST_LABEL_DEF,
    AST_DEFTYPE,
    AST_EXIT,

    /* I/O */
    AST_INPUT,
    AST_LINE_INPUT,
    AST_WRITE_STMT,
    AST_CLS,
    AST_LOCATE,
    AST_COLOR,
    AST_BEEP,
    AST_VIEW_PRINT,
    AST_WIDTH_STMT,
    AST_PRINT_USING,
    AST_DATA,
    AST_READ,
    AST_RESTORE,

    /* Arrays */
    AST_REDIM,
    AST_ERASE,
    AST_SWAP,
    AST_OPTION_BASE,

    /* UDT */
    AST_TYPE_DEF,

    /* Procedures */
    AST_SUB_DEF,
    AST_FUNCTION_DEF,
    AST_CALL,
    AST_DECLARE,
    AST_DEF_FN,
    AST_SHARED_STMT,
    AST_STATIC_STMT,
    AST_ON_GOTO,
    AST_ON_GOSUB,

    /* File I/O */
    AST_OPEN,
    AST_CLOSE,
    AST_PRINT_FILE,
    AST_WRITE_FILE,
    AST_INPUT_FILE,
    AST_LINE_INPUT_FILE,
    AST_GET_FILE,
    AST_PUT_FILE,
    AST_SEEK_STMT,
    AST_LOCK_STMT,
    AST_UNLOCK_STMT,
    AST_NAME_STMT,
    AST_KILL_STMT,
    AST_FILES_STMT,
    AST_FIELD_STMT,
    AST_LSET,
    AST_RSET,
    AST_RESET,

    /* Error handling */
    AST_ON_ERROR,
    AST_RESUME,
    AST_ERROR_STMT,
    AST_TRON,
    AST_TROFF,

    /* System */
    AST_SHELL,
    AST_ENVIRON_STMT,
    AST_CHDIR,
    AST_MKDIR,
    AST_RMDIR,
    AST_CLEAR,
    AST_LPRINT,

    /* Graphics */
    AST_SCREEN,
    AST_PSET,
    AST_PRESET,
    AST_LINE_GFX,
    AST_CIRCLE,
    AST_PAINT,
    AST_DRAW,
    AST_GET_GFX,
    AST_PUT_GFX,
    AST_PALETTE,
    AST_PCOPY,
    AST_WINDOW_STMT,
    AST_VIEW_GFX,

    /* Sound */
    AST_SOUND,
    AST_PLAY,

    /* Events */
    AST_ON_KEY,
    AST_ON_TIMER,
    AST_KEY_CTRL,
    AST_TIMER_CTRL,

    /* Advanced */
    AST_SLEEP,
    AST_CHAIN,
    AST_RUN,
    AST_COMMON,
    AST_RANDOMIZE,

    /* Misc */
    AST_POKE,
    AST_OUT_STMT,

    AST_NODE_COUNT
} ASTKind;

typedef struct ASTNode {
    ASTKind kind;
    int     line;

    union {
        /* AST_LITERAL */
        struct { FBValue value; } literal;

        /* AST_VARIABLE */
        struct { char name[42]; FBType type_hint; } variable;

        /* AST_BINARY_OP */
        struct {
            TokenKind       op;
            struct ASTNode* left;
            struct ASTNode* right;
        } binop;

        /* AST_UNARY_OP */
        struct {
            TokenKind       op;
            struct ASTNode* operand;
        } unop;

        /* AST_FUNC_CALL */
        struct {
            char            name[42];
            struct ASTNode** args;
            int             arg_count;
        } func_call;

        /* AST_PAREN */
        struct { struct ASTNode* expr; } paren;

        /* AST_ARRAY_ACCESS */
        struct {
            char            name[42];
            struct ASTNode** subscripts;
            int             nsubs;
        } array_access;

        /* AST_UDT_MEMBER */
        struct {
            struct ASTNode* base;
            char            field[42];
        } udt_member;

        /* AST_PRINT */
        struct {
            struct ASTNode** items;
            int             item_count;
            int*            separators;  /* TOK_SEMICOLON, TOK_COMMA, or 0 */
            int             trailing_sep;
            int             filenum;     /* 0 for screen, >0 for file */
        } print;

        /* AST_LET */
        struct {
            struct ASTNode* target;
            struct ASTNode* expr;
        } let;

        /* AST_IF (block form) */
        struct {
            struct ASTNode*   condition;
            struct ASTNode**  then_body;
            int               then_count;
            struct ASTNode**  elseif_cond;
            struct ASTNode*** elseif_body;
            int*              elseif_count;
            int               elseif_n;
            struct ASTNode**  else_body;
            int               else_count;
        } if_block;

        /* AST_FOR */
        struct {
            struct ASTNode* var;
            struct ASTNode* start;
            struct ASTNode* end;
            struct ASTNode* step;
            struct ASTNode** body;
            int             body_count;
        } for_loop;

        /* AST_WHILE / AST_DO_LOOP */
        struct {
            struct ASTNode*  condition;
            struct ASTNode** body;
            int              body_count;
            int              is_until;
            int              is_post;
        } loop;

        /* AST_GOTO / AST_GOSUB */
        struct {
            char   label[42];
            int    lineno;    /* -1 if using label */
        } jump;

        /* AST_DIM / AST_REDIM */
        struct {
            char    name[42];
            FBType  type;
            int     is_shared;
            int     is_static;
            int     is_dynamic;
            struct ASTNode** bounds;  /* pairs: lower, upper for each dim */
            int     ndims;
            char    as_type_name[42]; /* For AS typename (UDTs) */
        } dim;

        /* AST_CONST_DECL */
        struct {
            char    name[42];
            struct ASTNode* value_expr;
        } const_decl;

        /* AST_DEFTYPE */
        struct {
            FBType  type;
            char    range_start;
            char    range_end;
        } deftype;

        /* AST_SELECT_CASE */
        struct {
            struct ASTNode*   test_expr;
            struct {
                struct ASTNode** values;  /* Case values/expressions */
                int*             kinds;   /* 0=value, 1=range(TO), 2=IS relop */
                TokenKind*       relops;  /* relational op for IS cases */
                struct ASTNode** range_ends; /* end of TO range */
                int              value_count;
                struct ASTNode** body;
                int              body_count;
            }*                cases;
            int               case_count;
            struct ASTNode**  else_body;
            int               else_count;
        } select_case;

        /* AST_EXIT */
        struct { int exit_what; /* 0=FOR, 1=DO, 2=WHILE, 3=SUB, 4=FUNCTION, 5=DEF */ } exit_stmt;

        /* AST_INPUT / AST_LINE_INPUT */
        struct {
            struct ASTNode*  prompt;
            struct ASTNode** vars;
            int              var_count;
            int              no_newline;  /* semicolon after prompt */
            int              filenum;
        } input;

        /* AST_WRITE_STMT */
        struct {
            struct ASTNode** items;
            int              item_count;
            int              filenum;
        } write_stmt;

        /* AST_LOCATE */
        struct {
            struct ASTNode* row;
            struct ASTNode* col;
        } locate;

        /* AST_COLOR */
        struct {
            struct ASTNode* fg;
            struct ASTNode* bg;
        } color;

        /* AST_VIEW_PRINT */
        struct {
            struct ASTNode* top;
            struct ASTNode* bottom;
        } view_print;

        /* AST_WIDTH_STMT */
        struct {
            struct ASTNode* cols;
            struct ASTNode* rows;
        } width_stmt;

        /* AST_DATA */
        struct {
            FBValue*  values;
            int       value_count;
        } data;

        /* AST_READ */
        struct {
            struct ASTNode** vars;
            int              var_count;
        } read_stmt;

        /* AST_RESTORE */
        struct {
            char   label[42];
            int    lineno;
        } restore;

        /* AST_PRINT_USING */
        struct {
            struct ASTNode*  format_expr;
            struct ASTNode** items;
            int              item_count;
            int              filenum;
        } print_using;

        /* AST_SWAP */
        struct {
            struct ASTNode* a;
            struct ASTNode* b;
        } swap;

        /* AST_OPTION_BASE */
        struct { int base; } option_base;

        /* AST_TYPE_DEF */
        struct {
            char   name[42];
            struct {
                char   field_name[42];
                FBType type;
                int    string_len; /* For STRING * n */
                int    is_array;
                struct ASTNode** bounds;
                int    ndims;
            }*     fields;
            int    field_count;
        } type_def;

        /* AST_SUB_DEF / AST_FUNCTION_DEF */
        struct {
            char    name[42];
            struct {
                char   pname[42];
                FBType ptype;
                int    is_byval;
                int    is_array;
            }*      params;
            int     param_count;
            struct ASTNode** body;
            int     body_count;
            int     is_static;
            FBType  return_type; /* For FUNCTION */
        } proc_def;

        /* AST_CALL */
        struct {
            char            name[42];
            struct ASTNode** args;
            int             arg_count;
        } call;

        /* AST_DECLARE */
        struct {
            int     is_function;
            char    name[42];
            struct {
                char   pname[42];
                FBType ptype;
                int    is_byval;
                int    is_array;
            }*      params;
            int     param_count;
            FBType  return_type;
        } declare;

        /* AST_DEF_FN */
        struct {
            char    name[42];
            struct {
                char   pname[42];
                FBType ptype;
            }*      params;
            int     param_count;
            struct ASTNode* body_expr;    /* Single-line */
            struct ASTNode** body;        /* Multi-line */
            int     body_count;
        } def_fn;

        /* AST_ON_GOTO / AST_ON_GOSUB */
        struct {
            struct ASTNode* expr;
            char   (*labels)[42];
            int*   linenos;
            int    label_count;
        } on_branch;

        /* AST_SHARED_STMT / AST_STATIC_STMT */
        struct {
            char   (*names)[42];
            FBType* types;
            int     name_count;
        } shared_stmt;

        /* AST_OPEN */
        struct {
            struct ASTNode* filename;
            struct ASTNode* filenum;
            int             mode;   /* 0=INPUT, 1=OUTPUT, 2=APPEND, 3=RANDOM, 4=BINARY */
            struct ASTNode* reclen;
            int             access_mode;
            int             lock_mode;
        } open;

        /* AST_CLOSE */
        struct {
            int*  filenums;
            int   filenum_count;
            int   close_all;
        } close;

        /* AST_GET_FILE / AST_PUT_FILE */
        struct {
            struct ASTNode* filenum;
            struct ASTNode* recnum;
            struct ASTNode* var;
        } file_io;

        /* AST_SEEK_STMT */
        struct {
            struct ASTNode* filenum;
            struct ASTNode* position;
        } seek;

        /* AST_FIELD_STMT */
        struct {
            struct ASTNode* filenum;
            struct {
                int    width;
                char   var_name[42];
            }*     fields;
            int    field_count;
        } field;

        /* AST_LSET / AST_RSET */
        struct {
            struct ASTNode* target;
            struct ASTNode* expr;
        } lrset;

        /* AST_NAME_STMT */
        struct {
            struct ASTNode* old_name;
            struct ASTNode* new_name;
        } name_stmt;

        /* AST_KILL_STMT / AST_SHELL / AST_CHDIR / AST_MKDIR / AST_RMDIR */
        struct {
            struct ASTNode* arg;
        } single_arg;

        /* AST_ON_ERROR */
        struct {
            char   label[42];
            int    lineno;
            int    disable;  /* ON ERROR GOTO 0 */
        } on_error;

        /* AST_RESUME */
        struct {
            int    resume_type; /* 0=RESUME, 1=RESUME NEXT, 2=RESUME label */
            char   label[42];
            int    lineno;
        } resume;

        /* AST_ERROR_STMT */
        struct { struct ASTNode* code; } error_stmt;

        /* AST_ENVIRON_STMT */
        struct { struct ASTNode* expr; } environ_stmt;

        /* AST_SCREEN */
        struct {
            struct ASTNode* mode;
            struct ASTNode* colorswitch;
            struct ASTNode* apage;
            struct ASTNode* vpage;
        } screen;

        /* AST_PSET / AST_PRESET */
        struct {
            struct ASTNode* x;
            struct ASTNode* y;
            struct ASTNode* color;
            int   is_step;
        } pset;

        /* AST_LINE_GFX */
        struct {
            struct ASTNode* x1;
            struct ASTNode* y1;
            struct ASTNode* x2;
            struct ASTNode* y2;
            struct ASTNode* color;
            int   has_box;   /* B */
            int   has_fill;  /* BF */
            int   step1;
            int   step2;
        } line_gfx;

        /* AST_CIRCLE */
        struct {
            struct ASTNode* x;
            struct ASTNode* y;
            struct ASTNode* radius;
            struct ASTNode* color;
            struct ASTNode* start_angle;
            struct ASTNode* end_angle;
            struct ASTNode* aspect;
            int   is_step;
        } circle;

        /* AST_PAINT */
        struct {
            struct ASTNode* x;
            struct ASTNode* y;
            struct ASTNode* paint_color;
            struct ASTNode* border_color;
            int   is_step;
        } paint;

        /* AST_DRAW / AST_PLAY */
        struct { struct ASTNode* cmd_string; } draw_play;

        /* AST_SOUND */
        struct {
            struct ASTNode* freq;
            struct ASTNode* duration;
        } sound;

        /* AST_PALETTE */
        struct {
            struct ASTNode* attr;
            struct ASTNode* color;
        } palette;

        /* AST_PCOPY */
        struct {
            struct ASTNode* src;
            struct ASTNode* dst;
        } pcopy;

        /* AST_GET_GFX / AST_PUT_GFX */
        struct {
            struct ASTNode* x1;
            struct ASTNode* y1;
            struct ASTNode* x2;
            struct ASTNode* y2;
            struct ASTNode* array_name;
            int             action;  /* For PUT: 0=XOR, 1=PSET, 2=PRESET, 3=AND, 4=OR */
        } gfx_buffer;

        /* AST_WINDOW_STMT */
        struct {
            struct ASTNode* x1;
            struct ASTNode* y1;
            struct ASTNode* x2;
            struct ASTNode* y2;
            int   use_screen;
        } window;

        /* AST_VIEW_GFX */
        struct {
            struct ASTNode* x1;
            struct ASTNode* y1;
            struct ASTNode* x2;
            struct ASTNode* y2;
            struct ASTNode* color;
            struct ASTNode* border;
            int   use_screen;
        } view_gfx;

        /* AST_ON_KEY / AST_ON_TIMER */
        struct {
            int   key_id;
            double interval;
            char  label[42];
            int   lineno;
        } on_event;

        /* AST_KEY_CTRL / AST_TIMER_CTRL */
        struct {
            int key_id;
            int action;  /* 0=OFF, 1=ON, 2=STOP */
        } event_ctrl;

        /* AST_SLEEP */
        struct { struct ASTNode* seconds; } sleep_stmt;

        /* AST_CHAIN */
        struct {
            struct ASTNode* filename;
            struct ASTNode* start_line;
            int   merge;
            int   all;
        } chain;

        /* AST_COMMON */
        struct {
            char  (*names)[42];
            FBType* types;
            int*    is_array;
            int     name_count;
            int     is_shared;
        } common;

        /* AST_RANDOMIZE */
        struct { struct ASTNode* seed; int use_timer; } randomize;

        /* AST_ERASE */
        struct {
            char (*names)[42];
            int  name_count;
        } erase;

        /* AST_LOCK_STMT / AST_UNLOCK_STMT */
        struct {
            struct ASTNode* filenum;
            struct ASTNode* start_rec;
            struct ASTNode* end_rec;
        } lock;

        /* AST_POKE / AST_OUT_STMT */
        struct {
            struct ASTNode* addr;
            struct ASTNode* val;
        } poke_out;

        /* AST_FILES_STMT */
        struct { struct ASTNode* pattern; } files;

        /* AST_CLEAR */
        struct { int dummy; } clear;

        /* AST_RUN */
        struct { struct ASTNode* arg; } run; /* NULL = restart current */

    } data;
} ASTNode;

/* AST constructors */
ASTNode* ast_literal(int line, FBValue value);
ASTNode* ast_variable(int line, const char* name, FBType type_hint);
ASTNode* ast_binop(int line, TokenKind op, ASTNode* left, ASTNode* right);
ASTNode* ast_unop(int line, TokenKind op, ASTNode* operand);
ASTNode* ast_func_call(int line, const char* name, ASTNode** args, int arg_count);
ASTNode* ast_paren(int line, ASTNode* expr);
ASTNode* ast_print(int line, ASTNode** items, int* seps, int count, int trailing);
ASTNode* ast_let(int line, ASTNode* target, ASTNode* expr);
ASTNode* ast_goto(int line, const char* label, int lineno);
ASTNode* ast_gosub(int line, const char* label, int lineno);
ASTNode* ast_for(int line, ASTNode* var, ASTNode* start, ASTNode* end,
                 ASTNode* step, ASTNode** body, int body_count);
ASTNode* ast_end(int line);
ASTNode* ast_stop(int line);
ASTNode* ast_return(int line);
ASTNode* ast_rem(int line);
ASTNode* ast_label_def(int line, const char* name);
ASTNode* ast_exit(int line, int exit_what);
ASTNode* ast_cls(int line);
ASTNode* ast_beep(int line);

/* Free an AST node and all its children recursively */
void ast_free(ASTNode* node);

#endif
