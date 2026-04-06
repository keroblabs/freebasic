# Phase 0 — Foundation & Architecture: Detailed Implementation Guide

This document specifies every data structure, file, and API needed to build the **foundation layer** of the FreeBASIC interpreter in pure C. Phase 0 produces no runnable BASIC programs — it builds the infrastructure that Phase 1 will wire into an execution loop.

---

## Project File Structure

```
fbasic/
├── Makefile
├── include/
│   ├── fb.h              // Master include — pulls in all subsystem headers
│   ├── token.h            // Token types enum + Token struct
│   ├── lexer.h            // Lexer API
│   ├── value.h            // FBValue tagged union + string ref-counting
│   ├── symtable.h         // Symbol table (hash map + scope stack)
│   ├── ast.h              // AST node definitions
│   ├── parser.h           // Parser API (Phase 0: skeleton only)
│   ├── coerce.h           // Type coercion / promotion rules
│   └── error.h            // Error codes and reporting
├── src/
│   ├── main.c             // Entry point — load file, lex, dump tokens
│   ├── lexer.c            // Lexer implementation
│   ├── value.c            // FBValue operations + string memory
│   ├── symtable.c         // Symbol table implementation
│   ├── ast.c              // AST node constructors + free
│   ├── parser.c           // Parser skeleton (fully built in Phase 1)
│   ├── coerce.c           // Type coercion engine
│   └── error.c            // Error message table + reporting
└── tests/
    ├── test_lexer.c       // Unit tests for the lexer
    ├── test_value.c       // Unit tests for FBValue + strings
    ├── test_symtable.c    // Unit tests for symbol table
    ├── test_coerce.c      // Unit tests for type coercion
    └── verify/
        ├── phase0_lex1.bas    // Lexer verification file 1
        ├── phase0_lex2.bas    // Lexer verification file 2
        ├── phase0_lex3.bas    // Lexer verification file 3
        └── phase0_expected/   // Expected token dump outputs
            ├── lex1.txt
            ├── lex2.txt
            └── lex3.txt
```

---

## 1. Token Types (`include/token.h`)

### 1.1 Token Kind Enum

Define every token the lexer can produce. Group them logically.

```c
typedef enum {
    // === Literals ===
    TOK_INTEGER_LIT,      // 42, &H1F, &O77
    TOK_LONG_LIT,         // 42&
    TOK_SINGLE_LIT,       // 3.14, 3.14!, 1E10
    TOK_DOUBLE_LIT,       // 3.14#, 1D10, 1.23456789012345#
    TOK_STRING_LIT,       // "hello"

    // === Identifiers ===
    TOK_IDENT,            // myVar (no suffix)
    TOK_IDENT_STR,        // myVar$ (string suffix)
    TOK_IDENT_INT,        // myVar% (integer suffix)
    TOK_IDENT_LONG,       // myVar& (long suffix)
    TOK_IDENT_SINGLE,     // myVar! (single suffix)
    TOK_IDENT_DOUBLE,     // myVar# (double suffix)

    // === Operators ===
    TOK_PLUS,             // +
    TOK_MINUS,            // -
    TOK_STAR,             // *
    TOK_SLASH,            // /
    TOK_BACKSLASH,        // \ (integer division)
    TOK_CARET,            // ^
    TOK_EQ,               // =
    TOK_NE,               // <>
    TOK_LT,               // <
    TOK_GT,               // >
    TOK_LE,               // <=
    TOK_GE,               // >=

    // === Punctuation ===
    TOK_LPAREN,           // (
    TOK_RPAREN,           // )
    TOK_COMMA,            // ,
    TOK_SEMICOLON,        // ;
    TOK_COLON,            // : (statement separator)
    TOK_DOT,              // . (UDT field access)
    TOK_HASH,             // # (file number prefix)

    // === Keywords (alphabetical) ===
    TOK_KW_ABS, TOK_KW_AND, TOK_KW_AS, TOK_KW_ASC, TOK_KW_ATN,
    TOK_KW_BEEP, TOK_KW_CALL, TOK_KW_CASE,
    TOK_KW_CDBL, TOK_KW_CHR, TOK_KW_CINT, TOK_KW_CIRCLE,
    TOK_KW_CLEAR, TOK_KW_CLNG, TOK_KW_CLOSE, TOK_KW_CLS,
    TOK_KW_COLOR, TOK_KW_COMMON, TOK_KW_CONST, TOK_KW_COS,
    TOK_KW_CSNG, TOK_KW_CSRLIN,
    TOK_KW_DATA, TOK_KW_DATE, TOK_KW_DECLARE, TOK_KW_DEF,
    TOK_KW_DEFDBL, TOK_KW_DEFINT, TOK_KW_DEFLNG, TOK_KW_DEFSNG,
    TOK_KW_DEFSTR, TOK_KW_DIM, TOK_KW_DO, TOK_KW_DOUBLE, TOK_KW_DRAW,
    TOK_KW_ELSE, TOK_KW_ELSEIF, TOK_KW_END, TOK_KW_ENVIRON,
    TOK_KW_EOF, TOK_KW_EQV, TOK_KW_ERASE, TOK_KW_ERR, TOK_KW_ERL,
    TOK_KW_ERROR, TOK_KW_EXIT, TOK_KW_EXP,
    TOK_KW_FIELD, TOK_KW_FILES, TOK_KW_FIX, TOK_KW_FOR,
    TOK_KW_FRE, TOK_KW_FREEFILE, TOK_KW_FUNCTION,
    TOK_KW_GET, TOK_KW_GOSUB, TOK_KW_GOTO,
    TOK_KW_HEX, TOK_KW_IF, TOK_KW_IMP, TOK_KW_INKEY,
    TOK_KW_INP, TOK_KW_INPUT, TOK_KW_INSTR, TOK_KW_INT, TOK_KW_INTEGER,
    TOK_KW_IS,
    TOK_KW_KEY, TOK_KW_KILL,
    TOK_KW_LBOUND, TOK_KW_LCASE, TOK_KW_LEFT, TOK_KW_LEN,
    TOK_KW_LET, TOK_KW_LINE, TOK_KW_LOC, TOK_KW_LOCATE,
    TOK_KW_LOCK, TOK_KW_LOF, TOK_KW_LOG, TOK_KW_LONG, TOK_KW_LOOP,
    TOK_KW_LPRINT, TOK_KW_LSET, TOK_KW_LTRIM,
    TOK_KW_MID, TOK_KW_MKDIR, TOK_KW_MOD,
    TOK_KW_NAME, TOK_KW_NEXT, TOK_KW_NOT,
    TOK_KW_OCT, TOK_KW_ON, TOK_KW_OPEN, TOK_KW_OPTION, TOK_KW_OR,
    TOK_KW_OUT, TOK_KW_OUTPUT,
    TOK_KW_PAINT, TOK_KW_PALETTE, TOK_KW_PEEK, TOK_KW_PLAY,
    TOK_KW_PMAP, TOK_KW_POINT, TOK_KW_POKE, TOK_KW_POS,
    TOK_KW_PRESET, TOK_KW_PRINT, TOK_KW_PSET, TOK_KW_PUT,
    TOK_KW_RANDOMIZE, TOK_KW_READ, TOK_KW_REDIM, TOK_KW_REM,
    TOK_KW_RESET, TOK_KW_RESTORE, TOK_KW_RESUME, TOK_KW_RETURN,
    TOK_KW_RIGHT, TOK_KW_RMDIR, TOK_KW_RND, TOK_KW_RSET,
    TOK_KW_RTRIM, TOK_KW_RUN,
    TOK_KW_SCREEN, TOK_KW_SEEK, TOK_KW_SEG, TOK_KW_SELECT,
    TOK_KW_SGN, TOK_KW_SHARED, TOK_KW_SHELL, TOK_KW_SIN,
    TOK_KW_SINGLE, TOK_KW_SLEEP, TOK_KW_SOUND, TOK_KW_SPACE,
    TOK_KW_SPC, TOK_KW_SQR, TOK_KW_STATIC, TOK_KW_STEP,
    TOK_KW_STICK, TOK_KW_STOP, TOK_KW_STR, TOK_KW_STRIG,
    TOK_KW_STRING, TOK_KW_SUB, TOK_KW_SWAP, TOK_KW_SYSTEM,
    TOK_KW_TAB, TOK_KW_TAN, TOK_KW_THEN, TOK_KW_TIME,
    TOK_KW_TIMER, TOK_KW_TO, TOK_KW_TROFF, TOK_KW_TRON,
    TOK_KW_TYPE,
    TOK_KW_UBOUND, TOK_KW_UCASE, TOK_KW_UNLOCK, TOK_KW_UNTIL,
    TOK_KW_USING,
    TOK_KW_VAL, TOK_KW_VARPTR, TOK_KW_VARSEG, TOK_KW_VIEW,
    TOK_KW_WAIT, TOK_KW_WEND, TOK_KW_WHILE, TOK_KW_WIDTH,
    TOK_KW_WINDOW, TOK_KW_WRITE,
    TOK_KW_XOR,

    // === Special ===
    TOK_LINENO,           // Line number at start of line (e.g. 100)
    TOK_LABEL,            // Label followed by colon (e.g. MyLabel:)
    TOK_EOL,              // End of logical line
    TOK_EOF,              // End of file

    TOK_COUNT             // Total number of token types
} TokenKind;
```

### 1.2 Token Struct

```c
typedef struct {
    TokenKind kind;
    int       line;          // Source line number (1-based)
    int       col;           // Source column (1-based)
    union {
        int16_t  int_val;    // TOK_INTEGER_LIT
        int32_t  long_val;   // TOK_LONG_LIT, TOK_LINENO
        float    single_val; // TOK_SINGLE_LIT
        double   double_val; // TOK_DOUBLE_LIT
        struct {
            char*   text;    // Heap-allocated, null-terminated
            int     length;
        } str;               // TOK_STRING_LIT, TOK_IDENT*, TOK_LABEL
    } value;
} Token;
```

### 1.3 Keyword Lookup

Use a **static sorted array** of `{ const char* name; TokenKind kind; }` with binary search. Case-insensitive comparison via `_stricmp` (Windows) / `strcasecmp` (POSIX). Approximately 160 keywords.

---

## 2. Lexer (`include/lexer.h`, `src/lexer.c`)

### 2.1 Lexer State

```c
typedef struct {
    const char* source;      // Entire source text
    int         source_len;
    int         pos;         // Current byte position
    int         line;        // Current line (1-based)
    int         col;         // Current column (1-based)
    Token*      tokens;      // Dynamic array of produced tokens
    int         token_count;
    int         token_cap;
} Lexer;
```

### 2.2 Lexer API

```c
// Initialize lexer with source code. Returns 0 on success.
int  lexer_init(Lexer* lex, const char* source, int source_len);

// Tokenize entire source. Returns 0 on success, -1 on error.
int  lexer_tokenize(Lexer* lex);

// Free all token memory.
void lexer_free(Lexer* lex);

// Debug: print all tokens to stdout.
void lexer_dump(const Lexer* lex);
```

### 2.3 Lexer Rules (Implementation Notes)

**Character-by-character scanner.** The main loop calls `lexer_next_token()` repeatedly until EOF.

| Input Pattern | Action |
|---|---|
| `' ...` or `REM ...` (after SOL or `:`) | Skip to EOL, emit nothing (or `TOK_REM` if you want to preserve comments) |
| `"..."` | Scan string literal. FB has no escape chars. `""` inside string = literal `"`. |
| `0-9` at start of line (before any non-whitespace) | Parse as line number `TOK_LINENO` |
| `0-9` or `.` (not at start of line) | Parse numeric literal — see rules below |
| `&H` | Hex literal: scan `[0-9A-Fa-f]+`, then optional `%` or `&` suffix |
| `&O` or `&` followed by octal digit | Octal literal: scan `[0-7]+` |
| `A-Za-z` | Identifier or keyword. Scan `[A-Za-z0-9._]*` then check for suffix `%`, `&`, `!`, `#`, `$`. Look up in keyword table (only if no suffix). |
| Identifier followed by `:` at end of statement position | Label definition `TOK_LABEL` |
| `_` at end of line (preceded by space) | Line continuation — eat `_`, newline, and leading whitespace of next line |
| `:` | Statement separator `TOK_COLON` |
| `<>` | `TOK_NE` |
| `<=` | `TOK_LE` |
| `>=` | `TOK_GE` |
| `<` | `TOK_LT` |
| `>` | `TOK_GT` |
| `=` | `TOK_EQ` (assignment AND comparison — disambiguated by parser) |
| Newline / CR+LF | `TOK_EOL` |

**Numeric literal scanning rules:**

1. Start with digits → accumulate integer part.
2. If `.` follows → floating point, accumulate fractional part.
3. If `E` or `e` follows → single-precision exponent. If `D` or `d` → double-precision exponent. Scan optional `+`/`-` and digits.
4. Suffix: `%` → integer, `&` → long, `!` → single, `#` → double.
5. Default (no suffix, no decimal, no exponent): if value fits int16 → `TOK_INTEGER_LIT`, else if fits int32 → `TOK_LONG_LIT`, else → `TOK_SINGLE_LIT`.
6. Default (has decimal or `E` exponent): `TOK_SINGLE_LIT`. Has `D` exponent or `#` suffix: `TOK_DOUBLE_LIT`.

---

## 3. Value System (`include/value.h`, `src/value.c`)

### 3.1 FBString (ref-counted)

```c
typedef struct {
    char*    data;       // Null-terminated for convenience
    int32_t  len;        // Actual length (may contain embedded NULs)
    int32_t  capacity;   // Allocated size of data buffer
    int32_t  refcount;   // Reference count (starts at 1)
} FBString;

// Allocate a new string with given content. refcount = 1.
FBString* fbstr_new(const char* text, int32_t len);

// Create empty string of length 0.
FBString* fbstr_empty(void);

// Increment refcount.
void fbstr_ref(FBString* s);

// Decrement refcount; free if zero.
void fbstr_unref(FBString* s);

// Return a writable copy (copy-on-write: if refcount > 1, duplicate).
FBString* fbstr_cow(FBString* s);

// Concatenate two strings, return new string (refcount = 1).
FBString* fbstr_concat(const FBString* a, const FBString* b);

// Substring (0-based start, len). New string.
FBString* fbstr_mid(const FBString* s, int32_t start, int32_t len);

// Compare: returns <0, 0, >0 like strcmp.
int fbstr_compare(const FBString* a, const FBString* b);
```

### 3.2 FBValue Tagged Union

```c
typedef enum {
    FB_INTEGER,    // int16_t
    FB_LONG,       // int32_t
    FB_SINGLE,     // float (32-bit IEEE)
    FB_DOUBLE,     // double (64-bit IEEE)
    FB_STRING      // FBString*
} FBType;

typedef struct {
    FBType type;
    union {
        int16_t    ival;     // FB_INTEGER
        int32_t    lval;     // FB_LONG
        float      sval;     // FB_SINGLE
        double     dval;     // FB_DOUBLE
        FBString*  str;      // FB_STRING
    } as;
} FBValue;

// Create values
FBValue fbval_int(int16_t v);
FBValue fbval_long(int32_t v);
FBValue fbval_single(float v);
FBValue fbval_double(double v);
FBValue fbval_string(FBString* s);   // Takes ownership (refs the string)
FBValue fbval_string_from_cstr(const char* text);  // Convenience

// Release a value's resources (unref string if FB_STRING).
void fbval_release(FBValue* v);

// Deep-copy a value (ref string, copy numeric).
FBValue fbval_copy(const FBValue* v);

// Convert value to a specific C double (for arithmetic). Returns 0.0 for strings → error.
double fbval_to_double(const FBValue* v);

// Convert value to int32 (with rounding as per FB rules).
int32_t fbval_to_long(const FBValue* v);

// Format value as display string (for PRINT). Returns heap-allocated C string.
// Numbers: leading space for positive, "-" for negative, trailing space.
char* fbval_format_print(const FBValue* v);
```

---

## 4. Type Coercion Engine (`include/coerce.h`, `src/coerce.c`)

### 4.1 Promotion Rules

The FB type hierarchy for arithmetic: `INTEGER < LONG < SINGLE < DOUBLE`.

```c
// Return the "wider" of two numeric types for binary operations.
// E.g., fb_promote_type(FB_INTEGER, FB_SINGLE) → FB_SINGLE
// Returns FB_STRING unchanged if both are strings (for concatenation).
// Returns -1 (error) for mismatched string+numeric.
FBType fb_promote_type(FBType a, FBType b);

// Coerce a FBValue to a target type. Returns new value.
// Integer → Long: sign-extend
// Integer/Long → Single/Double: convert
// Single → Double: widen
// Double → Single: narrow (may lose precision)
// Float → Integer/Long: round using "banker's rounding" (round half to even)
// String ↔ Numeric: TYPE MISMATCH error
FBValue fbval_coerce(const FBValue* v, FBType target);

// Perform binary arithmetic: a OP b. Auto-promotes, returns result.
// Handles: +, -, *, /, \, MOD, ^ for numerics; + for strings.
FBValue fbval_binary_op(const FBValue* a, const FBValue* b, TokenKind op);

// Perform unary operation: -, +, NOT.
FBValue fbval_unary_op(const FBValue* v, TokenKind op);

// Perform relational comparison. Returns FB_INTEGER: -1 (true) or 0 (false).
// Works for numeric (promotes then compares) and string (lexicographic).
FBValue fbval_compare(const FBValue* a, const FBValue* b, TokenKind op);

// Logical operations (AND, OR, XOR, EQV, IMP, NOT).
// Operands coerced to LONG, then bitwise operation, result is LONG.
// (FB does bitwise on integers, not short-circuit boolean.)
FBValue fbval_logical_op(const FBValue* a, const FBValue* b, TokenKind op);
```

### 4.2 Integer Division and MOD

```c
// Integer division (\): both operands rounded to LONG, then C integer division.
// MOD: both operands rounded to LONG, then C % operator.
// Division by zero → runtime error 11.
```

### 4.3 Truth Values

```c
// FB truth: 0 = FALSE, non-zero (typically -1) = TRUE.
// All comparisons return -1 (TRUE) or 0 (FALSE) as FB_INTEGER.
#define FB_TRUE  ((int16_t)-1)     // 0xFFFF — all bits set
#define FB_FALSE ((int16_t)0)

// Test if a FBValue is "truthy" (for IF conditions).
int fbval_is_true(const FBValue* v);
```

---

## 5. Symbol Table (`include/symtable.h`, `src/symtable.c`)

### 5.1 Variable Entry

```c
typedef enum {
    SYM_VARIABLE,
    SYM_CONST,
    SYM_ARRAY,      // Phase 3 — placeholder for now
    SYM_SUB,         // Phase 4
    SYM_FUNCTION,    // Phase 4
    SYM_TYPE_DEF,    // Phase 3 — TYPE...END TYPE
    SYM_LABEL        // Line label for GOTO/GOSUB
} SymKind;

typedef struct {
    char      name[42];    // FB max 40 chars + suffix + NUL
    SymKind   kind;
    FBType    type;        // Data type of the variable
    FBValue   value;       // Current value (for SYM_VARIABLE, SYM_CONST)
    int       is_shared;   // DIM SHARED flag
    int       is_static;   // STATIC flag
    // For SYM_LABEL:
    int       target_line; // Index into statement array
} Symbol;
```

### 5.2 Scope

```c
#define SYMTAB_BUCKETS 256

typedef struct Scope {
    Symbol**       buckets;     // Hash table: array of linked lists
    int            bucket_count;
    struct Scope*  parent;      // Enclosing scope (NULL for module-level)
    // DEFtype ranges: default type for variables starting with letter A-Z.
    // deftype[0] = default for 'A', deftype[25] = default for 'Z'.
    // Initialized to FB_SINGLE (FreeBASIC default).
    FBType         deftype[26];
} Scope;

// Create new scope. If parent is NULL, this is module-level.
Scope* scope_new(Scope* parent);

// Free scope and all its symbols.
void scope_free(Scope* scope);

// Look up a symbol by name. Case-insensitive.
// Searches current scope first, then parent scopes.
Symbol* scope_lookup(Scope* scope, const char* name);

// Look up ONLY in the current scope (no parent search).
Symbol* scope_lookup_local(Scope* scope, const char* name);

// Insert a new symbol into the current scope. Returns pointer to it.
// Returns NULL if already exists in current scope.
Symbol* scope_insert(Scope* scope, const char* name, SymKind kind, FBType type);

// Resolve the default type for a variable name using DEFtype setting.
// Uses the first letter of the name to index into deftype[].
FBType scope_default_type(Scope* scope, const char* name);
```

### 5.3 Case-Insensitive Hashing

```c
// FNV-1a hash on the uppercased name.
static uint32_t sym_hash(const char* name) {
    uint32_t h = 2166136261u;
    for (const char* p = name; *p; p++) {
        h ^= (uint8_t)toupper(*p);
        h *= 16777619u;
    }
    return h;
}
```

---

## 6. AST Nodes (`include/ast.h`, `src/ast.c`)

### 6.1 Node Kinds

```c
typedef enum {
    // Expressions
    AST_LITERAL,          // Numeric or string literal
    AST_VARIABLE,         // Variable reference (name + type suffix)
    AST_BINARY_OP,        // a OP b
    AST_UNARY_OP,         // OP a
    AST_FUNC_CALL,        // Built-in function call (ABS, LEFT$, etc.)
    AST_PAREN,            // Parenthesized expression

    // Statements (Phase 0 defines them, Phase 1 implements execution)
    AST_PRINT,
    AST_LET,              // Assignment
    AST_DIM,
    AST_CONST_DECL,
    AST_IF,               // Block IF
    AST_IF_SINGLE,        // Single-line IF
    AST_FOR,
    AST_WHILE,
    AST_DO_LOOP,
    AST_SELECT_CASE,
    AST_GOTO,
    AST_GOSUB,
    AST_RETURN,
    AST_END,
    AST_STOP,
    AST_REM,
    AST_LABEL_DEF,        // Label: or line number definition
    AST_DEFTYPE,
    AST_COLON_SEP,        // Multiple statements on one line

    // Placeholders for later phases
    AST_INPUT,
    AST_LINE_INPUT,
    AST_SUB_DEF,
    AST_FUNCTION_DEF,
    AST_CALL,
    AST_OPEN,
    AST_CLOSE,
    AST_EXIT,

    AST_NODE_COUNT
} ASTKind;
```

### 6.2 AST Node Struct

Use a flexible struct with a union for kind-specific data. Each node carries its source line number.

```c
typedef struct ASTNode {
    ASTKind kind;
    int     line;         // Source line for error reporting

    union {
        // AST_LITERAL
        struct { FBValue value; } literal;

        // AST_VARIABLE
        struct { char name[42]; FBType type_hint; } variable;

        // AST_BINARY_OP
        struct {
            TokenKind       op;
            struct ASTNode* left;
            struct ASTNode* right;
        } binop;

        // AST_UNARY_OP
        struct {
            TokenKind       op;
            struct ASTNode* operand;
        } unop;

        // AST_FUNC_CALL
        struct {
            char            name[42];
            struct ASTNode** args;     // Array of argument expressions
            int             arg_count;
        } func_call;

        // AST_PRINT
        struct {
            struct ASTNode** items;    // Array of expressions to print
            int             item_count;
            int*            separators; // TOK_SEMICOLON, TOK_COMMA, or 0 (none)
            int             trailing_sep; // 1 if ends with ; or ,
        } print;

        // AST_LET
        struct {
            struct ASTNode* target;    // Variable or array element
            struct ASTNode* expr;
        } let;

        // AST_IF (block form)
        struct {
            struct ASTNode*  condition;
            struct ASTNode** then_body;    // statements
            int              then_count;
            struct ASTNode** elseif_cond;  // array of ELSEIF conditions
            struct ASTNode***elseif_body;  // array of arrays of statements
            int*             elseif_count; // statement count per ELSEIF
            int              elseif_n;     // number of ELSEIF clauses
            struct ASTNode** else_body;
            int              else_count;
        } if_block;

        // AST_FOR
        struct {
            struct ASTNode* var;       // Loop variable
            struct ASTNode* start;     // Start expression
            struct ASTNode* end;       // End expression
            struct ASTNode* step;      // Step expression (NULL = default 1)
            struct ASTNode** body;
            int             body_count;
        } for_loop;

        // AST_WHILE / AST_DO_LOOP
        struct {
            struct ASTNode*  condition; // NULL if condition-less DO...LOOP
            struct ASTNode** body;
            int              body_count;
            int              is_until;  // 0=WHILE, 1=UNTIL
            int              is_post;   // 0=pre-test, 1=post-test
        } loop;

        // AST_GOTO / AST_GOSUB
        struct {
            char   label[42];
            int    lineno;    // -1 if using label, else line number
        } jump;

        // AST_DIM
        struct {
            char    name[42];
            FBType  type;
            int     is_shared;
        } dim;

        // AST_CONST_DECL
        struct {
            char    name[42];
            struct ASTNode* value_expr;
        } const_decl;

        // AST_DEFTYPE
        struct {
            FBType  type;       // The target type
            char    range_start; // 'A'..'Z'
            char    range_end;   // 'A'..'Z'
        } deftype;

    } data;
} ASTNode;

// === AST Constructor API ===
ASTNode* ast_literal(int line, FBValue value);
ASTNode* ast_variable(int line, const char* name, FBType type_hint);
ASTNode* ast_binop(int line, TokenKind op, ASTNode* left, ASTNode* right);
ASTNode* ast_unop(int line, TokenKind op, ASTNode* operand);
ASTNode* ast_print(int line, ASTNode** items, int* seps, int count, int trailing);
ASTNode* ast_let(int line, ASTNode* target, ASTNode* expr);
ASTNode* ast_for(int line, ASTNode* var, ASTNode* start, ASTNode* end,
                 ASTNode* step, ASTNode** body, int body_count);
ASTNode* ast_goto(int line, const char* label, int lineno);
// ... constructors for all other node kinds ...

// Free an AST node and all its children recursively.
void ast_free(ASTNode* node);
```

---

## 7. Program Representation (`include/parser.h` skeleton)

### 7.1 Program Structure

```c
typedef struct {
    ASTNode**   statements;     // Flat array of top-level statements
    int         stmt_count;
    int         stmt_cap;

    // Label → statement index lookup (for GOTO/GOSUB)
    struct {
        char    name[42];
        int     stmt_index;
    }*          labels;
    int         label_count;
    int         label_cap;

    // Line number → statement index lookup
    struct {
        int32_t lineno;
        int     stmt_index;
    }*          line_map;
    int         linemap_count;
    int         linemap_cap;

    // DATA pool (for DATA/READ/RESTORE — Phase 2, but allocate now)
    FBValue*    data_pool;
    int         data_count;
    int         data_cap;
} Program;

// Parse token array into a Program. Returns 0 on success.
int parser_parse(const Token* tokens, int token_count, Program* prog);

// Free the program.
void program_free(Program* prog);

// Lookup label by name. Returns statement index, or -1 if not found.
int program_find_label(const Program* prog, const char* name);

// Lookup line number. Returns statement index, or -1 if not found.
int program_find_lineno(const Program* prog, int32_t lineno);
```

---

## 8. Error System (`include/error.h`, `src/error.c`)

```c
typedef enum {
    FB_ERR_NONE = 0,
    FB_ERR_NEXT_WITHOUT_FOR = 1,
    FB_ERR_SYNTAX = 2,
    FB_ERR_RETURN_WITHOUT_GOSUB = 3,
    FB_ERR_OUT_OF_DATA = 4,
    FB_ERR_ILLEGAL_FUNC_CALL = 5,
    FB_ERR_OVERFLOW = 6,
    FB_ERR_OUT_OF_MEMORY = 7,
    FB_ERR_UNDEFINED_LABEL = 8,
    FB_ERR_SUBSCRIPT_OUT_OF_RANGE = 9,
    FB_ERR_DUPLICATE_DEFINITION = 10,
    FB_ERR_DIVISION_BY_ZERO = 11,
    FB_ERR_ILLEGAL_IN_DIRECT = 12,
    FB_ERR_TYPE_MISMATCH = 13,
    FB_ERR_OUT_OF_STRING_SPACE = 14,
    // ... (define all ~75 FB error codes)
    FB_ERR_FILE_NOT_FOUND = 53,
    FB_ERR_BAD_FILE_MODE = 54,
    FB_ERR_FILE_ALREADY_OPEN = 55,
    FB_ERR_INPUT_PAST_END = 62,
    FB_ERR_BAD_RECORD_NUMBER = 63,
    FB_ERR_BAD_FILE_NAME = 64,
    FB_ERR_PATH_NOT_FOUND = 76,
} FBErrorCode;

// Report a runtime error. In Phase 0, just prints and exits.
// Later phases will route through ON ERROR.
void fb_error(FBErrorCode code, int line, const char* extra_msg);

// Report a compile/parse error and abort.
void fb_syntax_error(int line, int col, const char* msg);

// Get human-readable error message for a code.
const char* fb_error_message(FBErrorCode code);
```

---

## 9. Main Entry Point (`src/main.c`) — Phase 0 Verification Driver

For Phase 0, `main.c` is a **lexer + data-structure test harness**, not a full interpreter.

```c
#include "fb.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: fbasic <file.bas>\n");
        return 1;
    }

    // 1. Load source file
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("Cannot open file"); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = malloc(fsize + 1);
    fread(source, 1, fsize, f);
    source[fsize] = '\0';
    fclose(f);

    // 2. Tokenize
    Lexer lex;
    lexer_init(&lex, source, (int)fsize);
    if (lexer_tokenize(&lex) != 0) {
        fprintf(stderr, "Lexer failed.\n");
        free(source);
        return 1;
    }

    // 3. Dump tokens (Phase 0 verification)
    printf("=== TOKEN DUMP ===\n");
    lexer_dump(&lex);
    printf("=== %d tokens ===\n", lex.token_count);

    // 4. Test value system
    printf("\n=== VALUE SYSTEM TEST ===\n");
    FBValue a = fbval_int(42);
    FBValue b = fbval_double(3.14);
    FBValue c = fbval_binary_op(&a, &b, TOK_STAR);
    char* formatted = fbval_format_print(&c);
    printf("42 * 3.14 = %s\n", formatted);
    free(formatted);
    fbval_release(&c);

    FBValue s1 = fbval_string_from_cstr("Hello, ");
    FBValue s2 = fbval_string_from_cstr("World!");
    FBValue s3 = fbval_binary_op(&s1, &s2, TOK_PLUS);
    char* sformatted = fbval_format_print(&s3);
    printf("Concat: %s\n", sformatted);
    free(sformatted);
    fbval_release(&s1);
    fbval_release(&s2);
    fbval_release(&s3);

    // 5. Test symbol table
    printf("\n=== SYMBOL TABLE TEST ===\n");
    Scope* global = scope_new(NULL);
    Symbol* sym = scope_insert(global, "counter%", SYM_VARIABLE, FB_INTEGER);
    sym->value = fbval_int(0);
    Symbol* found = scope_lookup(global, "COUNTER%");
    if (found) {
        printf("Found '%s' type=%d val=%d\n",
               found->name, found->type, found->value.as.ival);
    }
    // Test DEFtype
    global->deftype['I' - 'A'] = FB_INTEGER; // DEFINT I
    FBType resolved = scope_default_type(global, "index");
    printf("Default type for 'index': %d (expected %d=FB_INTEGER)\n",
           resolved, FB_INTEGER);
    scope_free(global);

    // 6. Test type coercion
    printf("\n=== COERCION TEST ===\n");
    FBValue iv = fbval_int(7);
    FBValue dv = fbval_double(2.5);
    FBType promoted = fb_promote_type(iv.type, dv.type);
    printf("int + double promotes to type %d (expected %d=FB_DOUBLE)\n",
           promoted, FB_DOUBLE);
    FBValue coerced = fbval_coerce(&iv, FB_DOUBLE);
    printf("7 as double = %f\n", coerced.as.dval);

    // Integer rounding test (FB rounds 0.5 to nearest even)
    FBValue fv = fbval_double(2.5);
    FBValue rounded = fbval_coerce(&fv, FB_INTEGER);
    printf("2.5 → int = %d (FB banker's round = 2)\n", rounded.as.ival);
    fv = fbval_double(3.5);
    rounded = fbval_coerce(&fv, FB_INTEGER);
    printf("3.5 → int = %d (FB banker's round = 4)\n", rounded.as.ival);

    // Integer division
    FBValue i10 = fbval_int(10);
    FBValue i3 = fbval_int(3);
    FBValue idiv = fbval_binary_op(&i10, &i3, TOK_BACKSLASH);
    printf("10 \\ 3 = %d (expected 3)\n", idiv.as.ival);
    FBValue imod = fbval_binary_op(&i10, &i3, TOK_KW_MOD);
    printf("10 MOD 3 = %d (expected 1)\n", imod.as.ival);

    // Logical NOT
    FBValue trueval = fbval_int(FB_TRUE);
    FBValue notval = fbval_unary_op(&trueval, TOK_KW_NOT);
    printf("NOT -1 = %d (expected 0)\n", notval.as.ival);

    lexer_free(&lex);
    free(source);
    printf("\nPhase 0 verification PASSED.\n");
    return 0;
}
```

---

## 10. Verification Test Files

### 10.1 `tests/verify/phase0_lex1.bas` — Basic Token Coverage

```basic
REM Phase 0 Lexer Test 1: Basic tokens
' This is also a comment
10 PRINT "Hello, World!"
20 LET x% = 42
30 LET y! = 3.14
40 LET name$ = "FreeBASIC"
50 LET big& = 100000
60 LET precise# = 1.23456789012345#
END
```

**Expected token dump (`tests/verify/phase0_expected/lex1.txt`):**

```
[  1] TOK_KW_REM
[  2] TOK_EOL
[  3] TOK_LINENO(10)
[  3] TOK_KW_PRINT
[  3] TOK_STRING_LIT("Hello, World!")
[  3] TOK_EOL
[  4] TOK_LINENO(20)
[  4] TOK_KW_LET
[  4] TOK_IDENT_INT("x")
[  4] TOK_EQ
[  4] TOK_INTEGER_LIT(42)
[  4] TOK_EOL
[  5] TOK_LINENO(30)
[  5] TOK_KW_LET
[  5] TOK_IDENT_SINGLE("y")
[  5] TOK_EQ
[  5] TOK_SINGLE_LIT(3.14)
[  5] TOK_EOL
[  6] TOK_LINENO(40)
[  6] TOK_KW_LET
[  6] TOK_IDENT_STR("name")
[  6] TOK_EQ
[  6] TOK_STRING_LIT("FreeBASIC")
[  6] TOK_EOL
[  7] TOK_LINENO(50)
[  7] TOK_KW_LET
[  7] TOK_IDENT_LONG("big")
[  7] TOK_EQ
[  7] TOK_INTEGER_LIT(100000) → TOK_LONG_LIT(100000)
[  7] TOK_EOL
[  8] TOK_LINENO(60)
[  8] TOK_KW_LET
[  8] TOK_IDENT_DOUBLE("precise")
[  8] TOK_EQ
[  8] TOK_DOUBLE_LIT(1.23456789012345)
[  8] TOK_EOL
[  9] TOK_KW_END
[  9] TOK_EOL
[ 10] TOK_EOF
```

### 10.2 `tests/verify/phase0_lex2.bas` — Operators, Hex/Octal, Expressions

```basic
REM Phase 0 Lexer Test 2: Operators and special literals
DEFINT A-Z
DIM result AS INTEGER

result = 10 + 5 * 2 - 3
result = 15 \ 4
result = 17 MOD 5
result = 2 ^ 8

IF result >= 256 AND result <= 256 THEN
    PRINT "Power!"
END IF

x = &HFF
y = &O77
z = (x <> y) OR (x > y)
w = NOT z
flag = (x = y) EQV (a = b)
mask = a IMP b
```

**Expected tokens include:**

```
TOK_KW_DEFINT, TOK_IDENT("A"), TOK_MINUS, TOK_IDENT("Z")
TOK_KW_DIM, TOK_IDENT("result"), TOK_KW_AS, TOK_KW_INTEGER
...
TOK_INTEGER_LIT(10), TOK_PLUS, TOK_INTEGER_LIT(5), TOK_STAR, TOK_INTEGER_LIT(2), TOK_MINUS, TOK_INTEGER_LIT(3)
TOK_BACKSLASH, TOK_KW_MOD, TOK_CARET
...
TOK_KW_IF, TOK_GE, TOK_KW_AND, TOK_LE, TOK_KW_THEN
...
TOK_INTEGER_LIT(255)       ← &HFF parsed as 255
TOK_INTEGER_LIT(63)        ← &O77 parsed as 63
TOK_NE, TOK_KW_OR, TOK_GT
TOK_KW_NOT
TOK_KW_EQV
TOK_KW_IMP
```

### 10.3 `tests/verify/phase0_lex3.bas` — Labels, Colons, Line Continuation, Strings

```basic
REM Phase 0 Lexer Test 3: Labels, multi-statement lines, edge cases
MyLabel:
    PRINT "Line 1": PRINT "Line 2": REM two statements on one line

longLine$ = "This is a " + _
    "continued line"

emptyStr$ = ""
quoteInStr$ = "He said ""hello"" to me"

GOTO MyLabel
GOSUB MyLabel
RETURN
```

**Expected tokens include:**

```
TOK_LABEL("MyLabel")
TOK_KW_PRINT, TOK_STRING_LIT("Line 1"), TOK_COLON
TOK_KW_PRINT, TOK_STRING_LIT("Line 2"), TOK_COLON, TOK_KW_REM
...
TOK_IDENT_STR("longLine"), TOK_EQ, TOK_STRING_LIT("This is a "), TOK_PLUS
TOK_STRING_LIT("continued line")    ← continuation joined
...
TOK_STRING_LIT("")                   ← empty string
TOK_STRING_LIT("He said ""hello"" to me")  ← internal quotes preserved or unescaped
...
TOK_KW_GOTO, TOK_IDENT("MyLabel")
TOK_KW_GOSUB, TOK_IDENT("MyLabel")
TOK_KW_RETURN
```

---

## 11. Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -g -fsanitize=address
LDFLAGS = -fsanitize=address -lm

SRC = src/main.c src/lexer.c src/value.c src/symtable.c src/ast.c \
      src/coerce.c src/error.c
OBJ = $(SRC:.c=.o)

fbasic: $(OBJ)
    $(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
    $(CC) $(CFLAGS) -c -o $@ $<

test: fbasic
    ./fbasic tests/verify/phase0_lex1.bas
    ./fbasic tests/verify/phase0_lex2.bas
    ./fbasic tests/verify/phase0_lex3.bas

clean:
    rm -f $(OBJ) fbasic

.PHONY: test clean
```

---

## 12. Phase 0 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **Lexer** | Correctly tokenizes all 3 test `.bas` files. Token dump matches expected output. Handles: keywords (case-insensitive), identifiers with type suffixes, all numeric literal forms (decimal, hex, octal, with suffixes), string literals (with embedded `""`), operators (including multi-char `<>`, `<=`, `>=`), line numbers, labels, `:` separator, `_` continuation, `REM`/`'` comments. |
| 2 | **FBValue** | `fbval_int()`, `fbval_long()`, `fbval_single()`, `fbval_double()`, `fbval_string_from_cstr()` all allocate correctly. `fbval_release()` frees strings. `fbval_copy()` increments string refcount. |
| 3 | **FBString** | `fbstr_new()` allocates with refcount=1. `fbstr_ref()` increments. `fbstr_unref()` decrements and frees at 0. `fbstr_concat()`, `fbstr_mid()`, `fbstr_compare()` work correctly. No memory leaks under ASan. |
| 4 | **Symbol Table** | Case-insensitive insert/lookup works. Scope chaining works (child→parent lookup). DEFtype defaults resolve correctly. Duplicate insert returns NULL. |
| 5 | **Type Coercion** | Promotion: INT+DOUBLE → DOUBLE. Coercion: 7→7.0 (int→double). Rounding: 2.5→2, 3.5→4 (banker's rounding). Integer division: 10\3=3. MOD: 10 MOD 3=1. Logical NOT -1 = 0. String+String = concatenation. String+Number = TYPE MISMATCH error. |
| 6 | **AST Constructors** | `ast_literal()`, `ast_variable()`, `ast_binop()` create valid nodes. `ast_free()` recursively frees without leaks. |
| 7 | **Error System** | `fb_error()` prints error code + message + line. `fb_syntax_error()` prints line:col + message. All FB error codes have human-readable messages. |
| 8 | **No Leaks** | All tests pass with `-fsanitize=address` and zero leak/error reports. |

---

## 13. Key Implementation Warnings

1. **FB number formatting for PRINT:** Positive numbers are printed with a **leading space** (where the sign would go) followed by the number followed by a **trailing space**. Negative numbers have `-` instead of the leading space. Example: `PRINT 42` outputs ` 42 ` (space-42-space). Implement this in `fbval_format_print()` now — Phase 1 will rely on it immediately.

2. **String `""` inside string literals:** FB uses `""` to represent a single `"` inside a string. The lexer must handle this: `"He said ""hi"""` → `He said "hi"`. Store the unescaped content.

3. **`=` is both assignment and comparison:** The lexer emits `TOK_EQ` for both. The **parser** (Phase 1) disambiguates based on context (statement position = assignment; inside expression = comparison).

4. **Keywords vs identifiers with suffixes:** `INT` is a keyword, but `INT%` is never valid — the parser rejects it. However, `INTEGER` is a keyword (for `AS INTEGER`), and `integer%` would be an identifier called "integer" with an int suffix. The lexer should try keyword lookup **only for bare identifiers** (no suffix).

5. **DEFtype initial state:** FreeBASIC defaults all letters A-Z to `SINGLE` (not INTEGER). So `x = 3.14` with no DIM and no DEFINT makes `x` a SINGLE. Initialize `deftype[0..25] = FB_SINGLE`.

6. **Line numbers vs numeric literals:** A number at the **very start** of a line (first non-whitespace token) is a line number (`TOK_LINENO`). The same number appearing anywhere else is a numeric literal. The lexer must track "start of line" state.
