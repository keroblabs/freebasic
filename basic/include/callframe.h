#ifndef CALLFRAME_H
#define CALLFRAME_H

#include "symtable.h"

#define CALL_STACK_MAX 256

typedef enum {
    FRAME_SUB,
    FRAME_FUNCTION,
    FRAME_DEF_FN
} FrameKind;

typedef struct {
    char     param_name[42];
    FBValue* caller_addr;     /* Pointer to caller's variable storage */
    int      is_byval;
} ParamBinding;

typedef struct {
    FrameKind    kind;
    Scope*       local_scope;
    int          return_pc;
    int          source_line;
    char         func_name[42];
    FBType       return_type;
    ParamBinding* param_bindings;
    int          param_count;
    int          is_static;
    Scope*       static_scope;   /* Persistent scope for STATIC procedures */
} CallFrame;

typedef struct {
    CallFrame  frames[CALL_STACK_MAX];
    int        sp;
} CallStack;

void       callstack_init(CallStack* cs);
int        callstack_push(CallStack* cs, CallFrame* frame);
CallFrame* callstack_top(CallStack* cs);
int        callstack_pop(CallStack* cs);

#endif
