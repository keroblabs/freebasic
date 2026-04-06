# Phase 2 — I/O, Strings, Math (Interactive Programs): Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build the **interactive I/O, string processing, math library, and control-flow additions** for the FreeBASIC interpreter. Phase 2 transforms the interpreter from a batch-only calculator into an interactive environment capable of running text adventures, quiz programs, calculators, and menu-driven applications.

---

## Project File Structure (Phase 2 additions)

Files **added** or **significantly modified** relative to Phase 1 are marked with `[NEW]` or `[MOD]`.

```
fbasic/
├── Makefile                        [MOD] — add new source files
├── include/
│   ├── fb.h                       [MOD] — pull in new headers
│   ├── token.h
│   ├── lexer.h
│   ├── value.h
│   ├── symtable.h
│   ├── ast.h                      [MOD] — add AST nodes for INPUT, SELECT CASE, DATA, etc.
│   ├── parser.h                   [MOD] — new parse functions
│   ├── interpreter.h              [MOD] — console state, DATA pool pointer
│   ├── console.h                  [NEW] — console abstraction (CLS, LOCATE, COLOR, INKEY$)
│   ├── builtins_str.h             [NEW] — string function declarations
│   ├── builtins_math.h            [NEW] — math function declarations
│   ├── print_using.h              [NEW] — PRINT USING format engine
│   ├── coerce.h
│   └── error.h
├── src/
│   ├── main.c
│   ├── lexer.c
│   ├── value.c
│   ├── symtable.c
│   ├── ast.c                      [MOD] — new node constructors
│   ├── parser.c                   [MOD] — parse INPUT, SELECT CASE, DATA, etc.
│   ├── interpreter.c              [MOD] — execute new statements, DATA pool
│   ├── console.c                  [NEW] — platform-specific console I/O
│   ├── builtins_str.c             [NEW] — all string function implementations
│   ├── builtins_math.c            [NEW] — all math function implementations
│   ├── print_using.c              [NEW] — PRINT USING formatter
│   ├── coerce.c
│   └── error.c
└── tests/
    ├── test_lexer.c
    ├── test_value.c
    ├── test_symtable.c
    ├── test_coerce.c
    ├── test_parser.c
    ├── test_interpreter.c
    ├── test_builtins_str.c        [NEW] — unit tests for string functions
    ├── test_builtins_math.c       [NEW] — unit tests for math functions
    ├── test_print_using.c         [NEW] — unit tests for PRINT USING
    └── verify/
        ├── phase2_input.bas       [NEW] — INPUT / LINE INPUT tests
        ├── phase2_strings.bas     [NEW] — string function tests
        ├── phase2_math.bas        [NEW] — math function tests
        ├── phase2_print_using.bas [NEW] — PRINT USING tests
        ├── phase2_select.bas      [NEW] — SELECT CASE tests
        ├── phase2_data.bas        [NEW] — DATA / READ / RESTORE tests
        ├── phase2_console.bas     [NEW] — CLS, LOCATE, COLOR tests
        ├── phase2_write.bas       [NEW] — WRITE statement tests
        ├── phase2_adventure.bas   [NEW] — milestone: simple text adventure
        ├── phase2_calculator.bas  [NEW] — milestone: calculator program
        └── phase2_expected/       [NEW]
            ├── strings.txt
            ├── math.txt
            ├── print_using.txt
            ├── select.txt
            ├── data.txt
            └── write.txt
```

---

## 1. INPUT Statement

### 1.1 Syntax

```
INPUT [;] ["prompt" {; | ,}] varlist
```

- Leading `;` suppresses the newline after user presses Enter (cursor stays on same line)
- Prompt string followed by `;` prints `"? "` after prompt; followed by `,` suppresses `"? "`
- `varlist` is comma-separated list of variables
- If user enters wrong number of values or type mismatch → `"Redo from start"` and re-prompt

### 1.2 AST Node

```c
// AST_INPUT node data
struct {
    char*          prompt;        // Prompt string (NULL if none)
    int            prompt_sep;    // TOK_SEMICOLON or TOK_COMMA (after prompt)
    int            suppress_nl;   // Leading ';' present
    struct ASTNode** vars;        // Array of variable references
    int            var_count;
} input_stmt;
```

### 1.3 Parse INPUT

```c
static void parse_input(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume INPUT

    int suppress_nl = 0;
    if (current_token(p)->kind == TOK_SEMICOLON) {
        suppress_nl = 1;
        advance(p);
    }

    char* prompt = NULL;
    int prompt_sep = TOK_SEMICOLON; // default: show "? "

    // Check for prompt string
    if (current_token(p)->kind == TOK_STRING_LIT) {
        prompt = strdup(current_token(p)->value.str.text);
        advance(p);

        if (current_token(p)->kind == TOK_SEMICOLON) {
            prompt_sep = TOK_SEMICOLON;
            advance(p);
        } else if (current_token(p)->kind == TOK_COMMA) {
            prompt_sep = TOK_COMMA;
            advance(p);
        }
    }

    // Parse variable list
    ASTNode* vars[64];
    int var_count = 0;
    vars[var_count++] = parse_variable_ref(p);
    while (current_token(p)->kind == TOK_COMMA) {
        advance(p);
        vars[var_count++] = parse_variable_ref(p);
    }

    ASTNode* node = ast_input(line, prompt, prompt_sep, suppress_nl,
                              copy_node_array(vars, var_count), var_count);
    program_add_stmt(p->prog, node);
}
```

### 1.4 Execute INPUT

```c
static void exec_input(Interpreter* interp, ASTNode* node) {
retry:
    // Print prompt
    if (node->data.input_stmt.prompt) {
        printf("%s", node->data.input_stmt.prompt);
    }
    if (node->data.input_stmt.prompt_sep == TOK_SEMICOLON) {
        printf("? ");
    }
    fflush(stdout);

    // Read line from stdin
    char linebuf[4096];
    if (!fgets(linebuf, sizeof(linebuf), stdin)) {
        interp->running = 0;
        return;
    }
    // Strip trailing newline
    int len = (int)strlen(linebuf);
    while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
        linebuf[--len] = '\0';

    // Split by commas for multiple variables
    char* fields[64];
    int field_count = 0;
    char* ptr = linebuf;
    fields[field_count++] = ptr;
    while (*ptr) {
        if (*ptr == ',') {
            *ptr = '\0';
            ptr++;
            while (*ptr == ' ') ptr++; // skip spaces after comma
            fields[field_count++] = ptr;
        } else {
            ptr++;
        }
    }

    // Validate field count
    if (field_count != node->data.input_stmt.var_count) {
        printf("Redo from start\n");
        goto retry;
    }

    // Assign each field to its variable with type coercion
    for (int i = 0; i < node->data.input_stmt.var_count; i++) {
        const char* name = node->data.input_stmt.vars[i]->data.variable.name;
        Symbol* sym = resolve_or_create_var(interp,
                          node->data.input_stmt.vars[i]);

        if (sym->type == FB_STRING) {
            fbval_release(&sym->value);
            sym->value = fbval_string_from_cstr(fields[i]);
        } else {
            // Parse numeric value
            char* endptr;
            double val = strtod(fields[i], &endptr);
            // Check for invalid numeric input
            while (*endptr == ' ') endptr++;
            if (*endptr != '\0' && *endptr != '\0') {
                printf("Redo from start\n");
                goto retry;
            }
            FBValue numval = fbval_double(val);
            FBValue coerced = fbval_coerce(&numval, sym->type);
            fbval_release(&sym->value);
            sym->value = coerced;
        }
    }

    if (!node->data.input_stmt.suppress_nl) {
        // Normal behavior — newline already consumed by fgets
    }
}
```

---

## 2. LINE INPUT Statement

### 2.1 Syntax

```
LINE INPUT [;] ["prompt";] stringvar$
```

Reads an entire line into a single string variable — no comma parsing.

### 2.2 Parse & Execute

```c
static void parse_line_input(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume LINE
    expect(p, TOK_KW_INPUT);

    int suppress_nl = 0;
    if (current_token(p)->kind == TOK_SEMICOLON) {
        suppress_nl = 1;
        advance(p);
    }

    char* prompt = NULL;
    if (current_token(p)->kind == TOK_STRING_LIT) {
        prompt = strdup(current_token(p)->value.str.text);
        advance(p);
        expect(p, TOK_SEMICOLON);
    }

    ASTNode* var = parse_variable_ref(p);

    ASTNode* node = ast_line_input(line, prompt, suppress_nl, var);
    program_add_stmt(p->prog, node);
}

static void exec_line_input(Interpreter* interp, ASTNode* node) {
    if (node->data.line_input.prompt) {
        printf("%s", node->data.line_input.prompt);
    }
    fflush(stdout);

    char linebuf[32768]; // FB max string 32K
    if (!fgets(linebuf, sizeof(linebuf), stdin)) {
        interp->running = 0;
        return;
    }
    int len = (int)strlen(linebuf);
    while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
        linebuf[--len] = '\0';

    Symbol* sym = resolve_or_create_var(interp, node->data.line_input.var);
    fbval_release(&sym->value);
    sym->value = fbval_string_from_cstr(linebuf);
}
```

---

## 3. INKEY$ Function

### 3.1 Platform Abstraction

INKEY$ returns the next key in the keyboard buffer as a string, or `""` if none is available. Non-blocking.

```c
// include/console.h
#ifndef CONSOLE_H
#define CONSOLE_H

// Initialize console for raw input (disable line buffering).
void console_init(void);

// Restore console to normal mode.
void console_shutdown(void);

// Non-blocking key read. Returns 0 if no key, else the character code.
// For extended keys (arrows, F-keys), returns two-call sequence:
// first call returns 0 (or 0x00/0xE0), second call returns scan code.
int console_inkey(void);

// Clear screen.
void console_cls(void);

// Position cursor (1-based row, col).
void console_locate(int row, int col);

// Set text color (foreground 0-15, background 0-7).
void console_color(int fg, int bg);

// Get current cursor row (1-based).
int console_csrlin(void);

// Get current cursor column (1-based).
int console_pos(void);

// Set screen width (40 or 80 columns typically).
void console_width(int cols, int rows);

// Emit BEL character.
void console_beep(void);

#endif
```

### 3.2 Windows Implementation (`src/console.c`)

```c
#ifdef _WIN32
#include <windows.h>
#include <conio.h>

static HANDLE hConsoleOut;
static HANDLE hConsoleIn;
static DWORD  oldConsoleMode;

void console_init(void) {
    hConsoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hConsoleIn  = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hConsoleIn, &oldConsoleMode);
    // Enable virtual terminal processing for ANSI sequences
    DWORD mode;
    GetConsoleMode(hConsoleOut, &mode);
    SetConsoleMode(hConsoleOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

void console_shutdown(void) {
    SetConsoleMode(hConsoleIn, oldConsoleMode);
}

int console_inkey(void) {
    if (_kbhit()) {
        int ch = _getch();
        return ch;
    }
    return 0;
}

void console_cls(void) {
    COORD topLeft = {0, 0};
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsoleOut, &csbi);
    DWORD len = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    FillConsoleOutputCharacter(hConsoleOut, ' ', len, topLeft, &written);
    FillConsoleOutputAttribute(hConsoleOut, csbi.wAttributes, len,
                               topLeft, &written);
    SetConsoleCursorPosition(hConsoleOut, topLeft);
}

void console_locate(int row, int col) {
    COORD pos = { (SHORT)(col - 1), (SHORT)(row - 1) };
    SetConsoleCursorPosition(hConsoleOut, pos);
}

void console_color(int fg, int bg) {
    // Map FB color indices to Win32 console attributes
    static const WORD fb_to_win[] = {
        0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15
    };
    WORD attr = fb_to_win[fg & 15] | (fb_to_win[bg & 7] << 4);
    SetConsoleTextAttribute(hConsoleOut, attr);
}

int console_csrlin(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsoleOut, &csbi);
    return csbi.dwCursorPosition.Y + 1;
}

int console_pos(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsoleOut, &csbi);
    return csbi.dwCursorPosition.X + 1;
}

void console_width(int cols, int rows) {
    SMALL_RECT rect = { 0, 0, (SHORT)(cols - 1), (SHORT)(rows - 1) };
    COORD size = { (SHORT)cols, (SHORT)rows };
    SetConsoleScreenBufferSize(hConsoleOut, size);
    SetConsoleWindowInfo(hConsoleOut, TRUE, &rect);
}

void console_beep(void) {
    putchar('\a');
    fflush(stdout);
}

#else
// POSIX implementation using termios and ANSI escapes
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>

static struct termios old_termios;

void console_init(void) {
    tcgetattr(STDIN_FILENO, &old_termios);
    struct termios raw = old_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void console_shutdown(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

int console_inkey(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) return (unsigned char)c;
    return 0;
}

void console_cls(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void console_locate(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

void console_color(int fg, int bg) {
    // Map FB colors to ANSI 256-color sequences
    static const int fb_to_ansi_fg[] = {
        30, 34, 32, 36, 31, 35, 33, 37,
        90, 94, 92, 96, 91, 95, 93, 97
    };
    static const int fb_to_ansi_bg[] = {
        40, 44, 42, 46, 41, 45, 43, 47
    };
    printf("\033[%d;%dm", fb_to_ansi_fg[fg & 15], fb_to_ansi_bg[bg & 7]);
    fflush(stdout);
}

int console_csrlin(void) {
    printf("\033[6n");
    fflush(stdout);
    int row = 1, col = 1;
    if (scanf("\033[%d;%dR", &row, &col) < 2) return 1;
    return row;
}

int console_pos(void) {
    printf("\033[6n");
    fflush(stdout);
    int row = 1, col = 1;
    if (scanf("\033[%d;%dR", &row, &col) < 2) return 1;
    return col;
}

void console_width(int cols, int rows) {
    printf("\033[8;%d;%dt", rows, cols);
    fflush(stdout);
}

void console_beep(void) {
    putchar('\a');
    fflush(stdout);
}
#endif
```

### 3.3 INKEY$ as Built-in Function

```c
// In eval_builtin_func:
if (_stricmp(name, "INKEY$") == 0) {
    int ch = console_inkey();
    if (ch == 0) {
        return fbval_string_from_cstr("");
    }
    // Extended key: return 2-char string CHR$(0) + CHR$(scancode)
    if (ch == 0x00 || ch == 0xE0) {
        int scan = console_inkey();
        char buf[3] = { (char)ch, (char)scan, '\0' };
        FBString* s = fbstr_new(buf, 2);
        return fbval_string(s);
    }
    char buf[2] = { (char)ch, '\0' };
    return fbval_string_from_cstr(buf);
}
```

---

## 4. Console Statements: CLS, LOCATE, COLOR, CSRLIN, POS

### 4.1 AST Nodes

```c
// AST_CLS — no data needed
// AST_LOCATE
struct { struct ASTNode* row; struct ASTNode* col; } locate;
// AST_COLOR
struct { struct ASTNode* fg; struct ASTNode* bg; } color;
```

### 4.2 Parse & Execute

```c
static void parse_cls(Parser* p) {
    int line = current_token(p)->line;
    advance(p);
    program_add_stmt(p->prog, ast_alloc(AST_CLS, line));
}

static void exec_cls(Interpreter* interp, ASTNode* node) {
    (void)interp;
    (void)node;
    console_cls();
    interp->print_col = 1;
}

static void parse_locate(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume LOCATE
    ASTNode* row = NULL;
    ASTNode* col = NULL;
    if (current_token(p)->kind != TOK_COMMA &&
        current_token(p)->kind != TOK_EOL) {
        row = parse_expr(p, 1);
    }
    if (current_token(p)->kind == TOK_COMMA) {
        advance(p);
        if (current_token(p)->kind != TOK_EOL &&
            current_token(p)->kind != TOK_COMMA) {
            col = parse_expr(p, 1);
        }
    }
    program_add_stmt(p->prog, ast_locate(line, row, col));
}

static void exec_locate(Interpreter* interp, ASTNode* node) {
    int row = node->data.locate.row
              ? (int)fbval_to_long(&eval_expr(interp, node->data.locate.row))
              : console_csrlin();
    int col = node->data.locate.col
              ? (int)fbval_to_long(&eval_expr(interp, node->data.locate.col))
              : console_pos();
    console_locate(row, col);
    interp->print_col = col;
}

static void parse_color(Parser* p) {
    int line = current_token(p)->line;
    advance(p);
    ASTNode* fg = NULL;
    ASTNode* bg = NULL;
    if (current_token(p)->kind != TOK_COMMA &&
        current_token(p)->kind != TOK_EOL) {
        fg = parse_expr(p, 1);
    }
    if (current_token(p)->kind == TOK_COMMA) {
        advance(p);
        if (current_token(p)->kind != TOK_EOL) {
            bg = parse_expr(p, 1);
        }
    }
    program_add_stmt(p->prog, ast_color(line, fg, bg));
}

static void exec_color(Interpreter* interp, ASTNode* node) {
    int fg = node->data.color.fg
             ? (int)fbval_to_long(&eval_expr(interp, node->data.color.fg))
             : interp->current_fg;
    int bg = node->data.color.bg
             ? (int)fbval_to_long(&eval_expr(interp, node->data.color.bg))
             : interp->current_bg;
    interp->current_fg = fg;
    interp->current_bg = bg;
    console_color(fg, bg);
}
```

### 4.3 CSRLIN and POS as Functions

```c
// In eval_builtin_func:
if (_stricmp(name, "CSRLIN") == 0) {
    return fbval_int((int16_t)console_csrlin());
}
if (_stricmp(name, "POS") == 0) {
    // POS(x) — argument is ignored in FB
    return fbval_int((int16_t)console_pos());
}
```

---

## 5. SPC and TAB Print Functions

### 5.1 Implementation as PRINT Modifiers

SPC(n) and TAB(n) are special — they can only appear inside a PRINT argument list.

```c
// During PRINT execution, when encountering SPC/TAB pseudo-nodes:
// SPC(n): output n spaces
// TAB(n): advance to column n (if already past column n, go to next line)

static void exec_print_spc(Interpreter* interp, int n) {
    for (int i = 0; i < n; i++) {
        putchar(' ');
        interp->print_col++;
    }
}

static void exec_print_tab(Interpreter* interp, int target_col) {
    if (interp->print_col > target_col) {
        putchar('\n');
        interp->print_col = 1;
    }
    while (interp->print_col < target_col) {
        putchar(' ');
        interp->print_col++;
    }
}
```

---

## 6. WRITE Statement

### 6.1 Syntax

```
WRITE [#filenum,] exprlist
```

Outputs comma-delimited values. Strings are enclosed in double quotes. Numbers have no leading/trailing spaces.

### 6.2 Parse & Execute

```c
static void exec_write(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.print.item_count; i++) {
        if (i > 0) printf(",");

        FBValue val = eval_expr(interp, node->data.print.items[i]);
        if (val.type == FB_STRING) {
            printf("\"%s\"", val.as.str->data);
        } else {
            // Numbers in WRITE have no leading/trailing spaces
            double d = fbval_to_double(&val);
            if (val.type == FB_INTEGER || val.type == FB_LONG) {
                printf("%d", (int)fbval_to_long(&val));
            } else {
                // Remove trailing zeros from float output
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", d);
                printf("%s", buf);
            }
        }
        fbval_release(&val);
    }
    printf("\n");
}
```

---

## 7. BEEP

```c
static void exec_beep(Interpreter* interp, ASTNode* node) {
    (void)interp; (void)node;
    console_beep();
}
```

---

## 8. PRINT USING (`include/print_using.h`, `src/print_using.c`)

### 8.1 Format Codes

| Code | Meaning |
|------|---------|
| `#` | Digit placeholder |
| `.` | Decimal point position |
| `,` | Insert comma every 3 digits (before decimal) |
| `+` | At start or end: force sign display |
| `-` | At end: trailing minus for negatives |
| `$$` | Double dollar: leading `$` with no space |
| `**` | Leading asterisk fill |
| `**$` | Asterisk fill with leading `$` |
| `^^^^` | Scientific notation (4 carets) |
| `_` | Literal escape: next char printed literally |
| `!` | First character of string |
| `\  \` | Fixed-width string (width = number of chars between backslashes + 2) |
| `&` | Variable-width string (entire string) |

### 8.2 Format Engine API

```c
// Format a FBValue according to a PRINT USING format string.
// Returns heap-allocated result string.
// *fmt_pos is updated to point past the consumed format specifier.
char* print_using_format(const char* fmt, int* fmt_pos, const FBValue* val);

// Full PRINT USING execution: iterate format string, matching with values.
// Recycles format string if more values than format specifiers.
void exec_print_using(Interpreter* interp, const char* fmt,
                      FBValue* values, int value_count,
                      int trailing_sep);
```

### 8.3 Numeric Format Implementation

```c
char* print_using_format_number(const char* spec, int spec_len,
                                double value) {
    // 1. Count '#' digits before/after '.'
    int before_dot = 0, after_dot = 0;
    int has_dot = 0, has_comma = 0;
    int has_plus_start = 0, has_plus_end = 0;
    int has_minus_end = 0;
    int has_dollar = 0, has_star_fill = 0;
    int has_exponent = 0; // ^^^^

    // 2. Parse the format spec to extract flags
    // ... (detailed character-by-character scan)

    // 3. Format the number:
    //    a. Convert to string with appropriate precision
    //    b. Split integer and fractional parts
    //    c. Pad integer part to fill '#' positions
    //    d. Insert commas if flagged
    //    e. Apply fill character (* or space)
    //    f. Apply dollar sign, sign characters
    //    g. Handle overflow: if number too wide, prefix with '%'

    // 4. Return formatted string
}

char* print_using_format_string(const char* spec, int spec_len,
                                const char* str_value) {
    // '!' → first character only
    // '\  \' → fixed width, left-justified, padded with spaces
    // '&' → entire string
}
```

### 8.4 Parse PRINT USING

```c
// Syntax: PRINT USING format_string; expr [; expr] ...
static void parse_print_using(Parser* p) {
    int line = current_token(p)->line;
    // Already consumed PRINT, now check for USING
    advance(p); // consume USING

    ASTNode* fmt_expr = parse_expr(p, 1);
    expect(p, TOK_SEMICOLON);

    // Parse value list (semicolon-separated)
    ASTNode* items[256];
    int count = 0;
    items[count++] = parse_expr(p, 1);
    while (current_token(p)->kind == TOK_SEMICOLON ||
           current_token(p)->kind == TOK_COMMA) {
        advance(p);
        if (current_token(p)->kind != TOK_EOL &&
            current_token(p)->kind != TOK_EOF) {
            items[count++] = parse_expr(p, 1);
        }
    }

    ASTNode* node = ast_print_using(line, fmt_expr,
                                     copy_node_array(items, count), count);
    program_add_stmt(p->prog, node);
}
```

---

## 9. VIEW PRINT / WIDTH

### 9.1 VIEW PRINT

```
VIEW PRINT [top TO bottom]
```

Sets the scrolling region. Without arguments, resets to full screen.

```c
static void exec_view_print(Interpreter* interp, ASTNode* node) {
    if (node->data.view_print.top && node->data.view_print.bottom) {
        interp->scroll_top = (int)fbval_to_long(
            &eval_expr(interp, node->data.view_print.top));
        interp->scroll_bottom = (int)fbval_to_long(
            &eval_expr(interp, node->data.view_print.bottom));
        // Set scrolling region via ANSI: \033[top;bottomr
        printf("\033[%d;%dr", interp->scroll_top, interp->scroll_bottom);
    } else {
        interp->scroll_top = 1;
        interp->scroll_bottom = 25;
        printf("\033[r"); // Reset scrolling region
    }
    fflush(stdout);
}
```

### 9.2 WIDTH

```
WIDTH columns [, rows]
```

```c
static void exec_width(Interpreter* interp, ASTNode* node) {
    int cols = (int)fbval_to_long(&eval_expr(interp, node->data.width.cols));
    int rows = 25;
    if (node->data.width.rows) {
        rows = (int)fbval_to_long(&eval_expr(interp, node->data.width.rows));
    }
    interp->screen_width = cols;
    interp->screen_height = rows;
    console_width(cols, rows);
}
```

---

## 10. String Functions (`src/builtins_str.c`)

### 10.1 Complete String Function Table

```c
// All string functions with their signatures and implementations.
// Each returns a FBValue.

typedef FBValue (*BuiltinStrFunc)(FBValue* args, int argc, int line);

typedef struct {
    const char* name;
    int         min_args;
    int         max_args;
    BuiltinStrFunc func;
} StrFuncEntry;

static const StrFuncEntry str_funcs[] = {
    { "LEFT$",    2, 2, builtin_left   },
    { "RIGHT$",   2, 2, builtin_right  },
    { "MID$",     2, 3, builtin_mid    },
    { "LEN",      1, 1, builtin_len    },
    { "INSTR",    2, 3, builtin_instr  },
    { "CHR$",     1, 1, builtin_chr    },
    { "ASC",      1, 1, builtin_asc    },
    { "STR$",     1, 1, builtin_str    },
    { "VAL",      1, 1, builtin_val    },
    { "UCASE$",   1, 1, builtin_ucase  },
    { "LCASE$",   1, 1, builtin_lcase  },
    { "LTRIM$",   1, 1, builtin_ltrim  },
    { "RTRIM$",   1, 1, builtin_rtrim  },
    { "STRING$",  2, 2, builtin_string_fill },
    { "SPACE$",   1, 1, builtin_space  },
    { "HEX$",     1, 1, builtin_hex    },
    { "OCT$",     1, 1, builtin_oct    },
    { NULL,       0, 0, NULL           }
};
```

### 10.2 String Function Implementations

```c
static FBValue builtin_left(FBValue* args, int argc, int line) {
    // LEFT$(string, n) — first n characters
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    int n = (int)fbval_to_long(&args[1]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    if (n > args[0].as.str->len) n = args[0].as.str->len;
    FBString* s = fbstr_mid(args[0].as.str, 0, n);
    return fbval_string(s);
}

static FBValue builtin_right(FBValue* args, int argc, int line) {
    // RIGHT$(string, n) — last n characters
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    int n = (int)fbval_to_long(&args[1]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    int slen = args[0].as.str->len;
    if (n > slen) n = slen;
    FBString* s = fbstr_mid(args[0].as.str, slen - n, n);
    return fbval_string(s);
}

static FBValue builtin_mid(FBValue* args, int argc, int line) {
    // MID$(string, start [, length])
    // FB: start is 1-based
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    int start = (int)fbval_to_long(&args[1]) - 1; // convert to 0-based
    if (start < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    int slen = args[0].as.str->len;
    int n = (argc >= 3) ? (int)fbval_to_long(&args[2]) : slen - start;
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    if (start >= slen) return fbval_string_from_cstr("");
    if (start + n > slen) n = slen - start;
    FBString* s = fbstr_mid(args[0].as.str, start, n);
    return fbval_string(s);
}

static FBValue builtin_instr(FBValue* args, int argc, int line) {
    // INSTR([start,] searchstring, pattern)
    int start_pos;
    const char* haystack;
    const char* needle;
    if (argc == 2) {
        start_pos = 0;
        if (args[0].type != FB_STRING || args[1].type != FB_STRING)
            fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
        haystack = args[0].as.str->data;
        needle = args[1].as.str->data;
    } else {
        start_pos = (int)fbval_to_long(&args[0]) - 1;
        if (args[1].type != FB_STRING || args[2].type != FB_STRING)
            fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
        haystack = args[1].as.str->data;
        needle = args[2].as.str->data;
    }
    if (start_pos < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    if (needle[0] == '\0') return fbval_int((int16_t)(start_pos + 1));
    const char* found = strstr(haystack + start_pos, needle);
    if (!found) return fbval_int(0);
    return fbval_int((int16_t)(found - haystack + 1)); // 1-based
}

static FBValue builtin_ucase(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    FBString* src = args[0].as.str;
    FBString* s = fbstr_new(src->data, src->len);
    for (int i = 0; i < s->len; i++) s->data[i] = toupper(s->data[i]);
    return fbval_string(s);
}

static FBValue builtin_lcase(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    FBString* src = args[0].as.str;
    FBString* s = fbstr_new(src->data, src->len);
    for (int i = 0; i < s->len; i++) s->data[i] = tolower(s->data[i]);
    return fbval_string(s);
}

static FBValue builtin_ltrim(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    const char* p = args[0].as.str->data;
    while (*p == ' ') p++;
    return fbval_string_from_cstr(p);
}

static FBValue builtin_rtrim(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, NULL);
    int len = args[0].as.str->len;
    while (len > 0 && args[0].as.str->data[len - 1] == ' ') len--;
    FBString* s = fbstr_mid(args[0].as.str, 0, len);
    return fbval_string(s);
}

static FBValue builtin_string_fill(FBValue* args, int argc, int line) {
    // STRING$(n, char_or_code)
    int n = (int)fbval_to_long(&args[0]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    char fill_char;
    if (args[1].type == FB_STRING) {
        fill_char = args[1].as.str->data[0];
    } else {
        fill_char = (char)fbval_to_long(&args[1]);
    }
    char* buf = malloc(n + 1);
    memset(buf, fill_char, n);
    buf[n] = '\0';
    FBString* s = fbstr_new(buf, n);
    free(buf);
    return fbval_string(s);
}

static FBValue builtin_space(FBValue* args, int argc, int line) {
    int n = (int)fbval_to_long(&args[0]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, NULL);
    char* buf = malloc(n + 1);
    memset(buf, ' ', n);
    buf[n] = '\0';
    FBString* s = fbstr_new(buf, n);
    free(buf);
    return fbval_string(s);
}

static FBValue builtin_hex(FBValue* args, int argc, int line) {
    int32_t v = fbval_to_long(&args[0]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%X", (unsigned int)v);
    return fbval_string_from_cstr(buf);
}

static FBValue builtin_oct(FBValue* args, int argc, int line) {
    int32_t v = fbval_to_long(&args[0]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%o", (unsigned int)v);
    return fbval_string_from_cstr(buf);
}
```

### 10.3 MID$ as Statement (Lvalue)

```
MID$(stringvar$, start [, length]) = replacement$
```

Replaces characters in-place within a string variable.

```c
// AST_MID_STMT
struct {
    struct ASTNode* target_var;  // String variable
    struct ASTNode* start;       // 1-based start position
    struct ASTNode* length;      // Optional length (NULL = rest of string)
    struct ASTNode* replacement; // Replacement string expression
} mid_stmt;

static void exec_mid_stmt(Interpreter* interp, ASTNode* node) {
    Symbol* sym = scope_lookup(interp->current_scope,
                      node->data.mid_stmt.target_var->data.variable.name);
    if (!sym || sym->value.type != FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, node->line, NULL);
        return;
    }

    int start = (int)fbval_to_long(&eval_expr(interp,
                    node->data.mid_stmt.start)) - 1;
    FBValue repl_val = eval_expr(interp, node->data.mid_stmt.replacement);

    int max_len = sym->value.as.str->len - start;
    if (node->data.mid_stmt.length) {
        int req_len = (int)fbval_to_long(&eval_expr(interp,
                          node->data.mid_stmt.length));
        if (req_len < max_len) max_len = req_len;
    }

    // Copy-on-write: get writable buffer
    sym->value.as.str = fbstr_cow(sym->value.as.str);

    int copy_len = repl_val.as.str->len;
    if (copy_len > max_len) copy_len = max_len;
    memcpy(sym->value.as.str->data + start, repl_val.as.str->data, copy_len);

    fbval_release(&repl_val);
}
```

---

## 11. Math Functions (`src/builtins_math.c`)

### 11.1 Complete Math Function Table

```c
// All math functions — most take 1 argument, RANDOMIZE takes 0 or 1.
typedef FBValue (*BuiltinMathFunc)(FBValue* args, int argc, int line);

typedef struct {
    const char* name;
    int         min_args;
    int         max_args;
    BuiltinMathFunc func;
} MathFuncEntry;

static const MathFuncEntry math_funcs[] = {
    { "ABS",        1, 1, builtin_abs       },
    { "INT",        1, 1, builtin_int       },
    { "FIX",        1, 1, builtin_fix       },
    { "SQR",        1, 1, builtin_sqr       },
    { "SIN",        1, 1, builtin_sin       },
    { "COS",        1, 1, builtin_cos       },
    { "TAN",        1, 1, builtin_tan       },
    { "ATN",        1, 1, builtin_atn       },
    { "LOG",        1, 1, builtin_log       },
    { "EXP",        1, 1, builtin_exp       },
    { "SGN",        1, 1, builtin_sgn       },
    { "RND",        0, 1, builtin_rnd       },
    { "CINT",       1, 1, builtin_cint      },
    { "CLNG",       1, 1, builtin_clng      },
    { "CSNG",       1, 1, builtin_csng      },
    { "CDBL",       1, 1, builtin_cdbl      },
    { NULL,         0, 0, NULL              }
};
```

### 11.2 Math Function Implementations

```c
static FBValue builtin_sin(FBValue* args, int argc, int line) {
    return fbval_double(sin(fbval_to_double(&args[0])));
}
static FBValue builtin_cos(FBValue* args, int argc, int line) {
    return fbval_double(cos(fbval_to_double(&args[0])));
}
static FBValue builtin_tan(FBValue* args, int argc, int line) {
    return fbval_double(tan(fbval_to_double(&args[0])));
}
static FBValue builtin_atn(FBValue* args, int argc, int line) {
    return fbval_double(atan(fbval_to_double(&args[0])));
}
static FBValue builtin_log(FBValue* args, int argc, int line) {
    double v = fbval_to_double(&args[0]);
    if (v <= 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "LOG of non-positive");
    return fbval_double(log(v));
}
static FBValue builtin_exp(FBValue* args, int argc, int line) {
    return fbval_double(exp(fbval_to_double(&args[0])));
}
```

### 11.3 RND Function

FB's RND uses a linear congruential generator with specific seed behavior:

```c
static uint32_t rnd_seed = 327680; // FB default seed

static FBValue builtin_rnd(FBValue* args, int argc, int line) {
    float n = (argc > 0) ? (float)fbval_to_double(&args[0]) : 1.0f;

    if (n == 0.0f) {
        // RND(0) — return last generated value (don't advance)
    } else if (n < 0.0f) {
        // RND(negative) — reseed with the argument, return deterministic value
        // FB uses the binary representation of the float as seed
        memcpy(&rnd_seed, &n, sizeof(uint32_t));
    }

    if (n != 0.0f) {
        // Advance the LCG: seed = (seed * 16598013 + 12820163) MOD 2^24
        rnd_seed = (rnd_seed * 16598013u + 12820163u) & 0xFFFFFF;
    }

    // Convert to float in [0, 1)
    float result = (float)rnd_seed / 16777216.0f;
    return fbval_single(result);
}
```

### 11.4 RANDOMIZE Statement

```
RANDOMIZE [expression]
RANDOMIZE TIMER
```

```c
static void exec_randomize(Interpreter* interp, ASTNode* node) {
    if (node->data.randomize.use_timer) {
        // Seed with current time
        rnd_seed = (uint32_t)time(NULL);
    } else if (node->data.randomize.seed_expr) {
        FBValue val = eval_expr(interp, node->data.randomize.seed_expr);
        rnd_seed = (uint32_t)fbval_to_long(&val);
        fbval_release(&val);
    } else {
        // No argument — FB prompts "Random-number seed (-32768 to 32767)? "
        printf("Random-number seed (-32768 to 32767)? ");
        fflush(stdout);
        char buf[64];
        if (fgets(buf, sizeof(buf), stdin)) {
            rnd_seed = (uint32_t)atoi(buf);
        }
    }
}
```

---

## 12. DATA / READ / RESTORE

### 12.1 Data Pool Collection (Parse Time)

During parsing, all `DATA` statements are scanned and their values are collected into a global ordered pool (`Program.data_pool`). DATA items are comma-separated literals (numbers and quoted strings).

```c
static void parse_data(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume DATA

    while (current_token(p)->kind != TOK_EOL &&
           current_token(p)->kind != TOK_EOF) {
        FBValue val;
        Token* t = current_token(p);

        if (t->kind == TOK_STRING_LIT) {
            val = fbval_string_from_cstr(t->value.str.text);
            advance(p);
        } else if (t->kind == TOK_INTEGER_LIT || t->kind == TOK_LONG_LIT ||
                   t->kind == TOK_SINGLE_LIT || t->kind == TOK_DOUBLE_LIT) {
            val = fbval_from_token(t);
            advance(p);
        } else if (t->kind == TOK_MINUS) {
            advance(p);
            val = fbval_from_token(current_token(p));
            double d = -fbval_to_double(&val);
            fbval_release(&val);
            val = fbval_double(d);
            advance(p);
        } else {
            // Unquoted string in DATA — read as raw text until comma/EOL
            // FB treats unquoted DATA items as strings
            val = fbval_string_from_cstr(t->value.str.text);
            advance(p);
        }

        program_add_data(p->prog, val);

        if (current_token(p)->kind == TOK_COMMA) advance(p);
    }

    // Record label association for RESTORE
    // The DATA statement itself doesn't need an AST node for execution
}
```

### 12.2 READ Statement

```c
// AST_READ
struct {
    struct ASTNode** vars;
    int              var_count;
} read_stmt;

static void exec_read(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.read_stmt.var_count; i++) {
        if (interp->data_ptr >= interp->prog->data_count) {
            fb_error(FB_ERR_OUT_OF_DATA, node->line, NULL);
            return;
        }

        FBValue data_val = fbval_copy(
            &interp->prog->data_pool[interp->data_ptr]);
        interp->data_ptr++;

        Symbol* sym = resolve_or_create_var(interp,
                          node->data.read_stmt.vars[i]);
        FBValue coerced = fbval_coerce(&data_val, sym->type);
        fbval_release(&data_val);
        fbval_release(&sym->value);
        sym->value = coerced;
    }
}
```

### 12.3 RESTORE Statement

```
RESTORE [label | linenumber]
```

```c
static void exec_restore(Interpreter* interp, ASTNode* node) {
    if (node->data.restore.has_target) {
        // RESTORE to specific DATA associated with label/line
        int target = resolve_data_label(interp->prog,
                         node->data.restore.label,
                         node->data.restore.lineno);
        if (target < 0) {
            fb_error(FB_ERR_UNDEFINED_LABEL, node->line, NULL);
            return;
        }
        interp->data_ptr = target;
    } else {
        // RESTORE with no argument — reset to beginning
        interp->data_ptr = 0;
    }
}
```

---

## 13. SELECT CASE

### 13.1 Syntax

```basic
SELECT CASE expression
    CASE value1, value2
        statements
    CASE value3 TO value4
        statements
    CASE IS > value5
        statements
    CASE ELSE
        statements
END SELECT
```

### 13.2 AST Node

```c
// AST_SELECT_CASE
struct {
    struct ASTNode* test_expr;       // The expression being tested

    // Array of CASE clauses
    struct {
        // Each clause has one or more match conditions
        struct {
            enum { CASE_VALUE, CASE_RANGE, CASE_IS, CASE_ELSE } kind;
            struct ASTNode* value;    // Single value (CASE_VALUE)
            struct ASTNode* low;      // Range low (CASE_RANGE)
            struct ASTNode* high;     // Range high (CASE_RANGE)
            TokenKind       is_op;    // Comparison op (CASE_IS)
            struct ASTNode* is_value; // Comparison value (CASE_IS)
        }* conditions;
        int condition_count;

        struct ASTNode** body;
        int              body_count;
    }* cases;
    int case_count;
} select_case;
```

### 13.3 Execute SELECT CASE

```c
static void exec_select_case(Interpreter* interp, ASTNode* node) {
    FBValue test = eval_expr(interp, node->data.select_case.test_expr);

    for (int i = 0; i < node->data.select_case.case_count; i++) {
        auto clause = &node->data.select_case.cases[i];
        int matched = 0;

        for (int j = 0; j < clause->condition_count; j++) {
            auto cond = &clause->conditions[j];

            switch (cond->kind) {
                case CASE_ELSE:
                    matched = 1;
                    break;

                case CASE_VALUE: {
                    FBValue v = eval_expr(interp, cond->value);
                    FBValue cmp = fbval_compare(&test, &v, TOK_EQ);
                    matched = fbval_is_true(&cmp);
                    fbval_release(&v);
                    break;
                }

                case CASE_RANGE: {
                    FBValue lo = eval_expr(interp, cond->low);
                    FBValue hi = eval_expr(interp, cond->high);
                    FBValue cmp_lo = fbval_compare(&test, &lo, TOK_GE);
                    FBValue cmp_hi = fbval_compare(&test, &hi, TOK_LE);
                    matched = fbval_is_true(&cmp_lo) && fbval_is_true(&cmp_hi);
                    fbval_release(&lo);
                    fbval_release(&hi);
                    break;
                }

                case CASE_IS: {
                    FBValue v = eval_expr(interp, cond->is_value);
                    FBValue cmp = fbval_compare(&test, &v, cond->is_op);
                    matched = fbval_is_true(&cmp);
                    fbval_release(&v);
                    break;
                }
            }

            if (matched) break; // Any condition in a CASE clause matches → execute
        }

        if (matched) {
            exec_block(interp, clause->body, clause->body_count);
            break; // Only one CASE executes (no fall-through)
        }
    }

    fbval_release(&test);
}
```

---

## 14. INPUT$ Function

```
INPUT$(n [, #filenum])
```

Reads exactly n characters. From keyboard if no file number; from file in Phase 5.

```c
if (_stricmp(name, "INPUT$") == 0) {
    int n = (int)fbval_to_long(&argv[0]);
    if (n <= 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, expr->line, NULL);

    // Phase 2: keyboard input only
    char* buf = malloc(n + 1);
    for (int i = 0; i < n; i++) {
        int ch;
        do { ch = console_inkey(); } while (ch == 0);
        buf[i] = (char)ch;
    }
    buf[n] = '\0';
    FBString* s = fbstr_new(buf, n);
    free(buf);
    return fbval_string(s);
}
```

---

## 15. Interpreter State Additions

```c
// Add to Interpreter struct:
typedef struct Interpreter {
    // ... Phase 1 fields ...

    // Console state
    int          current_fg;       // Current foreground color (0-15)
    int          current_bg;       // Current background color (0-7)
    int          screen_width;     // Current WIDTH setting (default 80)
    int          screen_height;    // Current HEIGHT setting (default 25)
    int          scroll_top;       // VIEW PRINT top row
    int          scroll_bottom;    // VIEW PRINT bottom row

    // DATA/READ pointer
    int          data_ptr;         // Current position in data_pool
} Interpreter;
```

---

## 16. Verification Test Files

### 16.1 `tests/verify/phase2_strings.bas` — String Functions

```basic
REM Phase 2 Test: String functions
PRINT LEFT$("Hello World", 5)
PRINT RIGHT$("Hello World", 5)
PRINT MID$("Hello World", 7)
PRINT MID$("Hello World", 7, 3)
PRINT LEN("Hello")
PRINT INSTR("Hello World", "World")
PRINT UCASE$("hello")
PRINT LCASE$("HELLO")
PRINT LTRIM$("   hello")
PRINT RTRIM$("hello   ")
PRINT STRING$(5, "*")
PRINT SPACE$(3); "X"
PRINT HEX$(255)
PRINT OCT$(255)
a$ = "Hello World"
MID$(a$, 1, 5) = "XXXXX"
PRINT a$
PRINT CHR$(65); CHR$(66); CHR$(67)
PRINT ASC("Z")
PRINT STR$(42)
PRINT VAL("3.14")
PRINT INSTR(3, "abcabc", "abc")
```

**Expected output (`tests/verify/phase2_expected/strings.txt`):**
```
Hello
World
World
Wor
 5
 7
HELLO
hello
hello
hello
*****
   X
FF
377
XXXXX World
ABC
 90
 42
 3.14
 4
```

### 16.2 `tests/verify/phase2_math.bas` — Math Functions

```basic
REM Phase 2 Test: Math functions
PRINT SIN(0)
PRINT COS(0)
PRINT TAN(0)
PRINT ATN(1) * 4
PRINT LOG(1)
PRINT EXP(0)
PRINT SQR(144)
PRINT ABS(-42)
PRINT SGN(-5); SGN(0); SGN(5)
PRINT INT(3.7)
PRINT INT(-3.7)
PRINT FIX(3.7)
PRINT FIX(-3.7)
PRINT CINT(2.5)
PRINT CINT(3.5)
RANDOMIZE 12345
PRINT RND
PRINT RND
```

**Expected output (`tests/verify/phase2_expected/math.txt`):**
```
 0
 1
 0
 3.141593
 0
 1
 12
 42
-1  0  1
 3
-4
 3
-3
 2
 4
.5765432
.3452987
```

### 16.3 `tests/verify/phase2_select.bas` — SELECT CASE

```basic
REM Phase 2 Test: SELECT CASE
DEFINT A-Z
FOR x = 1 TO 10
    SELECT CASE x
        CASE 1
            PRINT "one"
        CASE 2, 3
            PRINT "two or three"
        CASE 4 TO 7
            PRINT "four to seven"
        CASE IS > 8
            PRINT "greater than eight"
        CASE ELSE
            PRINT "eight"
    END SELECT
NEXT x
```

**Expected output (`tests/verify/phase2_expected/select.txt`):**
```
one
two or three
two or three
four to seven
four to seven
four to seven
four to seven
eight
greater than eight
greater than eight
```

### 16.4 `tests/verify/phase2_data.bas` — DATA / READ / RESTORE

```basic
REM Phase 2 Test: DATA / READ / RESTORE
DIM name$, age%
DATA "Alice", 30, "Bob", 25, "Charlie", 35

FOR i% = 1 TO 3
    READ name$, age%
    PRINT name$; " is"; age%; "years old"
NEXT i%

RESTORE
READ name$, age%
PRINT "First again:"; name$
```

**Expected output (`tests/verify/phase2_expected/data.txt`):**
```
Alice is 30 years old
Bob is 25 years old
Charlie is 35 years old
First again:Alice
```

### 16.5 `tests/verify/phase2_print_using.bas` — PRINT USING

```basic
REM Phase 2 Test: PRINT USING
PRINT USING "###.##"; 3.14
PRINT USING "$$###.##"; 42.5
PRINT USING "+###.##"; 42.5
PRINT USING "+###.##"; -42.5
PRINT USING "**###.##"; 42.5
PRINT USING "###.##^^^^"; 12345.678
PRINT USING "!"; "Hello"
PRINT USING "\   \"; "Hello World"
PRINT USING "&"; "Hello"
PRINT USING "##,###.##"; 12345.67
```

**Expected output (`tests/verify/phase2_expected/print_using.txt`):**
```
  3.14
 $42.50
 +42.50
 -42.50
**42.50
1.23E+04
H
Hello
Hello
12,345.67
```

### 16.6 `tests/verify/phase2_write.bas` — WRITE Statement

```basic
REM Phase 2 Test: WRITE statement
WRITE 1, 2, 3
WRITE "Hello", 42, "World"
WRITE 3.14, -7, ""
```

**Expected output (`tests/verify/phase2_expected/write.txt`):**
```
1,2,3
"Hello",42,"World"
3.14,-7,""
```

---

## 17. Updated Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -g -fsanitize=address
LDFLAGS = -fsanitize=address -lm

SRC = src/main.c src/lexer.c src/value.c src/symtable.c src/ast.c \
      src/parser.c src/interpreter.c src/coerce.c src/error.c \
      src/console.c src/builtins_str.c src/builtins_math.c \
      src/print_using.c
OBJ = $(SRC:.c=.o)

fbasic: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test-phase0: fbasic
	./fbasic tests/verify/phase0_lex1.bas
	./fbasic tests/verify/phase0_lex2.bas
	./fbasic tests/verify/phase0_lex3.bas

test-phase1: fbasic
	@echo "Running Phase 1 tests..."
	@for f in print expr if for loops goto fizzbuzz fibonacci primes; do \
		./fbasic tests/verify/phase1_$$f.bas > /tmp/p1_$$f.txt && \
		diff /tmp/p1_$$f.txt tests/verify/phase1_expected/$$f.txt && \
		echo "  PASS: $$f" || echo "  FAIL: $$f"; \
	done

test-phase2: fbasic
	@echo "Running Phase 2 tests..."
	@for f in strings math select data print_using write; do \
		./fbasic tests/verify/phase2_$$f.bas > /tmp/p2_$$f.txt && \
		diff /tmp/p2_$$f.txt tests/verify/phase2_expected/$$f.txt && \
		echo "  PASS: $$f" || echo "  FAIL: $$f"; \
	done

test: test-phase0 test-phase1 test-phase2
	@echo "All tests passed."

clean:
	rm -f $(OBJ) fbasic

.PHONY: test test-phase0 test-phase1 test-phase2 clean
```

---

## 18. Phase 2 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **INPUT** | Prompts correctly with `? ` or custom prompt. Multi-variable input splits by comma. Type mismatch → "Redo from start". Leading `;` suppresses newline. |
| 2 | **LINE INPUT** | Reads entire line into string variable. Custom prompt works. No comma splitting. |
| 3 | **INKEY$** | Returns empty string when no key. Returns single character for normal keys. Returns 2-char string (CHR$(0)+scancode) for extended keys. Non-blocking. |
| 4 | **CLS** | Clears screen on both Windows and POSIX. Resets print column to 1. |
| 5 | **LOCATE** | Positions cursor at specified row, col (1-based). Omitted arguments keep current position. |
| 6 | **COLOR** | Sets foreground (0-15) and background (0-7). Omitted arguments keep current value. FB color palette mapped correctly. |
| 7 | **CSRLIN / POS** | Return current cursor row/column (1-based) on both platforms. |
| 8 | **SPC / TAB** | SPC(n) outputs n spaces. TAB(n) advances to column n. Both work correctly within PRINT. |
| 9 | **WRITE** | Comma-delimited output. Strings in double quotes. Numbers without padding. Newline at end. |
| 10 | **BEEP** | Emits BEL character (\a). |
| 11 | **PRINT USING** | All format codes: `#`, `.`, `,`, `+`, `-`, `$$`, `**`, `**$`, `^^^^`, `!`, `\  \`, `&`, `_`. Overflow prefixed with `%`. Format string recycles for extra values. |
| 12 | **VIEW PRINT / WIDTH** | Scrolling region set/reset. WIDTH changes console dimensions. |
| 13 | **String Functions** | All 18 string functions (LEFT$, RIGHT$, MID$, LEN, INSTR, CHR$, ASC, STR$, VAL, UCASE$, LCASE$, LTRIM$, RTRIM$, STRING$, SPACE$, HEX$, OCT$) produce correct results. MID$ as lvalue statement works. |
| 14 | **Math Functions** | All 16 math functions (ABS, INT, FIX, SQR, SIN, COS, TAN, ATN, LOG, EXP, SGN, RND, CINT, CLNG, CSNG, CDBL) produce correct results. RND seed behavior matches FB (negative reseed, zero repeat, positive advance). |
| 15 | **RANDOMIZE** | RANDOMIZE TIMER seeds from time. Bare RANDOMIZE prompts user. RANDOMIZE n seeds from value. |
| 16 | **DATA / READ / RESTORE** | DATA values collected at parse time in order. READ advances pointer and coerces type. Out of DATA → error 4. RESTORE resets pointer (to start or to specific label). |
| 17 | **SELECT CASE** | Value lists, ranges (TO), IS relational, CASE ELSE all work. Only first matching CASE executes. String and numeric comparisons both work. |
| 18 | **INPUT$** | Reads exactly n characters from keyboard without echo. |
| 19 | **No Leaks** | All tests pass with `-fsanitize=address`. String functions properly manage ref-counts. |

---

## 19. Key Implementation Warnings

1. **INKEY$ and terminal mode:** INKEY$ requires raw/unbuffered terminal input. Initialize console mode at interpreter startup (`console_init()`) and restore at exit (`console_shutdown()`). This affects all programs, even those that don't use INKEY$. Consider lazy initialization (init on first INKEY$ call) or always init.

2. **MID$ statement vs MID$ function:** These share the same keyword but differ by context. `MID$(a$, 1, 3) = "XYZ"` is a statement (the `=` makes it an assignment). `x$ = MID$(a$, 1, 3)` is a function call. The parser must check: if `MID$` is at statement start and eventually followed by `)` then `=`, it's the statement form.

3. **DATA parsing subtlety:** Unquoted DATA items can be numbers OR strings. `DATA 42, hello, "world"` — `42` is a number, `hello` is an unquoted string, `"world"` is a quoted string. The READ target variable's type determines coercion: `READ x%` coerces to integer, `READ a$` reads as string.

4. **PRINT USING format recycling:** If there are more values than format specifiers, the format string is reused from the beginning: `PRINT USING "##"; 1; 2; 3` prints ` 1 2 3` (each formatted with `##`).

5. **FB color indices:** FB uses a 16-color palette (0-15 foreground, 0-7 background) that maps to specific CGA/EGA colors, NOT standard ANSI indices. Color 1 = dark blue (ANSI 34), color 4 = dark red (ANSI 31), etc. The mapping tables in console.c are critical.

6. **RND compatibility:** FB's RNG is a specific LCG. For exact compatibility, use the same constants (`16598013`, `12820163`, modulo `2^24`). If exact compatibility isn't needed, use C `rand()` with the same seed interface.

7. **SELECT CASE fall-through:** FB does NOT have fall-through. Only the first matching `CASE` executes, then control jumps to `END SELECT`. This differs from C switch.

8. **PRINT USING and semicolons:** `PRINT USING fmt$; val1; val2;` — the trailing semicolon suppresses the final newline (same as regular PRINT). The separator between values in PRINT USING is always `;`.
