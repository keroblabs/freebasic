# Phase 6 — Error Handling: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **ON ERROR GOTO, RESUME, ERR/ERL, and tracing** for the FreeBASIC interpreter. Phase 6 transforms fatal runtime errors into recoverable conditions, enabling robust programs with file error handling and defensive coding patterns.

---

## Project File Structure (Phase 6 additions)

```
fbasic/
├── Makefile                        [MOD]
├── include/
│   ├── ast.h                      [MOD] — AST_ON_ERROR, AST_RESUME, AST_ERROR, AST_TRON/TROFF
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD] — error handler state, setjmp context
│   ├── error.h                    [MOD] — complete error code table, handler registration
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse ON ERROR, RESUME, ERROR, TRON, TROFF
│   ├── interpreter.c              [MOD] — error trap dispatch, RESUME mechanics
│   ├── error.c                    [MOD] — complete error message table (~75 codes)
│   └── ...
└── tests/
    └── verify/
        ├── phase6_on_error.bas    [NEW] — ON ERROR GOTO / RESUME tests
        ├── phase6_err_erl.bas     [NEW] — ERR / ERL access tests
        ├── phase6_error_stmt.bas  [NEW] — ERROR n statement tests
        ├── phase6_tron.bas        [NEW] — TRON / TROFF tests
        ├── phase6_robust.bas      [NEW] — milestone: robust file handler
        └── phase6_expected/       [NEW]
            ├── on_error.txt
            ├── err_erl.txt
            ├── error_stmt.txt
            ├── tron.txt
            └── robust.txt
```

---

## 1. Error Handler Architecture

### 1.1 Interpreter Error State

```c
#include <setjmp.h>

// Add to Interpreter struct:
typedef struct Interpreter {
    // ... previous fields ...

    // Error handling
    int          on_error_target;   // Statement index for error handler (-1 = none)
    int          on_error_active;   // 1 if ON ERROR GOTO is active
    int          in_error_handler;  // 1 if currently in error handler
    int          err_code;          // Most recent error code (ERR)
    int          err_line;          // Line where error occurred (ERL)
    int          resume_pc;         // Statement index to resume at
    int          resume_next_pc;    // Statement index for RESUME NEXT

    jmp_buf      error_jmp;         // setjmp/longjmp context for error dispatch
    int          error_jmp_valid;   // 1 if error_jmp has been set

    // TRON/TROFF
    int          tron_active;       // 1 if line tracing is on
} Interpreter;
```

### 1.2 Error Dispatch Flow

```
Normal execution → runtime error occurs
    │
    ├── ON ERROR GOTO active?
    │   ├── YES → save ERR/ERL, longjmp to error handler
    │   │         └── Handler executes...
    │   │             ├── RESUME → retry the failing statement
    │   │             ├── RESUME NEXT → skip to statement after the failing one
    │   │             └── RESUME label → jump to specific label
    │   │
    │   └── NO → print error message and terminate (Phase 0-5 behavior)
    │
    └── In error handler and another error occurs?
        └── Fatal: print and terminate (no nested error handling in FB)
```

### 1.3 Modified Error Reporting

```c
// Replace the simple fb_error() with a handler-aware version:
void fb_runtime_error(Interpreter* interp, FBErrorCode code,
                      int line, const char* extra) {
    if (interp->on_error_active && !interp->in_error_handler) {
        // Trap the error
        interp->err_code = code;
        interp->err_line = line;
        interp->resume_pc = interp->pc;       // For RESUME (retry)
        interp->resume_next_pc = interp->pc + 1; // For RESUME NEXT

        interp->in_error_handler = 1;

        // Jump to error handler
        if (interp->error_jmp_valid) {
            longjmp(interp->error_jmp, 1);
        } else {
            interp->pc = interp->on_error_target;
        }
    } else {
        // No handler — fatal error
        const char* msg = fb_error_message(code);
        if (extra) {
            fprintf(stderr, "Error %d: %s (%s) in line %d\n",
                    code, msg, extra, line);
        } else {
            fprintf(stderr, "Error %d: %s in line %d\n", code, msg, line);
        }
        interp->running = 0;
    }
}
```

---

## 2. ON ERROR GOTO

### 2.1 Syntax

```basic
ON ERROR GOTO label     ' Enable error handler
ON ERROR GOTO linenumber
ON ERROR GOTO 0         ' Disable error handler
```

### 2.2 AST Node

```c
// AST_ON_ERROR
struct {
    char    label[42];   // Target label (empty if line number)
    int32_t lineno;      // -1 for label, 0 for "disable"
    int     disable;     // 1 if ON ERROR GOTO 0
} on_error;
```

### 2.3 Parse ON ERROR GOTO

```c
static void parse_on_error(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume ON
    expect(p, TOK_KW_ERROR);
    expect(p, TOK_KW_GOTO);

    Token* target = current_token(p);

    if (target->kind == TOK_INTEGER_LIT && target->value.int_val == 0) {
        // ON ERROR GOTO 0 → disable
        advance(p);
        ASTNode* node = ast_on_error(line, "", 0, 1);
        program_add_stmt(p->prog, node);
    } else if (target->kind == TOK_INTEGER_LIT || target->kind == TOK_LONG_LIT) {
        int32_t lineno = (target->kind == TOK_INTEGER_LIT)
                         ? target->value.int_val : target->value.long_val;
        advance(p);
        ASTNode* node = ast_on_error(line, "", lineno, 0);
        program_add_stmt(p->prog, node);
    } else if (is_identifier(target->kind)) {
        char label[42];
        strncpy(label, target->value.str.text, 41);
        advance(p);
        ASTNode* node = ast_on_error(line, label, -1, 0);
        program_add_stmt(p->prog, node);
    } else {
        fb_syntax_error(line, target->col,
                        "Expected label, line number, or 0 after ON ERROR GOTO");
    }
}
```

### 2.4 Execute ON ERROR GOTO

```c
static void exec_on_error(Interpreter* interp, ASTNode* node) {
    if (node->data.on_error.disable) {
        interp->on_error_active = 0;
        interp->on_error_target = -1;
        return;
    }

    int target;
    if (node->data.on_error.lineno >= 0) {
        target = program_find_lineno(interp->prog, node->data.on_error.lineno);
    } else {
        target = program_find_label(interp->prog, node->data.on_error.label);
    }

    if (target < 0) {
        fb_error(FB_ERR_UNDEFINED_LABEL, node->line,
                 "ON ERROR GOTO target not found");
        return;
    }

    interp->on_error_target = target;
    interp->on_error_active = 1;
    interp->in_error_handler = 0;
}
```

---

## 3. RESUME Statement

### 3.1 Syntax

```basic
RESUME            ' Retry the statement that caused the error
RESUME NEXT       ' Skip to the statement after the one that caused the error
RESUME label      ' Jump to a specific label
RESUME linenumber ' Jump to a specific line number
```

### 3.2 AST Node

```c
// AST_RESUME
typedef enum {
    RESUME_RETRY,    // RESUME (no argument)
    RESUME_NEXT,     // RESUME NEXT
    RESUME_LABEL     // RESUME label/linenumber
} ResumeKind;

struct {
    ResumeKind kind;
    char       label[42];
    int32_t    lineno;    // -1 for label, >= 0 for line number
} resume;
```

### 3.3 Execute RESUME

```c
static void exec_resume(Interpreter* interp, ASTNode* node) {
    if (!interp->in_error_handler) {
        fb_runtime_error(interp, FB_ERR_RESUME_WITHOUT_ERROR,
                         node->line, NULL);
        return;
    }

    interp->in_error_handler = 0;
    interp->err_code = 0;

    switch (node->data.resume.kind) {
        case RESUME_RETRY:
            // Re-execute the statement that caused the error
            interp->pc = interp->resume_pc;
            break;

        case RESUME_NEXT:
            // Skip past the failing statement
            interp->pc = interp->resume_next_pc;
            break;

        case RESUME_LABEL: {
            int target;
            if (node->data.resume.lineno >= 0) {
                target = program_find_lineno(interp->prog,
                             node->data.resume.lineno);
            } else {
                target = program_find_label(interp->prog,
                             node->data.resume.label);
            }
            if (target < 0) {
                fb_runtime_error(interp, FB_ERR_UNDEFINED_LABEL,
                                 node->line, NULL);
                return;
            }
            interp->pc = target;
            break;
        }
    }
}
```

---

## 4. ERR and ERL

### 4.1 As Built-in Functions/Variables

ERR and ERL are special system variables, not true functions.

```c
// In eval_expr, handle AST_VARIABLE for "ERR" and "ERL":
if (_stricmp(name, "ERR") == 0) {
    return fbval_int((int16_t)interp->err_code);
}
if (_stricmp(name, "ERL") == 0) {
    return fbval_int((int16_t)interp->err_line);
}
```

---

## 5. ERROR Statement

### 5.1 Syntax

```basic
ERROR errcode
```

Simulates a runtime error. Triggers the ON ERROR handler if active.

```c
static void exec_error_stmt(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.error_stmt.code);
    int code = (int)fbval_to_long(&val);
    fbval_release(&val);

    // Trigger as if it were a real runtime error
    fb_runtime_error(interp, (FBErrorCode)code, node->line, "User ERROR");
}
```

---

## 6. Complete Error Code Table (`src/error.c`)

### 6.1 All ~75 FB Error Codes

```c
static const struct {
    int         code;
    const char* message;
} error_messages[] = {
    {  1, "NEXT without FOR" },
    {  2, "Syntax error" },
    {  3, "RETURN without GOSUB" },
    {  4, "Out of DATA" },
    {  5, "Illegal function call" },
    {  6, "Overflow" },
    {  7, "Out of memory" },
    {  8, "Label not defined" },
    {  9, "Subscript out of range" },
    { 10, "Duplicate definition" },
    { 11, "Division by zero" },
    { 12, "Illegal in direct mode" },
    { 13, "Type mismatch" },
    { 14, "Out of string space" },
    { 16, "String formula too complex" },
    { 17, "Cannot continue" },
    { 18, "Function not defined" },
    { 19, "No RESUME" },
    { 20, "RESUME without error" },
    { 24, "Device timeout" },
    { 25, "Device fault" },
    { 26, "FOR without NEXT" },
    { 27, "Out of paper" },
    { 29, "WHILE without WEND" },
    { 30, "WEND without WHILE" },
    { 33, "Duplicate label" },
    { 35, "Subprogram not defined" },
    { 37, "Argument-count mismatch" },
    { 38, "Array not defined" },
    { 40, "Variable required" },
    { 50, "FIELD overflow" },
    { 51, "Internal error" },
    { 52, "Bad file name or number" },
    { 53, "File not found" },
    { 54, "Bad file mode" },
    { 55, "File already open" },
    { 56, "FIELD statement active" },
    { 57, "Device I/O error" },
    { 58, "File already exists" },
    { 59, "Bad record length" },
    { 61, "Disk full" },
    { 62, "Input past end of file" },
    { 63, "Bad record number" },
    { 64, "Bad file name" },
    { 67, "Too many files" },
    { 68, "Device unavailable" },
    { 69, "Communication-buffer overflow" },
    { 70, "Permission denied" },
    { 71, "Disk not ready" },
    { 72, "Disk-media error" },
    { 73, "Feature unavailable" },
    { 74, "Rename across disks" },
    { 75, "Path/File access error" },
    { 76, "Path not found" },
    { 0, NULL }
};

const char* fb_error_message(FBErrorCode code) {
    for (int i = 0; error_messages[i].message; i++) {
        if (error_messages[i].code == (int)code) {
            return error_messages[i].message;
        }
    }
    return "Unprintable error";
}
```

---

## 7. TRON / TROFF

### 7.1 Line Number Tracing

TRON enables line tracing — before each statement executes, the source line number is printed in brackets.

```c
static void exec_tron(Interpreter* interp, ASTNode* node) {
    (void)node;
    interp->tron_active = 1;
}

static void exec_troff(Interpreter* interp, ASTNode* node) {
    (void)node;
    interp->tron_active = 0;
}

// In main execution loop, before each statement:
void interp_run(Interpreter* interp) {
    interp->pc = 0;
    interp->running = 1;

    while (interp->running && interp->pc < interp->prog->stmt_count) {
        ASTNode* stmt = interp->prog->statements[interp->pc];

        // TRON tracing
        if (interp->tron_active) {
            printf("[%d]", stmt->line);
        }

        int old_pc = interp->pc;
        exec_statement(interp, stmt);
        if (interp->pc == old_pc) interp->pc++;
    }
}
```

---

## 8. setjmp/longjmp Integration

### 8.1 Error Handler Jump

The main execution loop is wrapped with setjmp. When a runtime error triggers ON ERROR, longjmp transfers control to the error handler location.

```c
void interp_run(Interpreter* interp) {
    interp->pc = 0;
    interp->running = 1;

    // Set up error recovery point
    if (setjmp(interp->error_jmp) != 0) {
        // We got here via longjmp from fb_runtime_error
        // pc has been set to on_error_target by the error dispatch
        interp->pc = interp->on_error_target;
    }
    interp->error_jmp_valid = 1;

    while (interp->running && interp->pc < interp->prog->stmt_count) {
        ASTNode* stmt = interp->prog->statements[interp->pc];

        if (interp->tron_active) {
            printf("[%d]", stmt->line);
        }

        int old_pc = interp->pc;
        exec_statement(interp, stmt);
        if (interp->pc == old_pc) interp->pc++;
    }

    interp->error_jmp_valid = 0;
}
```

### 8.2 Error Handler in Procedure Calls

When an error occurs inside a SUB/FUNCTION and no local ON ERROR is active, FB searches up the call chain for an error handler. The interpreter implements this by:

1. Checking each frame in the call stack for an active ON ERROR handler
2. Unwinding the call stack to that frame
3. Jumping to the handler

```c
void fb_runtime_error(Interpreter* interp, FBErrorCode code,
                      int line, const char* extra) {
    // Search for an error handler in the call chain
    if (interp->on_error_active && !interp->in_error_handler) {
        interp->err_code = code;
        interp->err_line = line;
        interp->resume_pc = interp->pc;
        interp->resume_next_pc = interp->pc + 1;
        interp->in_error_handler = 1;

        // Unwind call stack if needed (error in SUB/FUNCTION)
        // ON ERROR handlers are module-level only in FB
        while (interp->call_stack.sp > 0) {
            callstack_pop(&interp->call_stack);
        }
        interp->current_scope = interp->global_scope;

        longjmp(interp->error_jmp, 1);
    }

    // No handler — fatal
    fprintf(stderr, "Unhandled error %d: %s in line %d\n",
            code, fb_error_message(code), line);
    if (extra) fprintf(stderr, "  %s\n", extra);
    interp->running = 0;
}
```

---

## 9. Verification Test Files

### 9.1 `tests/verify/phase6_on_error.bas`

```basic
REM Phase 6 Test: ON ERROR GOTO and RESUME
ON ERROR GOTO ErrHandler
PRINT "Before error"
ERROR 5
PRINT "This should not print"

ErrHandler:
PRINT "Error caught:"; ERR
RESUME NEXT

' Test RESUME to label
ON ERROR GOTO ErrHandler2
PRINT "About to divide by zero"
DIM x%
x% = 1 \ 0
PRINT "After division"
GOTO Done

ErrHandler2:
PRINT "Division error:"; ERR
RESUME AfterDiv

AfterDiv:
PRINT "Resumed after division"

Done:
' Disable error handling
ON ERROR GOTO 0
PRINT "Done"
END
```

**Expected output (`tests/verify/phase6_expected/on_error.txt`):**
```
Before error
Error caught: 5
About to divide by zero
Division error: 11
Resumed after division
Done
```

### 9.2 `tests/verify/phase6_err_erl.bas`

```basic
REM Phase 6 Test: ERR and ERL
ON ERROR GOTO Handler
10 x% = 1 / 0
20 PRINT "After error"
GOTO Done

Handler:
PRINT "Error"; ERR; "at line"; ERL
RESUME NEXT

Done:
PRINT "Done"
```

**Expected output (`tests/verify/phase6_expected/err_erl.txt`):**
```
Error 11 at line 10
After error
Done
```

### 9.3 `tests/verify/phase6_robust.bas` — Milestone

```basic
REM Robust file handler - Phase 6 Milestone
ON ERROR GOTO FileError
OPEN "nonexistent_file_12345.txt" FOR INPUT AS #1
PRINT "File opened OK"
GOTO Done

FileError:
PRINT "File error"; ERR; ": ";
SELECT CASE ERR
    CASE 53
        PRINT "File not found"
    CASE 55
        PRINT "File already open"
    CASE ELSE
        PRINT "Unknown error"
END SELECT
RESUME Done

Done:
PRINT "Program completed gracefully"
```

**Expected output (`tests/verify/phase6_expected/robust.txt`):**
```
File error 53 : File not found
Program completed gracefully
```

---

## 10. Phase 6 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **ON ERROR GOTO** | Registers error handler at specified label/line. Traps runtime errors. `ON ERROR GOTO 0` disables. |
| 2 | **RESUME** | `RESUME` retries failing statement. `RESUME NEXT` skips past it. `RESUME label` jumps to label. |
| 3 | **ERR / ERL** | Return correct error code and line number after trapped error. Reset after RESUME. |
| 4 | **ERROR n** | Simulates any error code. Triggers ON ERROR handler if active. |
| 5 | **Error code table** | All ~75 standard FB error codes defined with human-readable messages. |
| 6 | **Call chain search** | Error in SUB/FUNCTION without local handler → search up call chain to module-level handler. Call stack unwound. |
| 7 | **No nested errors** | Error inside error handler → fatal (print and terminate). |
| 8 | **TRON / TROFF** | `[linenum]` printed before each statement when TRON active. TROFF disables. |
| 9 | **setjmp/longjmp** | Error dispatch via `longjmp` works correctly. Stack not corrupted. Local variables in scope after longjmp. |
| 10 | **Milestone** | Robust file handler catches file-not-found, recovers, and continues. |
| 11 | **No Leaks** | Error recovery path doesn't leak memory. Call frame cleanup on unwind. |

---

## 11. Key Implementation Warnings

1. **setjmp and local variables:** After `longjmp`, only variables declared `volatile` retain their values (per C standard). Use `volatile` for any locals in `interp_run()` that must survive the longjmp. Alternatively, store all critical state in the `Interpreter` struct (heap-allocated).

2. **RESUME without RESUME:** If the error handler reaches the end of the program or falls through without executing RESUME, FB error 19 ("No RESUME") is raised. Detect this: if execution returns to the main loop from the handler without clearing `in_error_handler`, trigger error 19.

3. **ON ERROR GOTO 0 inside handler:** If executed inside an error handler, it re-raises the current error as a fatal error. The handler effectively says "I can't handle this."

4. **ERROR n with custom codes:** FB allows ERROR with codes not in the standard table. The handler receives the code; `fb_error_message()` returns "Unprintable error" for unknown codes.

5. **ERL only works with line numbers:** ERL returns the line number (from source) of the failing statement. If the program doesn't use line numbers, ERL returns 0. It returns the line number label, not the internal statement index.

6. **RESUME inside a procedure:** After RESUME, execution continues at the specified point. If the error occurred in a SUB/FUNCTION and the handler is at module level, RESUME NEXT skips the entire CALL statement (not the failing statement inside the SUB).
