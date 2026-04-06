# Phase 4 — Procedures & Scoping: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **SUB/FUNCTION procedures, scoping rules, call frames, and recursion** for the FreeBASIC interpreter. Phase 4 transforms the interpreter into a proper structured programming environment capable of running recursive algorithms and multi-procedure programs.

---

## Project File Structure (Phase 4 additions)

Files **added** or **significantly modified** relative to Phase 3 are marked with `[NEW]` or `[MOD]`.

```
fbasic/
├── Makefile                        [MOD]
├── include/
│   ├── ast.h                      [MOD] — AST_SUB_DEF, AST_FUNCTION_DEF, AST_CALL, AST_EXIT
│   ├── parser.h                   [MOD] — procedure parsing
│   ├── interpreter.h              [MOD] — call frame stack, procedure dispatch
│   ├── callframe.h                [NEW] — call frame structure and stack API
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse SUB, FUNCTION, DECLARE, CALL, etc.
│   ├── interpreter.c              [MOD] — procedure invocation, scoping, pass-by-reference
│   ├── callframe.c                [NEW] — call frame management
│   └── ...
└── tests/
    ├── test_procedures.c          [NEW] — unit tests for call/return
    └── verify/
        ├── phase4_sub.bas         [NEW] — SUB tests
        ├── phase4_function.bas    [NEW] — FUNCTION tests
        ├── phase4_scope.bas       [NEW] — scoping rules tests
        ├── phase4_recursion.bas   [NEW] — recursive procedures
        ├── phase4_deffn.bas       [NEW] — DEF FN tests
        ├── phase4_on_goto.bas     [NEW] — ON...GOTO/GOSUB tests
        ├── phase4_hanoi.bas       [NEW] — milestone: Tower of Hanoi
        ├── phase4_structured.bas  [NEW] — milestone: multi-procedure program
        └── phase4_expected/       [NEW]
            ├── sub.txt
            ├── function.txt
            ├── scope.txt
            ├── recursion.txt
            ├── deffn.txt
            ├── on_goto.txt
            ├── hanoi.txt
            └── structured.txt
```

---

## 1. Call Frame Stack (`include/callframe.h`, `src/callframe.c`)

### 1.1 Call Frame Structure

```c
#ifndef CALLFRAME_H
#define CALLFRAME_H

#include "symtable.h"

#define CALL_STACK_MAX 256

typedef enum {
    FRAME_SUB,
    FRAME_FUNCTION,
    FRAME_DEF_FN,
    FRAME_GOSUB        // GOSUB still uses simple return stack
} FrameKind;

typedef struct {
    FrameKind    kind;
    Scope*       local_scope;     // Local variables for this call
    int          return_pc;       // Statement index to return to
    int          source_line;     // Source line of CALL (for error messages)

    // For FUNCTION: where to store the return value
    char         func_name[42];   // Name of the function (for FuncName = expr)
    FBType       return_type;     // Expected return type

    // For pass-by-reference: mapping of param names to caller's lvalue addresses
    struct {
        char     param_name[42];  // Parameter name in callee scope
        FBValue* caller_addr;     // Pointer to caller's variable storage
        int      is_byval;        // 1 if passed by value (extra parens)
    }* param_bindings;
    int          param_count;

    // STATIC local preservation (STATIC SUB/FUNCTION)
    int          is_static;       // Preserve locals across calls
    Scope*       static_scope;    // Persistent scope for STATIC procedures
} CallFrame;

typedef struct {
    CallFrame  frames[CALL_STACK_MAX];
    int        sp;    // Stack pointer (0 = empty)
} CallStack;

void callstack_init(CallStack* cs);
int  callstack_push(CallStack* cs, CallFrame* frame);  // Returns 0 on success
CallFrame* callstack_top(CallStack* cs);
int  callstack_pop(CallStack* cs);

#endif
```

### 1.2 Pass-by-Reference Mechanics

FB passes arguments by reference by default. This means the callee's parameter is an alias for the caller's variable.

```c
// During CALL setup:
// 1. Evaluate each argument expression
// 2. For each parameter:
//    a. If argument is a simple variable → pass by reference (store pointer)
//    b. If argument is an expression or (expr) → pass by value (create local copy)
// 3. Create local scope for callee
// 4. For by-ref params: local param names point to caller's storage
// 5. For by-val params: local param names get copies of values

static void setup_call_frame(Interpreter* interp, ASTNode* call_node,
                              ASTNode* proc_def, CallFrame* frame) {
    int argc = call_node->data.call.arg_count;
    ASTNode** call_args = call_node->data.call.args;
    int paramc = proc_def->data.proc_def.param_count;

    // Validate argument count
    if (argc != paramc) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, call_node->line,
                 "Wrong number of arguments");
        return;
    }

    frame->param_bindings = calloc(paramc, sizeof(*frame->param_bindings));
    frame->param_count = paramc;

    for (int i = 0; i < paramc; i++) {
        strncpy(frame->param_bindings[i].param_name,
                proc_def->data.proc_def.params[i].name, 41);

        // Check if argument is a parenthesized expression → force by-value
        if (call_args[i]->kind == AST_PAREN) {
            frame->param_bindings[i].is_byval = 1;
            frame->param_bindings[i].caller_addr = NULL;
        }
        // Check if argument is a simple variable → by-reference
        else if (call_args[i]->kind == AST_VARIABLE ||
                 call_args[i]->kind == AST_ARRAY_ACCESS) {
            frame->param_bindings[i].is_byval = 0;
            frame->param_bindings[i].caller_addr =
                resolve_lvalue(interp, call_args[i]);
        }
        // Expression → by-value
        else {
            frame->param_bindings[i].is_byval = 1;
            frame->param_bindings[i].caller_addr = NULL;
        }
    }
}
```

---

## 2. SUB...END SUB

### 2.1 Syntax

```basic
SUB Name [(paramlist)] [STATIC]
    statements
END SUB
```

Called via: `CALL Name(args)` or `Name args`

### 2.2 AST Node

```c
// AST_SUB_DEF
struct {
    char            name[42];
    struct {
        char    name[42];
        FBType  type;
    }* params;
    int             param_count;
    int             is_static;      // STATIC keyword present
    struct ASTNode** body;
    int             body_count;
} proc_def;

// AST_CALL
struct {
    char            name[42];
    struct ASTNode** args;
    int             arg_count;
} call;
```

### 2.3 Parse SUB Definition

SUB/FUNCTION definitions are parsed at the top level. They are NOT nested inside other code. FB requires all SUB/FUNCTION to be defined after the main module code.

```c
static void parse_sub_def(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume SUB

    char name[42];
    strncpy(name, current_token(p)->value.str.text, 41);
    advance(p);

    // Parse parameter list
    Param params[64];
    int param_count = 0;
    if (current_token(p)->kind == TOK_LPAREN) {
        advance(p);
        if (current_token(p)->kind != TOK_RPAREN) {
            parse_param_list(p, params, &param_count);
        }
        expect(p, TOK_RPAREN);
    }

    // STATIC keyword
    int is_static = 0;
    if (current_token(p)->kind == TOK_KW_STATIC) {
        is_static = 1;
        advance(p);
    }
    expect_eol(p);

    // Parse body until END SUB
    ASTNode** body = NULL;
    int body_count = 0;
    parse_block(p, &body, &body_count, TOK_KW_END, -1, -1);
    expect(p, TOK_KW_END);
    expect(p, TOK_KW_SUB);

    ASTNode* node = ast_sub_def(line, name, params, param_count,
                                 is_static, body, body_count);

    // Register in program's procedure table
    program_add_procedure(p->prog, node);
}

static void parse_param_list(Parser* p, Param* params, int* count) {
    do {
        Token* t = current_token(p);
        strncpy(params[*count].name, t->value.str.text, 41);
        params[*count].type = ident_type_from_token(t);
        advance(p);

        // Optional AS type
        if (current_token(p)->kind == TOK_KW_AS) {
            advance(p);
            params[*count].type = parse_type_name(p);
        }
        (*count)++;
    } while (current_token(p)->kind == TOK_COMMA && (advance(p), 1));
}
```

### 2.4 Parse CALL Statement

```c
// Two invocation forms:
// CALL Name(arg1, arg2)    — CALL keyword, parens required
// Name arg1, arg2          — no CALL, no parens

static void parse_call_keyword(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume CALL

    char name[42];
    strncpy(name, current_token(p)->value.str.text, 41);
    advance(p);

    expect(p, TOK_LPAREN);
    ASTNode* args[64];
    int argc = 0;
    if (current_token(p)->kind != TOK_RPAREN) {
        args[argc++] = parse_expr(p, 1);
        while (current_token(p)->kind == TOK_COMMA) {
            advance(p);
            args[argc++] = parse_expr(p, 1);
        }
    }
    expect(p, TOK_RPAREN);

    program_add_stmt(p->prog,
        ast_call(line, name, copy_node_array(args, argc), argc));
}

// Implicit call (no CALL keyword) — detected when identifier at statement
// start is a known SUB name
static void parse_implicit_call(Parser* p) {
    int line = current_token(p)->line;
    char name[42];
    strncpy(name, current_token(p)->value.str.text, 41);
    advance(p);

    ASTNode* args[64];
    int argc = 0;
    // Parse arguments without parentheses (space-separated, comma-delimited)
    if (current_token(p)->kind != TOK_EOL &&
        current_token(p)->kind != TOK_COLON &&
        current_token(p)->kind != TOK_EOF) {
        args[argc++] = parse_expr(p, 1);
        while (current_token(p)->kind == TOK_COMMA) {
            advance(p);
            args[argc++] = parse_expr(p, 1);
        }
    }

    program_add_stmt(p->prog,
        ast_call(line, name, copy_node_array(args, argc), argc));
}
```

### 2.5 Execute SUB Call

```c
static void exec_call(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.call.name;

    // Find the SUB definition
    ASTNode* proc = program_find_procedure(interp->prog, name);
    if (!proc) {
        fb_error(FB_ERR_SYNTAX, node->line, "Undefined SUB");
        return;
    }

    // Set up call frame
    CallFrame frame = {0};
    frame.kind = FRAME_SUB;
    frame.return_pc = interp->pc;
    frame.source_line = node->line;
    frame.is_static = proc->data.proc_def.is_static;

    setup_call_frame(interp, node, proc, &frame);

    // Create local scope
    if (frame.is_static && frame.static_scope) {
        frame.local_scope = frame.static_scope;
    } else {
        frame.local_scope = scope_new(interp->global_scope);
    }

    // Bind parameters
    for (int i = 0; i < frame.param_count; i++) {
        FBType ptype = proc->data.proc_def.params[i].type;
        if (frame.param_bindings[i].is_byval) {
            // By value: evaluate and copy into local scope
            FBValue val = eval_expr(interp, node->data.call.args[i]);
            FBValue coerced = fbval_coerce(&val, ptype);
            fbval_release(&val);
            Symbol* sym = scope_insert(frame.local_scope,
                              frame.param_bindings[i].param_name,
                              SYM_VARIABLE, ptype);
            sym->value = coerced;
        } else {
            // By reference: create alias in local scope pointing to caller's storage
            Symbol* sym = scope_insert(frame.local_scope,
                              frame.param_bindings[i].param_name,
                              SYM_VARIABLE, ptype);
            // Note: for by-ref, we need the scope_insert to create a "ref" symbol
            // that directly shares the caller's FBValue storage
            sym->value = fbval_copy(frame.param_bindings[i].caller_addr);
            sym->is_ref = 1;
            sym->ref_addr = frame.param_bindings[i].caller_addr;
        }
    }

    // Push frame and execute
    callstack_push(&interp->call_stack, &frame);
    Scope* saved_scope = interp->current_scope;
    interp->current_scope = frame.local_scope;

    exec_block(interp, proc->data.proc_def.body,
               proc->data.proc_def.body_count);

    // Write back by-ref values
    for (int i = 0; i < frame.param_count; i++) {
        if (!frame.param_bindings[i].is_byval &&
            frame.param_bindings[i].caller_addr) {
            Symbol* sym = scope_lookup_local(frame.local_scope,
                              frame.param_bindings[i].param_name);
            if (sym) {
                fbval_release(frame.param_bindings[i].caller_addr);
                *frame.param_bindings[i].caller_addr = fbval_copy(&sym->value);
            }
        }
    }

    // Pop frame and restore scope
    interp->current_scope = saved_scope;
    callstack_pop(&interp->call_stack);

    if (!frame.is_static) {
        scope_free(frame.local_scope);
    } else {
        frame.static_scope = frame.local_scope; // Preserve for next call
    }
    free(frame.param_bindings);
}
```

### 2.6 EXIT SUB

```c
// EXIT SUB: unwind to the enclosing SUB frame.
// Implemented as a flag on the interpreter that causes exec_block to return early.
static void exec_exit_sub(Interpreter* interp, ASTNode* node) {
    interp->exit_procedure = 1; // Flag checked by exec_block
}

// In exec_block:
static void exec_block(Interpreter* interp, ASTNode** stmts, int count) {
    for (int i = 0; i < count && interp->running; i++) {
        if (interp->exit_procedure) return;
        exec_statement(interp, stmts[i]);
    }
}
```

---

## 3. FUNCTION...END FUNCTION

### 3.1 Syntax

```basic
FUNCTION Name [type_suffix] [(paramlist)] [STATIC]
    Name = return_value
END FUNCTION
```

Return value is assigned by `FunctionName = expr`.

### 3.2 Parse FUNCTION Definition

```c
static void parse_function_def(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume FUNCTION

    Token* name_tok = current_token(p);
    char name[42];
    strncpy(name, name_tok->value.str.text, 41);
    FBType return_type = ident_type_from_token(name_tok);
    advance(p);

    // Parse parameter list
    Param params[64];
    int param_count = 0;
    if (current_token(p)->kind == TOK_LPAREN) {
        advance(p);
        if (current_token(p)->kind != TOK_RPAREN) {
            parse_param_list(p, params, &param_count);
        }
        expect(p, TOK_RPAREN);
    }

    // STATIC keyword
    int is_static = 0;
    if (current_token(p)->kind == TOK_KW_STATIC) {
        is_static = 1;
        advance(p);
    }
    expect_eol(p);

    // Parse body until END FUNCTION
    ASTNode** body = NULL;
    int body_count = 0;
    parse_block(p, &body, &body_count, TOK_KW_END, -1, -1);
    expect(p, TOK_KW_END);
    expect(p, TOK_KW_FUNCTION);

    ASTNode* node = ast_func_def(line, name, return_type,
                                  params, param_count,
                                  is_static, body, body_count);
    program_add_procedure(p->prog, node);
}
```

### 3.3 Execute FUNCTION Call

Functions are called from expressions. The evaluator handles `AST_FUNC_CALL` by checking if the function name matches a user-defined FUNCTION.

```c
static FBValue eval_user_function(Interpreter* interp, ASTNode* expr) {
    const char* name = expr->data.func_call.name;
    ASTNode* func_def = program_find_procedure(interp->prog, name);

    // Set up call frame (similar to SUB)
    CallFrame frame = {0};
    frame.kind = FRAME_FUNCTION;
    strncpy(frame.func_name, name, 41);
    frame.return_type = func_def->data.proc_def.return_type;

    // Create local scope and bind parameters
    frame.local_scope = scope_new(interp->global_scope);

    // Insert the function name as a local variable (for return value)
    Symbol* ret_sym = scope_insert(frame.local_scope, name,
                                    SYM_VARIABLE, frame.return_type);
    // Default return value
    switch (frame.return_type) {
        case FB_INTEGER: ret_sym->value = fbval_int(0); break;
        case FB_LONG:    ret_sym->value = fbval_long(0); break;
        case FB_SINGLE:  ret_sym->value = fbval_single(0.0f); break;
        case FB_DOUBLE:  ret_sym->value = fbval_double(0.0); break;
        case FB_STRING:  ret_sym->value = fbval_string_from_cstr(""); break;
    }

    // Bind parameters (same as SUB)
    // ... (parameter binding code)

    callstack_push(&interp->call_stack, &frame);
    Scope* saved = interp->current_scope;
    interp->current_scope = frame.local_scope;
    interp->exit_procedure = 0;

    exec_block(interp, func_def->data.proc_def.body,
               func_def->data.proc_def.body_count);

    interp->exit_procedure = 0;

    // Get return value (the function name variable)
    FBValue result = fbval_copy(&ret_sym->value);

    // Write back by-ref params, restore scope, pop frame
    interp->current_scope = saved;
    callstack_pop(&interp->call_stack);

    // Write back by-ref values
    // ... (same as SUB)

    scope_free(frame.local_scope);
    free(frame.param_bindings);

    return result;
}
```

---

## 4. DECLARE Statement

### 4.1 Syntax

```basic
DECLARE SUB Name [(paramtypes)]
DECLARE FUNCTION Name [type] [(paramtypes)]
```

Forward declaration — allows the parser to know that a name is a SUB or FUNCTION before the definition is encountered.

```c
static void parse_declare(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume DECLARE

    int is_function = 0;
    if (current_token(p)->kind == TOK_KW_SUB) {
        advance(p);
    } else if (current_token(p)->kind == TOK_KW_FUNCTION) {
        is_function = 1;
        advance(p);
    } else {
        fb_syntax_error(line, current_token(p)->col,
                        "Expected SUB or FUNCTION after DECLARE");
    }

    char name[42];
    strncpy(name, current_token(p)->value.str.text, 41);
    FBType return_type = ident_type_from_token(current_token(p));
    advance(p);

    // Optional parameter type list
    Param params[64];
    int param_count = 0;
    if (current_token(p)->kind == TOK_LPAREN) {
        advance(p);
        if (current_token(p)->kind != TOK_RPAREN) {
            parse_param_list(p, params, &param_count);
        }
        expect(p, TOK_RPAREN);
    }

    // Register as known procedure name for parser disambiguation
    program_declare_procedure(p->prog, name, is_function,
                               return_type, params, param_count);
}
```

---

## 5. Scoping Rules

### 5.1 Scope Hierarchy

```
Module-level scope (global)
    │
    ├── SUB scope (local to call)
    │   └── Can see: own locals, SHARED variables, module-level via SHARED
    │
    ├── FUNCTION scope (local to call)
    │   └── Same as SUB
    │
    └── DEF FN scope (shares module-level except params/STATIC)
```

### 5.2 DIM SHARED

`DIM SHARED x AS INTEGER` at module level creates a variable visible inside all SUB/FUNCTION without explicit SHARED statement.

```c
// In exec_dim: if is_shared, insert into global_scope with shared flag.
// When looking up variables inside procedure scope:
// 1. Search local scope
// 2. Search global scope for SHARED variables only
static Symbol* procedure_lookup(Interpreter* interp, const char* name) {
    // First: local scope
    Symbol* sym = scope_lookup_local(interp->current_scope, name);
    if (sym) return sym;

    // Second: check if in a procedure frame
    CallFrame* frame = callstack_top(&interp->call_stack);
    if (frame) {
        // Check module-level for DIM SHARED variables
        Symbol* global_sym = scope_lookup_local(interp->global_scope, name);
        if (global_sym && global_sym->is_shared) {
            return global_sym;
        }
    } else {
        // Module level — search normally
        return scope_lookup(interp->current_scope, name);
    }
    return NULL;
}
```

### 5.3 SHARED Statement Inside Procedures

```basic
SUB MySub
    SHARED x, y$    ' Access module-level x and y$
    PRINT x; y$
END SUB
```

```c
// AST_SHARED
struct {
    char names[64][42];
    int  name_count;
} shared_stmt;

static void exec_shared(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.shared_stmt.name_count; i++) {
        // Find the module-level variable
        Symbol* global = scope_lookup_local(interp->global_scope,
                             node->data.shared_stmt.names[i]);
        if (!global) {
            fb_error(FB_ERR_SYNTAX, node->line,
                     "SHARED: variable not found at module level");
            continue;
        }
        // Create alias in current local scope pointing to global
        Symbol* local = scope_insert(interp->current_scope,
                            node->data.shared_stmt.names[i],
                            SYM_VARIABLE, global->type);
        local->is_ref = 1;
        local->ref_addr = &global->value;
        local->value = fbval_copy(&global->value);
    }
}
```

### 5.4 STATIC Variables

```basic
SUB Counter STATIC
    count% = count% + 1    ' Preserved between calls
    PRINT count%
END SUB
```

Or individual static variables:
```basic
SUB Counter
    STATIC count%
    count% = count% + 1
    PRINT count%
END SUB
```

For `SUB ... STATIC`, the entire local scope is preserved. For `STATIC varname`, only listed variables are preserved.

---

## 6. DEF FN...END DEF

### 6.1 Syntax

```basic
' Single-line:
DEF FnDouble(x) = x * 2

' Multi-line:
DEF FnClamp(x, lo, hi)
    IF x < lo THEN FnClamp = lo
    ELSEIF x > hi THEN FnClamp = hi
    ELSE FnClamp = x
    END IF
END DEF
```

### 6.2 Scoping

DEF FN shares the module-level scope — all module variables are visible. Only the parameters and STATIC variables are local.

```c
static FBValue eval_def_fn(Interpreter* interp, const char* name,
                            ASTNode** args, int argc) {
    ASTNode* def = program_find_def_fn(interp->prog, name);
    if (!def) {
        fb_error(FB_ERR_SYNTAX, 0, "Undefined DEF FN");
        return fbval_int(0);
    }

    // Save current parameter values (DEF FN parameters shadow module vars)
    FBValue saved_params[16];
    for (int i = 0; i < def->data.def_fn.param_count; i++) {
        Symbol* sym = scope_lookup(interp->current_scope,
                          def->data.def_fn.params[i].name);
        if (sym) saved_params[i] = fbval_copy(&sym->value);
    }

    // Assign parameter values
    for (int i = 0; i < argc; i++) {
        FBValue val = eval_expr(interp, args[i]);
        Symbol* sym = resolve_or_create_var_by_name(interp,
                          def->data.def_fn.params[i].name,
                          def->data.def_fn.params[i].type);
        fbval_release(&sym->value);
        sym->value = fbval_coerce(&val, sym->type);
        fbval_release(&val);
    }

    // Create return variable
    Symbol* ret = resolve_or_create_var_by_name(interp, name, def->data.def_fn.return_type);
    // Default return value
    fbval_release(&ret->value);
    ret->value = fbval_int(0);

    // Execute body (shares module scope)
    if (def->data.def_fn.is_single_line) {
        FBValue result = eval_expr(interp, def->data.def_fn.expr);
        fbval_release(&ret->value);
        ret->value = result;
    } else {
        exec_block(interp, def->data.def_fn.body, def->data.def_fn.body_count);
    }

    FBValue result = fbval_copy(&ret->value);

    // Restore saved parameter values
    for (int i = 0; i < def->data.def_fn.param_count; i++) {
        Symbol* sym = scope_lookup(interp->current_scope,
                          def->data.def_fn.params[i].name);
        if (sym) {
            fbval_release(&sym->value);
            sym->value = saved_params[i];
        }
    }

    return result;
}
```

---

## 7. ON...GOSUB / ON...GOTO

### 7.1 Syntax

```basic
ON expression GOTO label1, label2, label3
ON expression GOSUB label1, label2, label3
```

Expression evaluated to integer (1-based index). If value is < 1 or > number of labels, execution continues with next statement (no error).

### 7.2 Parse & Execute

```c
// AST_ON_GOTO / AST_ON_GOSUB
struct {
    struct ASTNode* expr;        // Selector expression
    int             is_gosub;    // 0 = GOTO, 1 = GOSUB
    char            labels[64][42];
    int32_t         linenos[64]; // -1 if label, else line number
    int             target_count;
} on_branch;

static void exec_on_branch(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.on_branch.expr);
    int index = (int)fbval_to_long(&val);
    fbval_release(&val);

    // FB: if index < 1 or > count, skip (no error)
    if (index < 1 || index > node->data.on_branch.target_count) {
        return; // Continue with next statement
    }

    int target_idx;
    if (node->data.on_branch.linenos[index - 1] >= 0) {
        target_idx = program_find_lineno(interp->prog,
                         node->data.on_branch.linenos[index - 1]);
    } else {
        target_idx = program_find_label(interp->prog,
                         node->data.on_branch.labels[index - 1]);
    }

    if (target_idx < 0) {
        fb_error(FB_ERR_UNDEFINED_LABEL, node->line, NULL);
        return;
    }

    if (node->data.on_branch.is_gosub) {
        // Push return address
        interp->gosub_stack[interp->gosub_sp].return_pc = interp->pc + 1;
        interp->gosub_stack[interp->gosub_sp].line = node->line;
        interp->gosub_sp++;
    }

    interp->pc = target_idx;
}
```

---

## 8. Passing by Value

### 8.1 Syntax

```basic
CALL MySub((x))    ' (x) in extra parens forces by-value
```

The parser already handles this: `AST_PAREN` nodes wrap the argument, and the call frame setup detects this.

---

## 9. Procedure Table in Program

```c
// Add to Program struct:
typedef struct {
    ASTNode** procedures;    // Array of AST_SUB_DEF and AST_FUNCTION_DEF nodes
    int       proc_count;
    int       proc_cap;

    // Forward declarations for disambiguation
    struct {
        char    name[42];
        int     is_function;
        FBType  return_type;
    }* declarations;
    int  decl_count;
    int  decl_cap;
} Program;

// Find a procedure by name (case-insensitive)
ASTNode* program_find_procedure(const Program* prog, const char* name) {
    for (int i = 0; i < prog->proc_count; i++) {
        if (_stricmp(prog->procedures[i]->data.proc_def.name, name) == 0) {
            return prog->procedures[i];
        }
    }
    return NULL;
}
```

---

## 10. Verification Test Files

### 10.1 `tests/verify/phase4_sub.bas` — SUB Tests

```basic
REM Phase 4 Test: SUB procedures
DECLARE SUB Greet (name$)
DECLARE SUB AddOne (x%)

DIM n AS INTEGER
n = 10
CALL AddOne(n)
PRINT "After AddOne:"; n

Greet "World"

SUB Greet (name$)
    PRINT "Hello, "; name$; "!"
END SUB

SUB AddOne (x%)
    x% = x% + 1
END SUB
```

**Expected output (`tests/verify/phase4_expected/sub.txt`):**
```
After AddOne: 11
Hello, World!
```

### 10.2 `tests/verify/phase4_function.bas` — FUNCTION Tests

```basic
REM Phase 4 Test: FUNCTION procedures
DECLARE FUNCTION Square% (n%)
DECLARE FUNCTION Factorial& (n%)
DECLARE FUNCTION Max% (a%, b%)

PRINT "Square(5) ="; Square%(5)
PRINT "Factorial(6) ="; Factorial&(6)
PRINT "Max(3, 7) ="; Max%(3, 7)

FUNCTION Square% (n%)
    Square% = n% * n%
END FUNCTION

FUNCTION Factorial& (n%)
    IF n% <= 1 THEN
        Factorial& = 1
    ELSE
        Factorial& = n% * Factorial&(n% - 1)
    END IF
END FUNCTION

FUNCTION Max% (a%, b%)
    IF a% > b% THEN
        Max% = a%
    ELSE
        Max% = b%
    END IF
END FUNCTION
```

**Expected output (`tests/verify/phase4_expected/function.txt`):**
```
Square(5) = 25
Factorial(6) = 720
Max(3, 7) = 7
```

### 10.3 `tests/verify/phase4_hanoi.bas` — Milestone: Tower of Hanoi

```basic
REM Tower of Hanoi - Phase 4 Milestone
DECLARE SUB Hanoi (n%, fromPeg$, toPeg$, auxPeg$)

Hanoi 3, "A", "C", "B"

SUB Hanoi (n%, fromPeg$, toPeg$, auxPeg$)
    IF n% = 1 THEN
        PRINT "Move disk 1 from "; fromPeg$; " to "; toPeg$
    ELSE
        Hanoi n% - 1, fromPeg$, auxPeg$, toPeg$
        PRINT "Move disk"; n%; "from "; fromPeg$; " to "; toPeg$
        Hanoi n% - 1, auxPeg$, toPeg$, fromPeg$
    END IF
END SUB
```

**Expected output (`tests/verify/phase4_expected/hanoi.txt`):**
```
Move disk 1 from A to C
Move disk 2 from A to B
Move disk 1 from C to B
Move disk 3 from A to C
Move disk 1 from B to A
Move disk 2 from B to C
Move disk 1 from A to C
```

### 10.4 `tests/verify/phase4_scope.bas` — Scoping Rules

```basic
REM Phase 4 Test: Scoping
DECLARE SUB TestShared ()
DECLARE SUB TestByVal (x%)

DIM SHARED globalVar%
globalVar% = 100

TestShared
PRINT "Global after TestShared:"; globalVar%

DIM n%
n% = 42
TestByVal (n%)
PRINT "n after TestByVal with parens:"; n%

SUB TestShared
    PRINT "Inside TestShared, globalVar ="; globalVar%
    globalVar% = 200
END SUB

SUB TestByVal (x%)
    x% = 999
END SUB
```

**Expected output (`tests/verify/phase4_expected/scope.txt`):**
```
Inside TestShared, globalVar = 100
Global after TestShared: 200
n after TestByVal with parens: 42
```

---

## 11. Updated Makefile

```makefile
SRC = src/main.c src/lexer.c src/value.c src/symtable.c src/ast.c \
      src/parser.c src/interpreter.c src/coerce.c src/error.c \
      src/console.c src/builtins_str.c src/builtins_math.c \
      src/print_using.c src/array.c src/udt.c src/builtins_convert.c \
      src/callframe.c

test-phase4: fbasic
	@echo "Running Phase 4 tests..."
	@for f in sub function scope recursion deffn on_goto hanoi structured; do \
		./fbasic tests/verify/phase4_$$f.bas > /tmp/p4_$$f.txt && \
		diff /tmp/p4_$$f.txt tests/verify/phase4_expected/$$f.txt && \
		echo "  PASS: $$f" || echo "  FAIL: $$f"; \
	done
```

---

## 12. Phase 4 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **SUB...END SUB** | Procedures defined, invoked via CALL and implicit call syntax. Parameters passed by reference by default. EXIT SUB works. STATIC SUB preserves locals. |
| 2 | **FUNCTION...END FUNCTION** | Return value via `FuncName = expr`. Recursive calls work. Callable from expressions. EXIT FUNCTION works. |
| 3 | **DECLARE** | Forward declarations allow parser to disambiguate SUB/FUNCTION names. Argument count/type validation at parse time. |
| 4 | **Call-frame stack** | Proper stack frames for recursion (256 deep). Local variable allocation per call. Stack overflow detected. |
| 5 | **Pass by reference** | Modifications to parameters visible in caller. Array elements as arguments work. |
| 6 | **Pass by value** | `CALL Sub((expr))` with extra parens prevents modification of caller's variable. |
| 7 | **DIM SHARED** | Module-level SHARED variables visible inside all procedures without SHARED statement. |
| 8 | **SHARED statement** | `SHARED varlist` inside SUB/FUNCTION aliases specific module-level variables. |
| 9 | **STATIC** | `STATIC varlist` preserves individual locals between calls. `SUB...STATIC` preserves entire local scope. |
| 10 | **DEF FN...END DEF** | Single-line and multi-line forms. Shares module scope. Parameters shadow module vars and are restored after call. EXIT DEF works. |
| 11 | **ON...GOTO/GOSUB** | Computed branch with 1-based index. Out-of-range → no branch (no error). Both GOTO and GOSUB variants work. |
| 12 | **Recursion** | Factorial, Fibonacci, Tower of Hanoi all produce correct output with proper stack frame isolation. |
| 13 | **Milestone Programs** | Tower of Hanoi and multi-procedure structured programs produce correct output. |
| 14 | **No Leaks** | Call frames properly freed. Local scopes deallocated on return. By-ref parameter write-back correct. |

---

## 13. Key Implementation Warnings

1. **Parser disambiguation:** When the parser encounters an identifier at statement start, it must determine whether it's an assignment (`x = 5`) or a SUB call (`MySub 5`). Use the DECLARE table: if the name is a declared SUB, parse as implicit call. Otherwise, parse as assignment. Without DECLARE, FB's IDE would prompt — in our interpreter, assume assignment unless declared.

2. **Recursive FUNCTION and name collision:** The function's return variable shares the function name. Inside `FUNCTION Factorial&`, `Factorial& = n * Factorial&(n-1)` — the right side is a recursive call (because followed by `(`), the left side is return assignment. The parser distinguishes: `FuncName(args)` = call, `FuncName = expr` at statement start = return assignment.

3. **By-reference with array elements:** `CALL MySub(arr(5))` should pass a reference to `arr(5)`. The callee's parameter is an alias for that specific array element. Writes to the parameter inside the SUB update `arr(5)` in the caller.

4. **STATIC scope lifecycle:** For `SUB Counter STATIC`, the local scope persists across calls. This means the Scope object must be stored somewhere persistent (e.g., attached to the procedure's AST node or in the Program struct). Don't free it on SUB return.

5. **DEF FN reentrancy:** DEF FN shares module scope, so recursive DEF FN calls see the same variables. However, parameters are shadowed/restored. If `DEF FnA(x)` calls itself, the inner call's `x` overwrites the outer call's `x` (unlike SUB/FUNCTION which get separate frames). This is FB's documented behavior.

6. **Procedure definitions at module end:** In FB, SUB and FUNCTION definitions appear after the main module code (separated by a line). The parser should first scan for all DECLARE statements and SUB/FUNCTION definitions (two-pass or pre-scan), then parse the main module body. This ensures forward references work.

7. **EXIT FUNCTION return value:** When `EXIT FUNCTION` is executed, the function returns whatever value has been assigned to `FuncName` so far (or the default if never assigned). Don't lose the partial return value.
