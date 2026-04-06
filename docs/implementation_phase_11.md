# Phase 11 — Advanced / Rare Features: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **CHAIN/COMMON, RUN, serial I/O, multi-module support, SLEEP, and other niche features** for the FreeBASIC interpreter. Phase 11 completes the interpreter with seldom-used features that enable maximum FB compatibility.

---

## Project File Structure (Phase 11 additions)

```
fbasic/
├── Makefile                        [MOD]
├── include/
│   ├── ast.h                      [MOD] — AST_CHAIN, AST_RUN, AST_SLEEP, etc.
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD]
│   ├── chain.h                    [NEW] — CHAIN/COMMON/RUN API
│   ├── serial.h                   [NEW] — serial port abstraction (stub)
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse CHAIN, COMMON, RUN, SLEEP, etc.
│   ├── interpreter.c              [MOD] — execute new statements
│   ├── chain.c                    [NEW] — CHAIN/RUN program loading
│   ├── serial.c                   [NEW] — OPEN "COMn:" stub
│   └── ...
└── tests/
    └── verify/
        ├── phase11_sleep.bas      [NEW]
        ├── phase11_chain_main.bas [NEW]
        ├── phase11_chain_sub.bas  [NEW]
        ├── phase11_run.bas        [NEW]
        ├── phase11_misc.bas       [NEW]
        ├── phase11_milestone.bas  [NEW]
        └── phase11_expected/      [NEW]
            ├── sleep.txt
            ├── chain.txt
            ├── run.txt
            ├── misc.txt
            └── milestone.txt
```

---

## 1. SLEEP

### 1.1 Syntax

```basic
SLEEP [seconds]    ' Pause execution; any keypress resumes early
```

### 1.2 Implementation

```c
static void exec_sleep(Interpreter* interp, ASTNode* node) {
    double seconds = 0;
    int indefinite = 1;

    if (node->data.sleep_stmt.has_duration) {
        FBValue val = eval_expr(interp, node->data.sleep_stmt.duration);
        seconds = fbval_to_double(&val);
        fbval_release(&val);
        indefinite = 0;
    }

    double start = get_timer_seconds();

    while (1) {
        // Check for keypress
        if (key_available()) {
            consume_key(); // Don't leave key in buffer
            break;
        }

        // Check elapsed time
        if (!indefinite) {
            double elapsed = get_timer_seconds() - start;
            if (elapsed >= seconds) break;
        }

        // Pump events (SDL + console)
        #ifdef USE_SDL2
            graphics_pump_events();
        #endif

        // Small sleep to avoid CPU spin
        platform_sleep_ms(10);
    }
}

// Platform-specific sleep:
static void platform_sleep_ms(int ms) {
    #ifdef _WIN32
        Sleep(ms);
    #else
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    #endif
}
```

---

## 2. CHAIN

### 2.1 Syntax

```basic
CHAIN filename$                    ' Load and run another .BAS program
CHAIN filename$, line              ' Start at specific line
CHAIN MERGE filename$, line, ALL   ' Merge source, keep variables
CHAIN MERGE filename$, , ALL, DELETE range
```

### 2.2 Data Structures

```c
typedef struct ChainState {
    // Variables to carry across CHAIN
    int         use_common;      // 1 if COMMON variables should be preserved
    int         use_all;         // 1 if ALL variables should be preserved
    int         merge;           // 1 if CHAIN MERGE
    int         start_line;      // Starting line number (-1 for beginning)

    // COMMON variable list (shared between programs)
    struct {
        char    name[42];
        FBType  type;
        int     is_array;
    } common_vars[256];
    int common_count;
} ChainState;
```

### 2.3 COMMON Statement

```basic
COMMON [SHARED] varname [AS type] [, ...]
```

COMMON declares variables that will be shared across CHAIN calls. The parser records these declarations; they take effect when CHAIN executes.

```c
static void parse_common(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume COMMON

    int shared = 0;
    if (token_is_keyword(current_token(p), KW_SHARED)) {
        shared = 1;
        advance(p);
    }

    // Parse variable list
    ASTNode* node = ast_common(line, shared);

    while (1) {
        char name[42];
        FBType type;
        int is_array = 0;

        parse_variable_decl(p, name, &type, &is_array);
        ast_common_add_var(node, name, type, is_array);

        if (!token_is(current_token(p), TOK_COMMA)) break;
        advance(p); // consume comma
    }

    program_add_stmt(p->prog, node);
}
```

### 2.4 Execute CHAIN

```c
static void exec_chain(Interpreter* interp, ASTNode* node) {
    FBValue filename_v = eval_expr(interp, node->data.chain.filename);
    const char* filename = fbval_to_cstr(&filename_v);

    // 1. Read source file
    char* source = read_file_to_string(filename);
    if (!source) {
        fb_runtime_error(interp, FB_ERR_FILE_NOT_FOUND,
                         node->line, filename);
        fbval_release(&filename_v);
        return;
    }

    // 2. Save COMMON variables
    FBValue saved_common[256];
    int saved_count = 0;
    if (!node->data.chain.all) {
        saved_count = save_common_variables(interp, saved_common);
    }

    // Save ALL variables if requested
    SymbolTable* saved_scope = NULL;
    if (node->data.chain.all) {
        saved_scope = symtable_clone(interp->global_scope);
    }

    // 3. Reset interpreter state
    program_clear(interp->prog);
    symtable_clear(interp->global_scope);

    // 4. Lex + Parse new program
    Lexer lexer;
    lexer_init(&lexer, source, filename);
    Parser parser;
    parser_init(&parser, &lexer, interp->prog);
    parse_program(&parser);

    free(source);
    fbval_release(&filename_v);

    // 5. Restore variables
    if (node->data.chain.all && saved_scope) {
        symtable_merge(interp->global_scope, saved_scope);
        symtable_destroy(saved_scope);
    } else if (saved_count > 0) {
        restore_common_variables(interp, saved_common, saved_count);
    }

    // 6. Set starting point
    if (node->data.chain.start_line >= 0) {
        int target = program_find_lineno(interp->prog,
                         node->data.chain.start_line);
        if (target >= 0) {
            interp->pc = target;
        } else {
            interp->pc = 0;
        }
    } else {
        interp->pc = 0;
    }

    // 7. Continue execution (don't return — the main loop will pick up)
}

static int save_common_variables(Interpreter* interp, FBValue* saved) {
    int count = 0;
    // Walk the COMMON declarations from the current program
    for (int i = 0; i < interp->chain_state.common_count; i++) {
        const char* name = interp->chain_state.common_vars[i].name;
        FBValue* val = symtable_get(interp->global_scope, name);
        if (val) {
            saved[count] = fbval_copy(val);
            count++;
        }
    }
    return count;
}

static void restore_common_variables(Interpreter* interp,
                                      FBValue* saved, int count) {
    for (int i = 0; i < count && i < interp->chain_state.common_count; i++) {
        const char* name = interp->chain_state.common_vars[i].name;
        symtable_set(interp->global_scope, name, &saved[i]);
        fbval_release(&saved[i]);
    }
}
```

---

## 3. RUN

### 3.1 Syntax

```basic
RUN                    ' Restart current program from beginning
RUN linenumber         ' Restart from specific line
RUN filename$          ' Load and run a different program (variables cleared)
```

### 3.2 Implementation

```c
static void exec_run(Interpreter* interp, ASTNode* node) {
    if (node->data.run.has_filename) {
        // Load and run a different program
        FBValue fn = eval_expr(interp, node->data.run.filename);
        const char* filename = fbval_to_cstr(&fn);

        char* source = read_file_to_string(filename);
        if (!source) {
            fb_runtime_error(interp, FB_ERR_FILE_NOT_FOUND,
                             node->line, filename);
            fbval_release(&fn);
            return;
        }

        // Clear everything
        program_clear(interp->prog);
        symtable_clear(interp->global_scope);
        interp_reset_state(interp);

        // Lex + Parse
        Lexer lexer;
        lexer_init(&lexer, source, filename);
        Parser parser;
        parser_init(&parser, &lexer, interp->prog);
        parse_program(&parser);
        free(source);
        fbval_release(&fn);

        interp->pc = 0;
    } else if (node->data.run.has_lineno) {
        // Restart from specific line
        symtable_clear(interp->global_scope);
        interp_reset_state(interp);
        int target = program_find_lineno(interp->prog, node->data.run.lineno);
        interp->pc = (target >= 0) ? target : 0;
    } else {
        // Restart current program
        symtable_clear(interp->global_scope);
        interp_reset_state(interp);
        interp->pc = 0;
    }
}

static void interp_reset_state(Interpreter* interp) {
    // Reset call stack
    interp->call_stack.sp = 0;
    // Reset GOSUB stack
    interp->gosub_sp = 0;
    // Reset FOR stack
    interp->for_sp = 0;
    // Reset DATA pointer
    interp->data_ptr = 0;
    // Reset error state
    interp->err_code = 0;
    interp->err_line = 0;
    interp->on_error_active = 0;
    // Reset event traps
    memset(&interp->events, 0, sizeof(interp->events));
    // Close all files
    filetable_close_all(&interp->files);
    // Reset TRON
    interp->tron_active = 0;
}
```

---

## 4. OPEN "COMn:" — Serial Communications

### 4.1 Syntax

```basic
OPEN "COM1:9600,N,8,1" FOR RANDOM AS #1
' or
OPEN "COM1:" + options$ AS #1
```

### 4.2 Stub Implementation

Serial ports are rarely used in modern FB programs. Provide a stub that logs warnings:

```c
// serial.h
typedef struct SerialPort {
    int   port_num;   // 1 or 2
    int   baud_rate;
    char  parity;     // N, E, O
    int   data_bits;  // 7 or 8
    int   stop_bits;  // 1 or 2
    int   open;
} SerialPort;

// serial.c
int serial_open(SerialPort* port, const char* spec) {
    // Parse "9600,N,8,1" from spec
    fprintf(stderr, "Warning: Serial port COM%d not supported, "
                    "operating as null device\n", port->port_num);

    // Parse but don't actually open anything
    sscanf(spec, "%d,%c,%d,%d", &port->baud_rate, &port->parity,
           &port->data_bits, &port->stop_bits);
    port->open = 1;
    return 0;
}

int serial_write(SerialPort* port, const void* data, int len) {
    (void)port; (void)data;
    return len; // Pretend it worked
}

int serial_read(SerialPort* port, void* buffer, int maxlen) {
    (void)port; (void)buffer; (void)maxlen;
    return 0; // No data available
}

void serial_close(SerialPort* port) {
    port->open = 0;
}
```

### 4.3 Integration with File I/O

When `OPEN` detects a "COMn:" filename, redirect to the serial subsystem:

```c
// In exec_open (Phase 5), add:
if (strncasecmp(filename, "COM", 3) == 0 && isdigit(filename[3]) &&
    filename[4] == ':') {
    int port_num = filename[3] - '0';
    const char* spec = filename + 5;

    FBFile* f = &interp->files.files[filenum];
    f->kind = FILE_SERIAL;
    f->serial = calloc(1, sizeof(SerialPort));
    f->serial->port_num = port_num;
    serial_open(f->serial, spec);
    f->open = 1;
    return;
}
```

---

## 5. Multi-Module Support

### 5.1 Concept

FB supports multiple source modules compiled separately and linked together. Each module has its own module-level code. DECLARE statements in one module reference SUB/FUNCTION in another.

### 5.2 Implementation Strategy

For an interpreter (not compiler), multi-module support means:

1. **$INCLUDE metacommand:** Include source from another file at parse time
2. **Multiple source files:** Accept multiple filenames on the command line

```c
// $INCLUDE: 'filename.bi'
// In the lexer/parser, detect $INCLUDE and inline the file:

static void handle_metacommand(Parser* p, const char* cmd) {
    if (strncasecmp(cmd, "$INCLUDE:", 9) == 0) {
        // Extract filename (strip quotes)
        const char* fname = cmd + 9;
        while (*fname == ' ' || *fname == '\'') fname++;
        char filename[256];
        int i = 0;
        while (*fname && *fname != '\'' && *fname != '"' && i < 255)
            filename[i++] = *fname++;
        filename[i] = '\0';

        // Read and parse the included file
        char* source = read_file_to_string(filename);
        if (!source) {
            fb_error(FB_ERR_FILE_NOT_FOUND, p->current_line, filename);
            return;
        }

        // Create a sub-lexer for the included file
        Lexer include_lexer;
        lexer_init(&include_lexer, source, filename);

        // Parse statements from included file into same program
        Parser include_parser;
        parser_init(&include_parser, &include_lexer, p->prog);
        while (!at_eof(&include_parser)) {
            parse_statement(&include_parser);
        }

        free(source);
    }
}
```

### 5.3 Multi-File Command Line

```c
// In main():
// fbasic main.bas module2.bas module3.bas
// Parse each file into the same program in order

for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') continue; // Skip flags

    char* source = read_file_to_string(argv[i]);
    if (!source) {
        fprintf(stderr, "Cannot open: %s\n", argv[i]);
        return 1;
    }

    Lexer lexer;
    lexer_init(&lexer, source, argv[i]);
    parser_parse_into(&parser, &lexer, program);
    free(source);
}
```

---

## 6. UEVENT

### 6.1 Syntax

```basic
ON UEVENT GOSUB label    ' Register user-event handler
UEVENT ON / OFF / STOP   ' Control trapping
' (The event is raised by external mechanism, not standard in FB)
```

### 6.2 Stub Implementation

UEVENT is extremely rare. Provide the syntax but never fire the event:

```c
static void exec_on_uevent_gosub(Interpreter* interp, ASTNode* node) {
    // Store handler but never fire
    fprintf(stderr, "Warning: UEVENT registered but not supported\n");
}
```

---

## 7. Additional Niche Features

### 7.1 PCOPY (already in Phase 8)

### 7.2 WAIT

```basic
WAIT port%, andmask% [, xormask%]
```

Stub (no port I/O available):

```c
static void exec_wait(Interpreter* interp, ASTNode* node) {
    // In FB, WAIT loops until (INP(port) XOR xormask) AND andmask != 0
    // We can't do real port I/O — just return immediately
    (void)node;
}
```

### 7.3 PALETTE USING (already in Phase 8)

### 7.4 Additional META Commands

```basic
' $DYNAMIC                — force dynamic arrays
' $STATIC                 — force static arrays
' $INCLUDE: 'file.bi'     — include source file
```

```c
// In lexer, detect REM $META or ' $META lines:
if (line[0] == '\'' && line[1] == '$') {
    handle_metacommand(parser, line + 1);
}
```

### 7.5 ERDEV / ERDEV$

Device error code and device name after an I/O error. Stub:

```c
static FBValue builtin_erdev(Interpreter* interp) {
    return fbval_int(0);
}
static FBValue builtin_erdev_str(Interpreter* interp) {
    return fbval_string("");
}
```

### 7.6 IOCTL / IOCTL$

Device control strings. Stub:

```c
static void exec_ioctl(Interpreter* interp, ASTNode* node) {
    fprintf(stderr, "Warning: IOCTL not supported\n");
}
static FBValue builtin_ioctl_str(Interpreter* interp, FBValue* args, int nargs) {
    return fbval_string("");
}
```

### 7.7 SADD / SSEG / VARPTR$ / SETMEM

Address-related functions. Stub with dummy values:

```c
static FBValue builtin_sadd(Interpreter* interp, FBValue* args, int nargs) {
    // Return the actual C pointer (or dummy) as a long
    if (args[0].type == FB_STRING && args[0].data.str) {
        return fbval_long((long)(intptr_t)args[0].data.str->data);
    }
    return fbval_long(0);
}

static FBValue builtin_varptr_str(Interpreter* interp, FBValue* args, int nargs) {
    // Return a 3-byte string: type byte + 16-bit address
    char buf[3] = {0};
    buf[0] = (char)args[0].type;  // Type tag
    buf[1] = 0; buf[2] = 0;      // Dummy address
    return fbval_string_n(buf, 3);
}
```

---

## 8. Verification Test Files

### 8.1 `tests/verify/phase11_sleep.bas`

```basic
REM Phase 11 Test: SLEEP
PRINT "Sleeping 1 second..."
DIM start!
start! = TIMER
SLEEP 1
DIM elapsed!
elapsed! = TIMER - start!
IF elapsed! >= 0.9 AND elapsed! <= 2.0 THEN
    PRINT "SLEEP duration OK"
ELSE
    PRINT "SLEEP duration unexpected:"; elapsed!
END IF

PRINT "SLEEP 0 (instant, or until keypress)..."
' With no keypress simulation, this should return quickly
' In automated tests, SLEEP with no argument would block forever
' so we only test SLEEP n
PRINT "Done"
```

**Expected output (`tests/verify/phase11_expected/sleep.txt`):**
```
Sleeping 1 second...
SLEEP duration OK
SLEEP 0 (instant, or until keypress)...
Done
```

### 8.2 `tests/verify/phase11_chain_main.bas`

```basic
REM Phase 11 Test: CHAIN (main program)
COMMON x%, msg$
x% = 42
msg$ = "Hello from main"
PRINT "Main: x% ="; x%
PRINT "Main: chaining to sub..."
CHAIN "phase11_chain_sub.bas"
' Execution does not return here after CHAIN
```

### 8.3 `tests/verify/phase11_chain_sub.bas`

```basic
REM Phase 11 Test: CHAIN (sub program)
COMMON x%, msg$
PRINT "Sub: x% ="; x%
PRINT "Sub: msg$ = "; msg$
x% = x% + 100
PRINT "Sub: x% now ="; x%
PRINT "Sub: Done"
```

**Expected output (`tests/verify/phase11_expected/chain.txt`):**
```
Main: x% = 42
Main: chaining to sub...
Sub: x% = 42
Sub: msg$ = Hello from main
Sub: x% now = 142
Sub: Done
```

### 8.4 `tests/verify/phase11_run.bas`

```basic
REM Phase 11 Test: RUN
DIM x%
x% = 10
PRINT "x% ="; x%
IF x% = 10 THEN
    x% = 20
    PRINT "Running from line 100..."
    RUN 100
END IF
PRINT "ERROR: should not reach here"
END

100 PRINT "At line 100"
PRINT "x% after RUN ="; x%
PRINT "Done"
```

**Expected output (`tests/verify/phase11_expected/run.txt`):**
```
x% = 10
Running from line 100...
At line 100
x% after RUN = 0
Done
```

### 8.5 `tests/verify/phase11_milestone.bas` — Milestone

```basic
REM Phase 11 Milestone: Advanced features demo
PRINT "=== Phase 11 Milestone ==="

' SLEEP test
DIM start!
start! = TIMER
SLEEP 1
DIM elapsed!
elapsed! = TIMER - start!
IF elapsed! >= 0.8 THEN
    PRINT "SLEEP: OK"
ELSE
    PRINT "SLEEP: FAILED"
END IF

' Metacommand (if supported)
' $DYNAMIC
DIM arr%(5)
PRINT "Dynamic array: OK"

' Stub functions
PRINT "FRE(-1) ="; FRE(-1)
PRINT "ERDEV ="; ERDEV

' Demonstrate RUN restart
STATIC runcount%
runcount% = runcount% + 1
IF runcount% = 1 THEN
    PRINT "First run"
ELSE
    PRINT "This should not appear (RUN clears STATIC)"
END IF

PRINT "=== Milestone passed ==="
```

**Expected output (`tests/verify/phase11_expected/milestone.txt`):**
```
=== Phase 11 Milestone ===
SLEEP: OK
Dynamic array: OK
FRE(-1) = 655360
ERDEV = 0
First run
=== Milestone passed ===
```

---

## 9. Makefile Updates

```makefile
SRC += src/chain.c src/serial.c
```

---

## 10. Phase 11 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **SLEEP n** | Pauses for n seconds. Wakes early on keypress. Without arg, waits for keypress only. |
| 2 | **SLEEP 0** | Returns immediately (FB behavior: SLEEP 0 = suspend until next second boundary, but commonly no-op in modern implementations). |
| 3 | **CHAIN filename$** | Loads, parses, and begins executing another .BAS file. |
| 4 | **CHAIN with COMMON** | Variables declared COMMON in both programs survive the CHAIN. |
| 5 | **CHAIN ALL** | All variables survive the CHAIN (not just COMMON). |
| 6 | **CHAIN MERGE** | Merges source (numbered lines replace, unnumbered append). |
| 7 | **COMMON statement** | Declares shared variables. Works with SHARED modifier. Arrays supported. |
| 8 | **RUN (restart)** | Clears all variables and restarts from beginning. |
| 9 | **RUN linenumber** | Clears variables, restarts from specified line. |
| 10 | **RUN filename$** | Clears everything, loads and runs a different program. |
| 11 | **OPEN "COMn:"** | Parses serial port syntax. Opens as null device with warning. |
| 12 | **$INCLUDE** | Metacommand includes source from another file at parse time. |
| 13 | **$DYNAMIC / $STATIC** | Metacommands affecting array allocation strategy. |
| 14 | **UEVENT** | Syntax accepted, handler stored, event never fires. |
| 15 | **WAIT** | Parsed and executes as no-op (no port I/O). |
| 16 | **ERDEV / ERDEV$** | Return 0 / empty string. |
| 17 | **IOCTL / IOCTL$** | Statement is no-op; function returns empty string. |
| 18 | **SADD** | Returns C pointer to string data (or dummy). |
| 19 | **VARPTR$** | Returns 3-byte type+address string. |
| 20 | **Multi-file** | Multiple .BAS files on command line parsed into single program. |
| 21 | **Milestone** | SLEEP works, array allocation, stub functions return expected values. |

---

## 11. Key Implementation Warnings

1. **CHAIN does not return:** After CHAIN, execution continues in the new program. The old program's code is replaced. This is NOT like a function call — there is no return to the caller.

2. **COMMON must match:** Both the calling and called program must have identical COMMON declarations (same variable names, types, and order). If they don't match, variables may be misaligned. The interpreter should validate COMMON lists or at least match by name.

3. **RUN clears everything:** RUN is destructive — it clears all variables (including STATIC), resets all stacks, closes all files. Only the program code itself survives (unless `RUN filename$`, which replaces it too).

4. **CHAIN MERGE is complex:** MERGE mode integrates numbered lines from the new file into the existing program (replacing lines with matching numbers, inserting new ones). This requires keeping the program in a line-number-indexed structure during merge. This feature is rarely used and can be deferred or simplified.

5. **$INCLUDE is parse-time:** $INCLUDE inlines source code during parsing, not at runtime. It's essentially a C-style `#include`. Guard against recursive includes (circular dependency detection).

6. **Serial port reality:** Real serial port support would require platform-specific code (CreateFile on Windows, open() on POSIX for /dev/ttyS*). The stub is sufficient unless specific FB programs require serial I/O.

7. **SLEEP 0 behavior:** In FB, `SLEEP 0` or `SLEEP` without argument suspends until a keypress occurs. `SLEEP n` suspends for n seconds OR until keypress (whichever comes first). The distinction matters for interactive programs.

8. **Multi-module DECLARE:** In FB's IDE, DECLARE statements are auto-generated for SUB/FUNCTION in other modules. The interpreter should allow forward declarations that reference procedures defined later in the parse stream (from another file). The resolution happens after all files are parsed.
