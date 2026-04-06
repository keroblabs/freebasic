# Phase 7 — DOS / System Interface: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build the **system-level features** (SHELL, ENVIRON, COMMAND$, directory operations, date/time, memory, and printer output) for the FreeBASIC interpreter. Phase 7 enables environment-aware scripts and system utility programs.

---

## Project File Structure (Phase 7 additions)

```
fbasic/
├── Makefile                        [MOD]
├── include/
│   ├── ast.h                      [MOD] — new statement nodes
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD]
│   ├── system_api.h               [NEW] — cross-platform system wrappers
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse new statements
│   ├── interpreter.c              [MOD] — execute new statements
│   ├── system_api.c               [NEW] — SHELL, ENVIRON, directories, date/time
│   └── ...
└── tests/
    └── verify/
        ├── phase7_shell.bas       [NEW]
        ├── phase7_environ.bas     [NEW]
        ├── phase7_dirs.bas        [NEW]
        ├── phase7_datetime.bas    [NEW]
        ├── phase7_misc.bas        [NEW]
        ├── phase7_milestone.bas   [NEW]
        └── phase7_expected/       [NEW]
            ├── shell.txt
            ├── environ.txt
            ├── dirs.txt
            ├── datetime.txt
            ├── misc.txt
            └── milestone.txt
```

---

## 1. SHELL

### 1.1 Syntax

```basic
SHELL                     ' Open interactive shell
SHELL commandstring$      ' Execute command and return
```

### 1.2 Implementation

```c
static void exec_shell(Interpreter* interp, ASTNode* node) {
    if (node->data.shell.command) {
        FBValue cmd = eval_expr(interp, node->data.shell.command);
        const char* cmdstr = fbval_to_cstr(&cmd);
        int result = system(cmdstr);
        (void)result;  // FB doesn't expose return code
        fbval_release(&cmd);
    } else {
        // Open interactive shell
        #ifdef _WIN32
            system("cmd.exe");
        #else
            const char* sh = getenv("SHELL");
            if (!sh) sh = "/bin/sh";
            system(sh);
        #endif
    }
}
```

---

## 2. ENVIRON / ENVIRON$

### 2.1 Syntax

```basic
ENVIRON "NAME=VALUE"         ' Set environment variable (statement)
v$ = ENVIRON$("NAME")        ' Get environment variable (function)
v$ = ENVIRON$(n%)            ' Get nth environment string (function)
```

### 2.2 Implementation

```c
// ENVIRON statement — set environment variable
static void exec_environ_set(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.environ_stmt.expr);
    const char* str = fbval_to_cstr(&val);

    // Parse "NAME=VALUE"
    const char* eq = strchr(str, '=');
    if (!eq) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "ENVIRON requires NAME=VALUE");
        fbval_release(&val);
        return;
    }

    char name[256];
    int namelen = (int)(eq - str);
    if (namelen >= 256) namelen = 255;
    memcpy(name, str, namelen);
    name[namelen] = '\0';
    const char* value = eq + 1;

    #ifdef _WIN32
        _putenv_s(name, value);
    #else
        setenv(name, value, 1);
    #endif

    fbval_release(&val);
}

// ENVIRON$ function
static FBValue builtin_environ_func(Interpreter* interp, FBValue* args, int nargs) {
    if (nargs != 1) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL, 0, "ENVIRON$");
        return fbval_string("");
    }

    if (args[0].type == FB_STRING) {
        const char* name = args[0].data.str->data;
        const char* val = getenv(name);
        return fbval_string(val ? val : "");
    } else {
        // Numeric argument — get nth environment string
        int n = (int)fbval_to_long(&args[0]);
        #ifdef _WIN32
            // Windows: use _environ
            extern char** _environ;
            int i = 0;
            if (_environ) {
                for (; _environ[i]; i++) {
                    if (i + 1 == n) return fbval_string(_environ[i]);
                }
            }
        #else
            extern char** environ;
            int i = 0;
            if (environ) {
                for (; environ[i]; i++) {
                    if (i + 1 == n) return fbval_string(environ[i]);
                }
            }
        #endif
        return fbval_string("");
    }
}
```

---

## 3. COMMAND$

### 3.1 Syntax

```basic
c$ = COMMAND$     ' Returns command-line arguments as a single string
```

### 3.2 Implementation

Store `argc`/`argv` from `main()` in the interpreter at startup:

```c
// In Interpreter struct:
char* command_line;   // Combined args from argv[1..argc-1]

// At init:
void interp_set_command_line(Interpreter* interp, int argc, char** argv) {
    size_t total = 0;
    for (int i = 1; i < argc; i++) {
        total += strlen(argv[i]) + 1;  // space separator
    }
    interp->command_line = malloc(total + 1);
    interp->command_line[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(interp->command_line, " ");
        strcat(interp->command_line, argv[i]);
    }
    // FB uppercases COMMAND$
    for (char* p = interp->command_line; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
}

// In eval_expr for COMMAND$:
static FBValue builtin_command_func(Interpreter* interp) {
    return fbval_string(interp->command_line ? interp->command_line : "");
}
```

---

## 4. CHDIR / MKDIR / RMDIR

### 4.1 Syntax

```basic
CHDIR path$     ' Change current directory
MKDIR path$     ' Create directory
RMDIR path$     ' Remove directory
```

### 4.2 Implementation

```c
#include <direct.h>   // Windows: _chdir, _mkdir, _rmdir
// #include <unistd.h>  // POSIX: chdir
// #include <sys/stat.h> // POSIX: mkdir

static void exec_chdir(Interpreter* interp, ASTNode* node) {
    FBValue path = eval_expr(interp, node->data.dir_op.path);
    const char* p = fbval_to_cstr(&path);

    #ifdef _WIN32
        int rc = _chdir(p);
    #else
        int rc = chdir(p);
    #endif

    if (rc != 0) {
        fb_runtime_error(interp, FB_ERR_PATH_NOT_FOUND, node->line, p);
    }
    fbval_release(&path);
}

static void exec_mkdir(Interpreter* interp, ASTNode* node) {
    FBValue path = eval_expr(interp, node->data.dir_op.path);
    const char* p = fbval_to_cstr(&path);

    #ifdef _WIN32
        int rc = _mkdir(p);
    #else
        int rc = mkdir(p, 0755);
    #endif

    if (rc != 0) {
        fb_runtime_error(interp, FB_ERR_PATH_FILE_ACCESS, node->line, p);
    }
    fbval_release(&path);
}

static void exec_rmdir(Interpreter* interp, ASTNode* node) {
    FBValue path = eval_expr(interp, node->data.dir_op.path);
    const char* p = fbval_to_cstr(&path);

    #ifdef _WIN32
        int rc = _rmdir(p);
    #else
        int rc = rmdir(p);
    #endif

    if (rc != 0) {
        fb_runtime_error(interp, FB_ERR_PATH_FILE_ACCESS, node->line, p);
    }
    fbval_release(&path);
}
```

---

## 5. DATE$ / TIME$ / TIMER

### 5.1 Syntax

```basic
d$ = DATE$           ' Returns "MM-DD-YYYY"
DATE$ = "MM-DD-YYYY" ' Set date (stub or real)
t$ = TIME$           ' Returns "HH:MM:SS"
TIME$ = "HH:MM:SS"   ' Set time (stub or real)
v! = TIMER           ' Seconds since midnight (single precision)
```

### 5.2 Implementation

```c
#include <time.h>

static FBValue builtin_date_func(Interpreter* interp) {
    (void)interp;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char buf[11];
    snprintf(buf, sizeof(buf), "%02d-%02d-%04d",
             t->tm_mon + 1, t->tm_mday, t->tm_year + 1900);
    return fbval_string(buf);
}

static FBValue builtin_time_func(Interpreter* interp) {
    (void)interp;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             t->tm_hour, t->tm_min, t->tm_sec);
    return fbval_string(buf);
}

static FBValue builtin_timer_func(Interpreter* interp) {
    (void)interp;
    #ifdef _WIN32
        SYSTEMTIME st;
        GetLocalTime(&st);
        double secs = st.wHour * 3600.0 + st.wMinute * 60.0 +
                      st.wSecond + st.wMilliseconds / 1000.0;
        return fbval_single((float)secs);
    #else
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm* t = localtime(&ts.tv_sec);
        double secs = t->tm_hour * 3600.0 + t->tm_min * 60.0 +
                      t->tm_sec + ts.tv_nsec / 1e9;
        return fbval_single((float)secs);
    #endif
}
```

### 5.3 DATE$ / TIME$ as L-values

When DATE$ or TIME$ appear on the left side of assignment, they set the system clock. Since modern OSes require privileges, implement as stubs:

```c
static void exec_date_set(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.assign.rhs);
    // Stub: print warning
    fprintf(stderr, "Warning: DATE$ assignment not supported on this platform\n");
    fbval_release(&val);
}

static void exec_time_set(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.assign.rhs);
    fprintf(stderr, "Warning: TIME$ assignment not supported on this platform\n");
    fbval_release(&val);
}
```

---

## 6. FRE / CLEAR

### 6.1 Syntax

```basic
FRE(n)       ' Available memory (numeric arg)
FRE(x$)      ' Free string space after garbage collection
CLEAR         ' Reset all variables to defaults
```

### 6.2 Implementation

FRE in FB returned remaining heap. In a modern environment, return a large constant or query OS:

```c
static FBValue builtin_fre(Interpreter* interp, FBValue* args, int nargs) {
    (void)interp;
    (void)args;
    (void)nargs;
    // Return a large but believable value (simulate 640K)
    // FRE(-1) returns largest non-string array block
    // FRE(-2) returns unused stack space
    // FRE(x$) forces string garbage collection first
    return fbval_long(640L * 1024L);
}

static void exec_clear(Interpreter* interp, ASTNode* node) {
    (void)node;
    // Reset all variables to zero/empty
    symtable_clear(interp->global_scope);
    // Reset DATA pointer
    interp->data_ptr = 0;
    // Reset error state
    interp->err_code = 0;
    interp->err_line = 0;
    interp->on_error_active = 0;
    interp->on_error_target = -1;
    interp->in_error_handler = 0;
}
```

---

## 7. LPRINT / LPRINT USING

### 7.1 Syntax

```basic
LPRINT expr [; expr ...]         ' Print to printer (same formatting as PRINT)
LPRINT USING fmt$; expr [; ...]  ' Formatted print to printer
```

### 7.2 Implementation Strategy

Modern systems don't have LPT1. Redirect to a log file or pipe:

```c
static FILE* get_printer(Interpreter* interp) {
    if (!interp->printer_file) {
        // Try to open LPT1 (Windows) or /dev/lp0 (Linux)
        #ifdef _WIN32
            interp->printer_file = fopen("LPT1", "w");
        #else
            interp->printer_file = fopen("/dev/lp0", "w");
        #endif
        if (!interp->printer_file) {
            // Fall back to printer.log
            interp->printer_file = fopen("PRINTER.LOG", "a");
        }
        if (!interp->printer_file) {
            fb_runtime_error(interp, FB_ERR_DEVICE_UNAVAILABLE,
                             0, "Cannot open printer");
            return NULL;
        }
    }
    return interp->printer_file;
}

// exec_lprint uses the same logic as exec_print but writes to get_printer()
static void exec_lprint(Interpreter* interp, ASTNode* node) {
    FILE* out = get_printer(interp);
    if (!out) return;
    // Delegate to the shared print engine with 'out' as target
    exec_print_to(interp, node, out);
}
```

---

## 8. Stubs for Low-Level DOS Features

### 8.1 Features to Stub

These features are hardware-specific to 16-bit DOS and cannot be meaningfully implemented in a modern environment. Print a warning or no-op:

| Statement/Function | Stub Behavior |
|---|---|
| `PEEK(addr)` | Return 0 |
| `POKE addr, byte` | No-op, optional warning |
| `DEF SEG [= addr]` | No-op, store segment number |
| `INP(port)` | Return 0 |
| `OUT port, byte` | No-op |
| `VARPTR(var)` | Return dummy address (incrementing counter) |
| `VARSEG(var)` | Return fixed segment value |
| `BLOAD file$, addr` | No-op with warning |
| `BSAVE file$, addr, len` | No-op with warning |
| `CALL ABSOLUTE(addr)` | Fatal error: "Feature unavailable" |
| `CALL INTERRUPT(intnum, ...)` | Fatal error: "Feature unavailable" |
| `WAIT port, andmask, xormask` | No-op |
| `SETMEM(bytes)` | Return 0 |

### 8.2 Implementation Pattern

```c
static FBValue builtin_peek(Interpreter* interp, FBValue* args, int nargs) {
    (void)nargs;
    long addr = fbval_to_long(&args[0]);
    fprintf(stderr, "Warning: PEEK(%ld) not supported, returning 0\n", addr);
    return fbval_int(0);
}

static void exec_poke(Interpreter* interp, ASTNode* node) {
    FBValue addr_v = eval_expr(interp, node->data.poke.addr);
    FBValue val_v  = eval_expr(interp, node->data.poke.value);
    // No-op
    fbval_release(&addr_v);
    fbval_release(&val_v);
}

static void exec_def_seg(Interpreter* interp, ASTNode* node) {
    if (node->data.def_seg.has_addr) {
        FBValue val = eval_expr(interp, node->data.def_seg.addr);
        interp->def_seg = (int)fbval_to_long(&val);
        fbval_release(&val);
    } else {
        interp->def_seg = 0;  // Reset to default
    }
}

static FBValue builtin_varptr(Interpreter* interp, FBValue* args, int nargs) {
    (void)interp; (void)args; (void)nargs;
    static int counter = 0x1000;
    return fbval_int((int16_t)(counter++));
}

static FBValue builtin_varseg(Interpreter* interp, FBValue* args, int nargs) {
    (void)interp; (void)args; (void)nargs;
    return fbval_int((int16_t)interp->def_seg);
}
```

---

## 9. Parser Changes

### 9.1 New AST Node Types

```c
AST_SHELL,          // SHELL [command$]
AST_ENVIRON_SET,    // ENVIRON "NAME=VALUE"
AST_CHDIR,          // CHDIR path$
AST_MKDIR,          // MKDIR path$
AST_RMDIR,          // RMDIR path$
AST_CLEAR,          // CLEAR
AST_LPRINT,         // LPRINT (reuse AST_PRINT with flag)
AST_POKE,           // POKE addr, value
AST_DEF_SEG,        // DEF SEG [= addr]
```

### 9.2 New Built-in Functions

```c
// Register in function table:
{ "ENVIRON$", builtin_environ_func, 1, 1 },
{ "COMMAND$", builtin_command_func, 0, 0 },
{ "DATE$",    builtin_date_func,    0, 0 },
{ "TIME$",    builtin_time_func,    0, 0 },
{ "TIMER",    builtin_timer_func,   0, 0 },
{ "FRE",      builtin_fre,          1, 1 },
{ "PEEK",     builtin_peek,         1, 1 },
{ "VARPTR",   builtin_varptr,       1, 1 },
{ "VARSEG",   builtin_varseg,       1, 1 },
{ "SETMEM",   builtin_setmem,      1, 1 },
```

---

## 10. Verification Test Files

### 10.1 `tests/verify/phase7_environ.bas`

```basic
REM Phase 7 Test: ENVIRON / ENVIRON$ / COMMAND$
ENVIRON "FBTEST=HELLO_WORLD"
PRINT ENVIRON$("FBTEST")
PRINT "COMMAND$ = "; COMMAND$
' Test numeric index form
v$ = ENVIRON$(1)
IF LEN(v$) > 0 THEN
    PRINT "First env var exists"
ELSE
    PRINT "No env vars?"
END IF
```

**Expected output (`tests/verify/phase7_expected/environ.txt`):**
```
HELLO_WORLD
COMMAND$ =
First env var exists
```

### 10.2 `tests/verify/phase7_datetime.bas`

```basic
REM Phase 7 Test: DATE$ / TIME$ / TIMER
d$ = DATE$
PRINT "Date length:"; LEN(d$)
t$ = TIME$
PRINT "Time length:"; LEN(t$)
v! = TIMER
IF v! >= 0 AND v! < 86400 THEN
    PRINT "Timer in valid range"
ELSE
    PRINT "Timer out of range!"
END IF
```

**Expected output (`tests/verify/phase7_expected/datetime.txt`):**
```
Date length: 10
Time length: 8
Timer in valid range
```

### 10.3 `tests/verify/phase7_dirs.bas`

```basic
REM Phase 7 Test: MKDIR / CHDIR / RMDIR
ON ERROR GOTO DirErr
MKDIR "FBTESTDIR"
PRINT "Directory created"
CHDIR "FBTESTDIR"
PRINT "Changed into directory"
CHDIR ".."
PRINT "Changed back"
RMDIR "FBTESTDIR"
PRINT "Directory removed"
GOTO Done

DirErr:
PRINT "Directory error:"; ERR
RESUME NEXT

Done:
PRINT "Done"
```

**Expected output (`tests/verify/phase7_expected/dirs.txt`):**
```
Directory created
Changed into directory
Changed back
Directory removed
Done
```

### 10.4 `tests/verify/phase7_misc.bas`

```basic
REM Phase 7 Test: FRE, CLEAR, stubs
PRINT "FRE ="; FRE(0)
x% = 42
PRINT "Before CLEAR: x% ="; x%
CLEAR
PRINT "After CLEAR: x% ="; x%
PRINT "PEEK(0) ="; PEEK(0)
DEF SEG = &HB800
PRINT "DEF SEG done"
DEF SEG
PRINT "DEF SEG reset"
```

**Expected output (`tests/verify/phase7_expected/misc.txt`):**
```
FRE = 655360
Before CLEAR: x% = 42
After CLEAR: x% = 0
PEEK(0) = 0
DEF SEG done
DEF SEG reset
```

### 10.5 `tests/verify/phase7_milestone.bas` — Milestone

```basic
REM Phase 7 Milestone: System utility program
PRINT "===  System Info  ==="
PRINT "Date: "; DATE$
PRINT "Time: "; TIME$
PRINT "Timer:"; INT(TIMER); "seconds since midnight"
PRINT
ENVIRON "FB_VERSION=4.5"
PRINT "FB_VERSION = "; ENVIRON$("FB_VERSION")
PRINT "Free memory:"; FRE(0); "bytes"
PRINT
ON ERROR GOTO ShellErr
MKDIR "TESTUTIL"
CHDIR "TESTUTIL"
CHDIR ".."
RMDIR "TESTUTIL"
PRINT "Directory operations: OK"
GOTO Done

ShellErr:
PRINT "Shell/Dir error:"; ERR
RESUME NEXT

Done:
PRINT "=== Milestone passed ==="
```

**Expected output (`tests/verify/phase7_expected/milestone.txt`):**
```
===  System Info  ===
Date: <current date>
Time: <current time>
Timer: <N> seconds since midnight

FB_VERSION = 4.5
Free memory: 655360 bytes

Directory operations: OK
=== Milestone passed ===
```
*(Note: date/time values are dynamic — test validates format/length only)*

---

## 11. Makefile Updates

```makefile
# Add to SRC list:
SRC += src/system_api.c
```

---

## 12. Phase 7 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **SHELL** | Execute command string via `system()`. No-arg opens interactive shell. |
| 2 | **ENVIRON statement** | Sets environment variable via "NAME=VALUE" string. |
| 3 | **ENVIRON$ function** | String arg: returns env var value. Numeric arg: returns nth env string. Empty string if not found. |
| 4 | **COMMAND$** | Returns combined command-line arguments uppercased. |
| 5 | **CHDIR** | Changes working directory. Error 76 on bad path. |
| 6 | **MKDIR** | Creates directory. Error 75 on failure. |
| 7 | **RMDIR** | Removes directory. Error 75 on failure. |
| 8 | **DATE$** | Returns "MM-DD-YYYY" format, 10 chars. |
| 9 | **TIME$** | Returns "HH:MM:SS" format, 8 chars. |
| 10 | **TIMER** | Returns seconds since midnight as SINGLE (0.0 to 86399.x). |
| 11 | **DATE$/TIME$ assignment** | Stub with warning (no privilege to set system clock). |
| 12 | **FRE** | Returns simulated free memory (655360). |
| 13 | **CLEAR** | Resets all variables, DATA pointer, error state. |
| 14 | **LPRINT / LPRINT USING** | Outputs to printer device or PRINTER.LOG fallback. Same formatting as PRINT. |
| 15 | **PEEK / POKE** | PEEK returns 0. POKE is no-op. |
| 16 | **DEF SEG** | Stores segment; resets to 0 when called without argument. |
| 17 | **VARPTR / VARSEG** | Return dummy addresses. |
| 18 | **All other stubs** | INP, OUT, WAIT, BLOAD, BSAVE, CALL ABSOLUTE, CALL INTERRUPT, SETMEM — no-op or fatal with "Feature unavailable". |
| 19 | **Milestone** | System utility program runs: reads date/time/timer, sets/gets env var, creates+removes directory. |

---

## 13. Key Implementation Warnings

1. **COMMAND$ uppercasing:** FB always returns COMMAND$ in uppercase. The interpreter must uppercase argv before storing.

2. **TIMER precision:** FB's TIMER returns a SINGLE. On modern systems, use float (not double) and ensure the value wraps correctly at midnight (0.0 after 86400).

3. **ENVIRON portability:** `putenv()` behavior differs between Windows and POSIX. On Windows use `_putenv_s()`; on POSIX use `setenv()`. Never use `putenv()` with stack-allocated strings.

4. **CLEAR is destructive:** CLEAR reinitializes all variables, resets the stack, and clears ON ERROR state. It does NOT clear CONST values or DEFtype settings (those are compile-time). If procedures are active (call frames on stack), CLEAR should probably be disallowed or it should unwind the call stack first.

5. **SHELL security:** The `system()` call is inherently dangerous. In a sandboxed environment, consider disabling SHELL or logging all commands.

6. **Stub warnings:** Low-level stubs (PEEK/POKE/etc.) should print warnings to stderr only once per unique call site, not every invocation, to avoid flooding output.

7. **DATE$/TIME$ as l-values:** These are special tokens that can appear on the left side of `=`. The parser must recognize `DATE$ = expr` and `TIME$ = expr` as assignment statements, not comparisons.
