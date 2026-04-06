# Phase 1 — Core Interpreter + Basic Statements: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build the **core execution layer** of the FreeBASIC interpreter in pure C. Phase 1 wires the Phase 0 infrastructure (lexer, value system, symbol table, AST, coercion) into a working interpreter. By the end of Phase 1, the interpreter can run real BASIC programs: FizzBuzz, Fibonacci, prime sieves, and simple loops.

---

## Project File Structure (Phase 1 additions)

Files **added** or **significantly modified** relative to Phase 0 are marked with `[NEW]` or `[MOD]`.

```
fbasic/
├── Makefile                        [MOD] — add new source files
├── include/
│   ├── fb.h                       [MOD] — pull in new headers
│   ├── token.h
│   ├── lexer.h
│   ├── value.h
│   ├── symtable.h
│   ├── ast.h                      [MOD] — finalize statement node details
│   ├── parser.h                   [MOD] — full parser API
│   ├── interpreter.h              [NEW] — interpreter state + execution API
│   ├── coerce.h
│   └── error.h
├── src/
│   ├── main.c                     [MOD] — lex → parse → interpret pipeline
│   ├── lexer.c
│   ├── value.c
│   ├── symtable.c
│   ├── ast.c                      [MOD] — new node constructors
│   ├── parser.c                   [MOD] — full recursive-descent parser
│   ├── interpreter.c              [NEW] — tree-walking interpreter
│   ├── coerce.c
│   └── error.c                    [MOD] — add runtime error paths
└── tests/
    ├── test_lexer.c
    ├── test_value.c
    ├── test_symtable.c
    ├── test_coerce.c
    ├── test_parser.c              [NEW] — parser unit tests
    ├── test_interpreter.c         [NEW] — interpreter unit tests
    └── verify/
        ├── phase0_lex1.bas
        ├── phase0_lex2.bas
        ├── phase0_lex3.bas
        ├── phase1_print.bas       [NEW] — PRINT formatting tests
        ├── phase1_expr.bas        [NEW] — expression evaluation tests
        ├── phase1_if.bas          [NEW] — IF block & single-line tests
        ├── phase1_for.bas         [NEW] — FOR...NEXT tests
        ├── phase1_loops.bas       [NEW] — WHILE/WEND + DO/LOOP tests
        ├── phase1_goto.bas        [NEW] — GOTO + GOSUB/RETURN tests
        ├── phase1_fizzbuzz.bas    [NEW] — milestone: FizzBuzz
        ├── phase1_fibonacci.bas   [NEW] — milestone: Fibonacci sequence
        ├── phase1_primes.bas      [NEW] — milestone: prime sieve
        ├── phase0_expected/
        │   ├── lex1.txt
        │   ├── lex2.txt
        │   └── lex3.txt
        └── phase1_expected/       [NEW]
            ├── print.txt
            ├── expr.txt
            ├── if.txt
            ├── for.txt
            ├── loops.txt
            ├── goto.txt
            ├── fizzbuzz.txt
            ├── fibonacci.txt
            └── primes.txt
```

---

## 1. Expression Parser (`src/parser.c`)

### 1.1 Operator Precedence Table

The parser uses a **precedence-climbing** (Pratt parser) approach. Operator precedence from **lowest to highest** (FB rules):

| Precedence | Operators       | Associativity | Notes                         |
|------------|-----------------|---------------|-------------------------------|
| 1 (lowest) | `IMP`           | Left          | Logical implication           |
| 2          | `EQV`           | Left          | Logical equivalence           |
| 3          | `XOR`           | Left          | Bitwise XOR                   |
| 4          | `OR`            | Left          | Bitwise OR                    |
| 5          | `AND`           | Left          | Bitwise AND                   |
| 6          | `NOT`           | Prefix/Unary  | Bitwise NOT                   |
| 7          | `=` `<>` `<` `>` `<=` `>=` | Left | Relational comparison       |
| 8          | `+` `-`         | Left          | Addition, subtraction, string `+` |
| 9          | `MOD`           | Left          | Integer modulus               |
| 10         | `\`             | Left          | Integer division              |
| 11         | `*` `/`         | Left          | Multiplication, float division|
| 12         | Unary `-` `+`   | Prefix        | Negation, positive            |
| 13 (highest)| `^`            | Right         | Exponentiation                |

### 1.2 Precedence-Climbing Algorithm

```c
// Parse an expression with minimum precedence level.
// Entry point: parse_expr(parser, 1)
static ASTNode* parse_expr(Parser* p, int min_prec);

// Parse primary expression (literal, variable, parenthesized expr, function call, unary op).
static ASTNode* parse_primary(Parser* p);

// Get precedence of a binary operator token. Returns 0 if not a binary operator.
static int op_precedence(TokenKind tok);

// Get associativity: 0 = left, 1 = right.
static int op_right_assoc(TokenKind tok);
```

**Implementation pseudocode:**

```c
static ASTNode* parse_expr(Parser* p, int min_prec) {
    ASTNode* left = parse_primary(p);

    while (1) {
        TokenKind op = current_token(p)->kind;
        int prec = op_precedence(op);
        if (prec == 0 || prec < min_prec) break;

        advance(p); // consume operator

        int next_prec = op_right_assoc(op) ? prec : prec + 1;
        ASTNode* right = parse_expr(p, next_prec);
        left = ast_binop(current_line(p), op, left, right);
    }
    return left;
}

static ASTNode* parse_primary(Parser* p) {
    Token* t = current_token(p);

    // Unary NOT (precedence 6)
    if (t->kind == TOK_KW_NOT) {
        advance(p);
        ASTNode* operand = parse_expr(p, 6); // bind tighter than relationals
        return ast_unop(t->line, TOK_KW_NOT, operand);
    }

    // Unary +/-  (precedence 12)
    if (t->kind == TOK_MINUS || t->kind == TOK_PLUS) {
        advance(p);
        ASTNode* operand = parse_expr(p, 12);
        return ast_unop(t->line, t->kind, operand);
    }

    // Numeric literal
    if (t->kind == TOK_INTEGER_LIT || t->kind == TOK_LONG_LIT ||
        t->kind == TOK_SINGLE_LIT || t->kind == TOK_DOUBLE_LIT) {
        ASTNode* node = ast_literal_from_token(t);
        advance(p);
        return node;
    }

    // String literal
    if (t->kind == TOK_STRING_LIT) {
        ASTNode* node = ast_literal(t->line,
            fbval_string_from_cstr(t->value.str.text));
        advance(p);
        return node;
    }

    // Parenthesized expression
    if (t->kind == TOK_LPAREN) {
        advance(p); // consume '('
        ASTNode* inner = parse_expr(p, 1);
        expect(p, TOK_RPAREN); // consume ')'
        return ast_paren(t->line, inner);
    }

    // Identifier (variable or built-in function call)
    if (is_identifier(t->kind)) {
        // Check for built-in function: name followed by '('
        if (peek_token(p, 1)->kind == TOK_LPAREN &&
            is_builtin_function(t->value.str.text)) {
            return parse_func_call(p);
        }
        ASTNode* node = ast_variable(t->line, t->value.str.text,
                                      ident_type_from_token(t));
        advance(p);
        return node;
    }

    fb_syntax_error(t->line, t->col, "Expected expression");
    return NULL; // unreachable
}
```

### 1.3 String Expression Handling

- `+` between two strings → concatenation (handled in `fbval_binary_op`)
- `+` between string and numeric → TYPE MISMATCH error (runtime error 13)
- Relational operators between strings → lexicographic comparison (case-sensitive, as per FB)
- All other arithmetic operators on strings → TYPE MISMATCH error

### 1.4 Built-in Function Parsing (Phase 1 subset)

Phase 1 implements only the functions needed by the milestone programs. Other functions are added in Phase 2.

```c
// Functions available in Phase 1 (minimal set for numeric programs):
// ABS(x), INT(x), FIX(x), SGN(x), SQR(x), CINT(x), CLNG(x), CSNG(x), CDBL(x)
// CHR$(n), ASC(s$), STR$(n), VAL(s$), LEN(s$)

static ASTNode* parse_func_call(Parser* p) {
    Token* name_tok = current_token(p);
    char func_name[42];
    strncpy(func_name, name_tok->value.str.text, 41);
    func_name[41] = '\0';
    int line = name_tok->line;

    advance(p); // consume function name
    expect(p, TOK_LPAREN); // consume '('

    // Parse comma-separated argument list
    ASTNode* args[16]; // FB built-ins have at most ~3 args
    int argc = 0;

    if (current_token(p)->kind != TOK_RPAREN) {
        args[argc++] = parse_expr(p, 1);
        while (current_token(p)->kind == TOK_COMMA) {
            advance(p); // consume ','
            args[argc++] = parse_expr(p, 1);
        }
    }
    expect(p, TOK_RPAREN); // consume ')'

    // Copy args into heap array
    ASTNode** arg_array = malloc(argc * sizeof(ASTNode*));
    memcpy(arg_array, args, argc * sizeof(ASTNode*));

    return ast_func_call(line, func_name, arg_array, argc);
}
```

---

## 2. Parser — Statement Parsing (`src/parser.c`)

### 2.1 Parser State

```c
typedef struct {
    const Token* tokens;
    int          token_count;
    int          pos;           // Current token index
    Program*     prog;          // Output program being built

    // Parsing context flags
    int          in_if_block;   // Nesting depth of IF blocks
    int          in_for_loop;   // Nesting depth of FOR loops
    int          in_while_loop; // Nesting depth of WHILE loops
    int          in_do_loop;    // Nesting depth of DO loops
} Parser;
```

### 2.2 Top-Level Parse Loop

```c
int parser_parse(const Token* tokens, int token_count, Program* prog) {
    Parser p;
    p.tokens = tokens;
    p.token_count = token_count;
    p.pos = 0;
    p.prog = prog;
    p.in_if_block = 0;
    p.in_for_loop = 0;
    p.in_while_loop = 0;
    p.in_do_loop = 0;

    while (current_token(&p)->kind != TOK_EOF) {
        // Skip blank lines
        if (current_token(&p)->kind == TOK_EOL) {
            advance(&p);
            continue;
        }

        // Line number or label
        if (current_token(&p)->kind == TOK_LINENO) {
            int32_t lineno = current_token(&p)->value.long_val;
            advance(&p);
            program_add_lineno(prog, lineno, prog->stmt_count);
        }
        if (current_token(&p)->kind == TOK_LABEL) {
            char label_name[42];
            strncpy(label_name, current_token(&p)->value.str.text, 41);
            advance(&p);
            program_add_label(prog, label_name, prog->stmt_count);
        }

        // Parse statement(s) on line — handle ':' separator
        parse_statement(&p);

        // Expect EOL or EOF after statement(s)
        if (current_token(&p)->kind == TOK_EOL) {
            advance(&p);
        } else if (current_token(&p)->kind != TOK_EOF) {
            fb_syntax_error(current_token(&p)->line,
                            current_token(&p)->col,
                            "Expected end of line");
        }
    }

    return 0;
}
```

### 2.3 Statement Dispatcher

```c
static void parse_statement(Parser* p) {
    Token* t = current_token(p);

    switch (t->kind) {
        case TOK_KW_PRINT:    parse_print(p); break;
        case TOK_KW_LET:      advance(p); parse_assignment(p); break;
        case TOK_KW_IF:       parse_if(p); break;
        case TOK_KW_FOR:      parse_for(p); break;
        case TOK_KW_WHILE:    parse_while(p); break;
        case TOK_KW_DO:       parse_do_loop(p); break;
        case TOK_KW_GOTO:     parse_goto(p); break;
        case TOK_KW_GOSUB:    parse_gosub(p); break;
        case TOK_KW_RETURN:   parse_return(p); break;
        case TOK_KW_DIM:      parse_dim(p); break;
        case TOK_KW_CONST:    parse_const(p); break;
        case TOK_KW_DEFINT:
        case TOK_KW_DEFLNG:
        case TOK_KW_DEFSNG:
        case TOK_KW_DEFDBL:
        case TOK_KW_DEFSTR:   parse_deftype(p); break;
        case TOK_KW_END:      parse_end(p); break;
        case TOK_KW_STOP:     parse_stop(p); break;
        case TOK_KW_SYSTEM:   parse_system(p); break;
        case TOK_KW_REM:      parse_rem(p); break;
        case TOK_KW_NEXT:     /* handled by FOR parser */ break;
        case TOK_KW_WEND:     /* handled by WHILE parser */ break;
        case TOK_KW_LOOP:     /* handled by DO parser */ break;

        default:
            // Identifier at statement start → implicit assignment (LET without keyword)
            if (is_identifier(t->kind)) {
                parse_assignment(p);
            } else if (t->kind == TOK_COLON) {
                // Empty statement before colon
                advance(p);
                if (current_token(p)->kind != TOK_EOL &&
                    current_token(p)->kind != TOK_EOF) {
                    parse_statement(p);
                }
            } else if (t->kind != TOK_EOL && t->kind != TOK_EOF) {
                fb_syntax_error(t->line, t->col,
                                "Unexpected token at statement start");
            }
            break;
    }

    // Handle ':' statement separator
    if (current_token(p)->kind == TOK_COLON) {
        advance(p);
        if (current_token(p)->kind != TOK_EOL &&
            current_token(p)->kind != TOK_EOF) {
            parse_statement(p);
        }
    }
}
```

---

## 3. PRINT Statement Parsing & Execution

### 3.1 Parse PRINT

```c
static void parse_print(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume PRINT

    ASTNode* items[256];
    int seps[256];
    int count = 0;
    int trailing = 0;

    while (current_token(p)->kind != TOK_EOL &&
           current_token(p)->kind != TOK_EOF &&
           current_token(p)->kind != TOK_COLON) {

        // SPC(n) and TAB(n) are special print modifiers
        if (current_token(p)->kind == TOK_KW_SPC ||
            current_token(p)->kind == TOK_KW_TAB) {
            // Parse as pseudo-function call for Phase 2; skip for Phase 1
        }

        items[count] = parse_expr(p, 1);
        trailing = 0;

        // Check for separator
        if (current_token(p)->kind == TOK_SEMICOLON) {
            seps[count] = TOK_SEMICOLON;
            trailing = 1;
            advance(p);
        } else if (current_token(p)->kind == TOK_COMMA) {
            seps[count] = TOK_COMMA;
            trailing = 1;
            advance(p);
        } else {
            seps[count] = 0;
        }
        count++;
    }

    // Copy to heap
    ASTNode** item_arr = malloc(count * sizeof(ASTNode*));
    int* sep_arr = malloc(count * sizeof(int));
    memcpy(item_arr, items, count * sizeof(ASTNode*));
    memcpy(sep_arr, seps, count * sizeof(int));

    ASTNode* node = ast_print(line, item_arr, sep_arr, count, trailing);
    program_add_stmt(p->prog, node);
}
```

### 3.2 Execute PRINT

```c
static void exec_print(Interpreter* interp, ASTNode* node) {
    int col = interp->print_col; // Track current print column (1-based)

    for (int i = 0; i < node->data.print.item_count; i++) {
        FBValue val = eval_expr(interp, node->data.print.items[i]);
        char* text = fbval_format_print(&val);

        printf("%s", text);
        col += (int)strlen(text);

        free(text);
        fbval_release(&val);

        // Handle separator
        int sep = node->data.print.separators[i];
        if (sep == TOK_COMMA) {
            // Advance to next 14-character print zone
            int next_zone = ((col - 1) / 14 + 1) * 14 + 1;
            while (col < next_zone) {
                putchar(' ');
                col++;
            }
        }
        // TOK_SEMICOLON: no spacing — items are concatenated
    }

    if (!node->data.print.trailing_sep) {
        putchar('\n');
        col = 1;
    }

    interp->print_col = col;
}
```

### 3.3 Number Formatting Rules (PRINT)

The `fbval_format_print()` function (implemented in Phase 0's `value.c`) must follow these exact rules:

| Value | Output String | Notes |
|-------|--------------|-------|
| `42` | `" 42 "` | Leading space (sign placeholder) + trailing space |
| `-7` | `"-7 "` | `-` replaces leading space + trailing space |
| `0` | `" 0 "` | Zero is positive → leading space |
| `3.14` | `" 3.14 "` | Decimal shown for SINGLE/DOUBLE |
| `1E+10` | `" 1E+10 "` | Scientific notation for large SINGLE |
| `1D+10` | `" 1D+10 "` | FB uses 'D' for DOUBLE exponent |
| `""` | `""` | Strings print with no padding |
| `"hello"` | `"hello"` | Strings print literally, no quotes |

**PRINT with commas (14-char zones):**
```basic
PRINT 1, 2, 3
```
Output: ` 1             2             3 ` (each value starts at column 1, 15, 29, ...)

**PRINT with semicolons (compact):**
```basic
PRINT 1; 2; 3
```
Output: ` 1  2  3 ` (numbers still have leading/trailing spaces)

**PRINT with trailing semicolon:**
```basic
PRINT "Hello ";
PRINT "World"
```
Output: `Hello World` (no newline after first PRINT)

---

## 4. Assignment (`[LET] var = expr`)

### 4.1 Parse Assignment

```c
static void parse_assignment(Parser* p) {
    int line = current_token(p)->line;

    // Parse target variable
    ASTNode* target = parse_variable_ref(p);

    // Expect '='
    expect(p, TOK_EQ);

    // Parse value expression
    ASTNode* expr = parse_expr(p, 1);

    ASTNode* node = ast_let(line, target, expr);
    program_add_stmt(p->prog, node);
}

// Parse a variable reference (identifier with optional type suffix).
// In Phase 3 this expands to handle array subscripts and UDT fields.
static ASTNode* parse_variable_ref(Parser* p) {
    Token* t = current_token(p);
    if (!is_identifier(t->kind)) {
        fb_syntax_error(t->line, t->col, "Expected variable name");
    }
    ASTNode* node = ast_variable(t->line, t->value.str.text,
                                  ident_type_from_token(t));
    advance(p);
    return node;
}
```

### 4.2 Execute Assignment

```c
static void exec_let(Interpreter* interp, ASTNode* node) {
    // Evaluate the right-hand side expression
    FBValue rhs = eval_expr(interp, node->data.let.expr);

    // Resolve the target variable
    const char* name = node->data.let.target->data.variable.name;
    FBType type_hint = node->data.let.target->data.variable.type_hint;

    // Look up or create the variable
    Symbol* sym = scope_lookup(interp->current_scope, name);
    if (!sym) {
        // Auto-create variable on first assignment (FB behavior)
        FBType vartype;
        if (type_hint != FB_INTEGER || has_type_suffix(name)) {
            vartype = type_hint;
        } else {
            vartype = scope_default_type(interp->current_scope, name);
        }
        sym = scope_insert(interp->current_scope, name, SYM_VARIABLE, vartype);
    }

    // Check CONST
    if (sym->kind == SYM_CONST) {
        fb_error(FB_ERR_DUPLICATE_DEFINITION, node->line,
                 "Cannot assign to CONST");
        return;
    }

    // Coerce RHS to variable's declared type
    FBValue coerced = fbval_coerce(&rhs, sym->type);
    fbval_release(&rhs);

    // Release old value and assign new
    fbval_release(&sym->value);
    sym->value = coerced;
}
```

---

## 5. REM / Comments

### 5.1 Parse REM

```c
static void parse_rem(Parser* p) {
    // REM and ' comments — skip to end of line
    // The lexer already consumed the comment text,
    // so we just advance past the REM token.
    advance(p); // consume TOK_KW_REM
    // No AST node emitted (comments are discarded)
}
```

No AST node is generated. The parser simply consumes the `TOK_KW_REM` token. The lexer has already skipped the comment body and the next token will be `TOK_EOL`.

---

## 6. END / STOP / SYSTEM

### 6.1 Parse

```c
static void parse_end(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume END

    // Check for "END IF", "END SUB", "END FUNCTION", etc.
    // In Phase 1, only "END IF" is relevant; bare "END" terminates program.
    if (current_token(p)->kind == TOK_KW_IF) {
        // This is handled by parse_if — don't emit a standalone node here.
        // Rewind if needed or handle in parse_if context.
        return;
    }

    program_add_stmt(p->prog, ast_end(line, AST_END));
}

static void parse_stop(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume STOP
    program_add_stmt(p->prog, ast_end(line, AST_STOP));
}

static void parse_system(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume SYSTEM
    program_add_stmt(p->prog, ast_end(line, AST_END));
}
```

### 6.2 Execute

```c
static void exec_end(Interpreter* interp, ASTNode* node) {
    (void)node;
    interp->running = 0; // Stop the main execution loop

    if (node->kind == AST_STOP) {
        // STOP: print "Break in line NNN" and stop (FB behavior)
        printf("Break in line %d\n", node->line);
    }
}
```

---

## 7. GOTO

### 7.1 Parse GOTO

```c
static void parse_goto(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume GOTO

    Token* target = current_token(p);
    if (target->kind == TOK_INTEGER_LIT || target->kind == TOK_LONG_LIT ||
        target->kind == TOK_LINENO) {
        // GOTO line_number
        int32_t lineno = (target->kind == TOK_INTEGER_LIT)
                         ? target->value.int_val
                         : target->value.long_val;
        advance(p);
        program_add_stmt(p->prog, ast_goto(line, NULL, lineno));
    } else if (is_identifier(target->kind)) {
        // GOTO label
        char label[42];
        strncpy(label, target->value.str.text, 41);
        label[41] = '\0';
        advance(p);
        program_add_stmt(p->prog, ast_goto(line, label, -1));
    } else {
        fb_syntax_error(line, target->col,
                        "Expected line number or label after GOTO");
    }
}
```

### 7.2 Execute GOTO

```c
static void exec_goto(Interpreter* interp, ASTNode* node) {
    int target_idx;

    if (node->data.jump.lineno >= 0) {
        target_idx = program_find_lineno(interp->prog,
                                          node->data.jump.lineno);
        if (target_idx < 0) {
            fb_error(FB_ERR_UNDEFINED_LABEL, node->line,
                     "Undefined line number");
            return;
        }
    } else {
        target_idx = program_find_label(interp->prog,
                                         node->data.jump.label);
        if (target_idx < 0) {
            fb_error(FB_ERR_UNDEFINED_LABEL, node->line,
                     "Undefined label");
            return;
        }
    }

    interp->pc = target_idx; // Jump — main loop will execute from here
}
```

---

## 8. IF...THEN...ELSE

### 8.1 Single-Line IF

**Syntax:** `IF condition THEN statements [ELSE statements]`

Where `THEN` can be followed by a line number (implicit GOTO).

```c
static void parse_if_single_line(Parser* p, int line, ASTNode* condition) {
    // Already consumed: IF, condition, THEN

    // Check if THEN is followed by a line number (IF x THEN 100)
    if (current_token(p)->kind == TOK_INTEGER_LIT ||
        current_token(p)->kind == TOK_LONG_LIT) {
        int32_t lineno = (current_token(p)->kind == TOK_INTEGER_LIT)
                         ? current_token(p)->value.int_val
                         : current_token(p)->value.long_val;
        advance(p);
        ASTNode* goto_node = ast_goto(line, NULL, lineno);
        ASTNode** then_body = malloc(sizeof(ASTNode*));
        then_body[0] = goto_node;

        ASTNode* node = ast_if_single(line, condition,
                                       then_body, 1, NULL, 0);
        program_add_stmt(p->prog, node);
        return;
    }

    // Parse THEN-clause statements (up to ELSE or EOL)
    ASTNode* then_stmts[64];
    int then_count = 0;
    while (current_token(p)->kind != TOK_KW_ELSE &&
           current_token(p)->kind != TOK_EOL &&
           current_token(p)->kind != TOK_EOF) {
        then_stmts[then_count++] = parse_inline_statement(p);
        if (current_token(p)->kind == TOK_COLON) advance(p);
    }

    // ELSE clause
    ASTNode* else_stmts[64];
    int else_count = 0;
    if (current_token(p)->kind == TOK_KW_ELSE) {
        advance(p); // consume ELSE

        // ELSE followed by line number → implicit GOTO
        if (current_token(p)->kind == TOK_INTEGER_LIT ||
            current_token(p)->kind == TOK_LONG_LIT) {
            int32_t lineno = (current_token(p)->kind == TOK_INTEGER_LIT)
                             ? current_token(p)->value.int_val
                             : current_token(p)->value.long_val;
            advance(p);
            else_stmts[else_count++] = ast_goto(line, NULL, lineno);
        } else {
            while (current_token(p)->kind != TOK_EOL &&
                   current_token(p)->kind != TOK_EOF) {
                else_stmts[else_count++] = parse_inline_statement(p);
                if (current_token(p)->kind == TOK_COLON) advance(p);
            }
        }
    }

    // Build node — copy arrays to heap
    ASTNode** then_arr = copy_node_array(then_stmts, then_count);
    ASTNode** else_arr = copy_node_array(else_stmts, else_count);

    ASTNode* node = ast_if_single(line, condition,
                                   then_arr, then_count,
                                   else_arr, else_count);
    program_add_stmt(p->prog, node);
}
```

### 8.2 Block IF

**Syntax:**
```basic
IF condition THEN
    statements
[ELSEIF condition THEN
    statements] ...
[ELSE
    statements]
END IF
```

```c
static void parse_if_block(Parser* p, int line, ASTNode* condition) {
    // Already consumed: IF, condition, THEN, EOL

    // Parse THEN-body
    ASTNode** then_body = NULL;
    int then_count = 0;
    parse_block(p, &then_body, &then_count,
                TOK_KW_ELSEIF, TOK_KW_ELSE, TOK_KW_END);

    // Collect ELSEIF chains
    ASTNode** elseif_conds = NULL;
    ASTNode*** elseif_bodies = NULL;
    int* elseif_counts = NULL;
    int elseif_n = 0;

    while (current_token(p)->kind == TOK_KW_ELSEIF) {
        advance(p); // consume ELSEIF
        ASTNode* elseif_cond = parse_expr(p, 1);
        expect(p, TOK_KW_THEN);
        expect_eol(p);

        ASTNode** elseif_body = NULL;
        int elseif_count = 0;
        parse_block(p, &elseif_body, &elseif_count,
                    TOK_KW_ELSEIF, TOK_KW_ELSE, TOK_KW_END);

        // Grow ELSEIF arrays
        elseif_n++;
        elseif_conds = realloc(elseif_conds,
                               elseif_n * sizeof(ASTNode*));
        elseif_bodies = realloc(elseif_bodies,
                                elseif_n * sizeof(ASTNode**));
        elseif_counts = realloc(elseif_counts,
                                elseif_n * sizeof(int));

        elseif_conds[elseif_n - 1] = elseif_cond;
        elseif_bodies[elseif_n - 1] = elseif_body;
        elseif_counts[elseif_n - 1] = elseif_count;
    }

    // ELSE clause
    ASTNode** else_body = NULL;
    int else_count = 0;
    if (current_token(p)->kind == TOK_KW_ELSE) {
        advance(p); // consume ELSE
        expect_eol(p);
        parse_block(p, &else_body, &else_count, TOK_KW_END, -1, -1);
    }

    // Expect END IF
    expect(p, TOK_KW_END);
    expect(p, TOK_KW_IF);

    ASTNode* node = ast_if_block(line, condition,
                                  then_body, then_count,
                                  elseif_conds, elseif_bodies,
                                  elseif_counts, elseif_n,
                                  else_body, else_count);
    program_add_stmt(p->prog, node);
}
```

### 8.3 IF Dispatcher

```c
static void parse_if(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume IF

    ASTNode* condition = parse_expr(p, 1);
    expect(p, TOK_KW_THEN);

    // Determine single-line vs block:
    // If next token is EOL → block IF; otherwise → single-line IF
    if (current_token(p)->kind == TOK_EOL) {
        advance(p); // consume EOL
        parse_if_block(p, line, condition);
    } else {
        parse_if_single_line(p, line, condition);
    }
}
```

### 8.4 Execute IF

```c
static void exec_if(Interpreter* interp, ASTNode* node) {
    // Evaluate main condition
    FBValue cond = eval_expr(interp, node->data.if_block.condition);
    int is_true = fbval_is_true(&cond);
    fbval_release(&cond);

    if (is_true) {
        exec_block(interp, node->data.if_block.then_body,
                   node->data.if_block.then_count);
        return;
    }

    // Check ELSEIF chains
    for (int i = 0; i < node->data.if_block.elseif_n; i++) {
        FBValue econd = eval_expr(interp,
                                   node->data.if_block.elseif_cond[i]);
        int etrue = fbval_is_true(&econd);
        fbval_release(&econd);

        if (etrue) {
            exec_block(interp, node->data.if_block.elseif_body[i],
                       node->data.if_block.elseif_count[i]);
            return;
        }
    }

    // ELSE clause
    if (node->data.if_block.else_body) {
        exec_block(interp, node->data.if_block.else_body,
                   node->data.if_block.else_count);
    }
}
```

---

## 9. FOR...NEXT

### 9.1 Parse FOR

**Syntax:** `FOR var = start TO end [STEP step]` ... `NEXT [var [, var ...]]`

```c
static void parse_for(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume FOR

    // Loop variable
    ASTNode* var = parse_variable_ref(p);
    expect(p, TOK_EQ);

    // Start value
    ASTNode* start = parse_expr(p, 1);
    expect(p, TOK_KW_TO);

    // End value
    ASTNode* end_val = parse_expr(p, 1);

    // Optional STEP
    ASTNode* step = NULL;
    if (current_token(p)->kind == TOK_KW_STEP) {
        advance(p);
        step = parse_expr(p, 1);
    }

    expect_eol(p);

    // Parse body until NEXT
    ASTNode** body = NULL;
    int body_count = 0;
    parse_block_until_next(p, &body, &body_count);

    // Consume NEXT and optional variable name(s)
    expect(p, TOK_KW_NEXT);
    // NEXT may specify the loop variable (or multiple: NEXT j, i)
    if (is_identifier(current_token(p)->kind)) {
        // Verify it matches our FOR variable (warning only)
        advance(p);
        // Handle "NEXT i, j" (nested FOR unwinding) — eat comma-separated vars
        while (current_token(p)->kind == TOK_COMMA) {
            advance(p);
            if (is_identifier(current_token(p)->kind)) {
                advance(p);
            }
            // Each consumed variable pops an outer FOR in the stack
            // The parser records these for multi-NEXT handling
        }
    }

    ASTNode* node = ast_for(line, var, start, end_val, step,
                            body, body_count);
    program_add_stmt(p->prog, node);
}
```

### 9.2 Execute FOR

```c
static void exec_for(Interpreter* interp, ASTNode* node) {
    // Initialize loop variable
    FBValue start_val = eval_expr(interp, node->data.for_loop.start);
    exec_assign_value(interp, node->data.for_loop.var, start_val);

    // Evaluate end and step
    FBValue end_val = eval_expr(interp, node->data.for_loop.end);
    FBValue step_val;
    if (node->data.for_loop.step) {
        step_val = eval_expr(interp, node->data.for_loop.step);
    } else {
        step_val = fbval_int(1); // Default STEP 1
    }

    // Determine step direction
    double step_d = fbval_to_double(&step_val);
    int step_positive = (step_d >= 0.0);

    // FB FOR semantics: body executes at least once if initial condition met.
    // Loop while: (step > 0 AND var <= end) OR (step < 0 AND var >= end)
    // OR (step == 0 → infinite loop, as per FB)
    while (interp->running) {
        // Check termination condition
        FBValue current = get_variable_value(interp,
                              node->data.for_loop.var);
        double cur_d = fbval_to_double(&current);
        double end_d = fbval_to_double(&end_val);
        fbval_release(&current);

        if (step_d == 0.0) {
            // Infinite loop — FB behavior
        } else if (step_positive && cur_d > end_d) {
            break;
        } else if (!step_positive && cur_d < end_d) {
            break;
        }

        // Execute body
        exec_block(interp, node->data.for_loop.body,
                   node->data.for_loop.body_count);

        if (!interp->running) break;

        // Increment loop variable: var = var + step
        FBValue cur = get_variable_value(interp,
                          node->data.for_loop.var);
        FBValue new_val = fbval_binary_op(&cur, &step_val, TOK_PLUS);
        exec_assign_value(interp, node->data.for_loop.var, new_val);
        fbval_release(&cur);
    }

    fbval_release(&end_val);
    fbval_release(&step_val);
}
```

### 9.3 FOR Loop Semantics (FB-specific)

1. **STEP 0** → infinite loop (FB does not error).
2. **Floating-point counters** are allowed: `FOR x! = 0.1 TO 1.0 STEP 0.1` — beware of float accumulation; FB does not guard against it.
3. **Exit condition checked before each iteration** (pre-test), including the first.
4. **NEXT with multiple variables:** `NEXT j, i` ends both inner FOR j and outer FOR i. When parsing at the top level (flat statement list, not nested AST), each variable in a multi-NEXT pops the corresponding FOR from a runtime FOR stack. In a nested-AST approach, the parser nests FOR nodes and a multi-NEXT simply closes multiple nesting levels.
5. **Type of loop variable determines arithmetic precision.** If `i%` is INTEGER, then `i% = i% + step` uses integer arithmetic. If `x!` is SINGLE, it uses SINGLE precision.

---

## 10. WHILE...WEND

### 10.1 Parse WHILE

```c
static void parse_while(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume WHILE

    ASTNode* condition = parse_expr(p, 1);
    expect_eol(p);

    // Parse body until WEND
    ASTNode** body = NULL;
    int body_count = 0;
    parse_block(p, &body, &body_count, TOK_KW_WEND, -1, -1);

    expect(p, TOK_KW_WEND);

    ASTNode* node = ast_loop(line, condition, body, body_count,
                             /*is_until=*/0, /*is_post=*/0);
    program_add_stmt(p->prog, node);
}
```

### 10.2 Execute WHILE

```c
static void exec_while(Interpreter* interp, ASTNode* node) {
    while (interp->running) {
        FBValue cond = eval_expr(interp, node->data.loop.condition);
        int is_true = fbval_is_true(&cond);
        fbval_release(&cond);
        if (!is_true) break;

        exec_block(interp, node->data.loop.body,
                   node->data.loop.body_count);
    }
}
```

---

## 11. DO...LOOP

### 11.1 All Four Variants

| Syntax | Pre/Post | WHILE/UNTIL |
|--------|----------|-------------|
| `DO WHILE cond` ... `LOOP` | Pre-test | WHILE |
| `DO UNTIL cond` ... `LOOP` | Pre-test | UNTIL |
| `DO` ... `LOOP WHILE cond` | Post-test | WHILE |
| `DO` ... `LOOP UNTIL cond` | Post-test | UNTIL |
| `DO` ... `LOOP` | Infinite (no condition) | — |

### 11.2 Parse DO...LOOP

```c
static void parse_do_loop(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume DO

    ASTNode* condition = NULL;
    int is_until = 0;
    int is_post = 0;

    // Check for pre-test condition: DO WHILE / DO UNTIL
    if (current_token(p)->kind == TOK_KW_WHILE) {
        advance(p);
        condition = parse_expr(p, 1);
        is_until = 0;
        is_post = 0;
    } else if (current_token(p)->kind == TOK_KW_UNTIL) {
        advance(p);
        condition = parse_expr(p, 1);
        is_until = 1;
        is_post = 0;
    }

    expect_eol(p);

    // Parse body until LOOP
    ASTNode** body = NULL;
    int body_count = 0;
    parse_block(p, &body, &body_count, TOK_KW_LOOP, -1, -1);

    expect(p, TOK_KW_LOOP);

    // Check for post-test condition: LOOP WHILE / LOOP UNTIL
    if (!condition) {
        if (current_token(p)->kind == TOK_KW_WHILE) {
            advance(p);
            condition = parse_expr(p, 1);
            is_until = 0;
            is_post = 1;
        } else if (current_token(p)->kind == TOK_KW_UNTIL) {
            advance(p);
            condition = parse_expr(p, 1);
            is_until = 1;
            is_post = 1;
        }
        // If still no condition → infinite DO...LOOP
    }

    ASTNode* node = ast_loop(line, condition, body, body_count,
                             is_until, is_post);
    program_add_stmt(p->prog, node);
}
```

### 11.3 Execute DO...LOOP

```c
static void exec_do_loop(Interpreter* interp, ASTNode* node) {
    while (interp->running) {
        // Pre-test condition
        if (node->data.loop.condition && !node->data.loop.is_post) {
            FBValue cond = eval_expr(interp,
                                      node->data.loop.condition);
            int result = fbval_is_true(&cond);
            fbval_release(&cond);
            if (node->data.loop.is_until) result = !result;
            if (!result) break;
        }

        // Execute body
        exec_block(interp, node->data.loop.body,
                   node->data.loop.body_count);
        if (!interp->running) break;

        // Post-test condition
        if (node->data.loop.condition && node->data.loop.is_post) {
            FBValue cond = eval_expr(interp,
                                      node->data.loop.condition);
            int result = fbval_is_true(&cond);
            fbval_release(&cond);
            if (node->data.loop.is_until) result = !result;
            if (!result) break;
        }

        // No condition → infinite loop (exit only via EXIT DO — Phase 4)
    }
}
```

---

## 12. GOSUB...RETURN

### 12.1 Runtime Return Stack

```c
// In Interpreter struct:
typedef struct {
    int return_pc;    // Statement index to return to
    int line;         // Source line of GOSUB (for error messages)
} GosubFrame;

#define GOSUB_STACK_MAX 256

typedef struct Interpreter {
    Program*     prog;
    Scope*       global_scope;
    Scope*       current_scope;
    int          pc;            // Program counter (statement index)
    int          running;       // 1 = executing, 0 = stopped

    int          print_col;     // Current print column for PRINT zones

    // GOSUB return stack
    GosubFrame   gosub_stack[GOSUB_STACK_MAX];
    int          gosub_sp;      // Stack pointer (0 = empty)
} Interpreter;
```

### 12.2 Parse GOSUB / RETURN

```c
static void parse_gosub(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume GOSUB

    Token* target = current_token(p);
    if (target->kind == TOK_INTEGER_LIT || target->kind == TOK_LONG_LIT) {
        int32_t lineno = (target->kind == TOK_INTEGER_LIT)
                         ? target->value.int_val
                         : target->value.long_val;
        advance(p);
        ASTNode* node = ast_alloc(AST_GOSUB, line);
        node->data.jump.lineno = lineno;
        node->data.jump.label[0] = '\0';
        program_add_stmt(p->prog, node);
    } else if (is_identifier(target->kind)) {
        char label[42];
        strncpy(label, target->value.str.text, 41);
        label[41] = '\0';
        advance(p);
        ASTNode* node = ast_alloc(AST_GOSUB, line);
        strncpy(node->data.jump.label, label, 41);
        node->data.jump.lineno = -1;
        program_add_stmt(p->prog, node);
    } else {
        fb_syntax_error(line, target->col,
                        "Expected line number or label after GOSUB");
    }
}

static void parse_return(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume RETURN
    program_add_stmt(p->prog, ast_alloc(AST_RETURN, line));
}
```

### 12.3 Execute GOSUB / RETURN

```c
static void exec_gosub(Interpreter* interp, ASTNode* node) {
    if (interp->gosub_sp >= GOSUB_STACK_MAX) {
        fb_error(FB_ERR_OUT_OF_MEMORY, node->line,
                 "GOSUB stack overflow");
        return;
    }

    // Push return address (next statement after this GOSUB)
    interp->gosub_stack[interp->gosub_sp].return_pc = interp->pc + 1;
    interp->gosub_stack[interp->gosub_sp].line = node->line;
    interp->gosub_sp++;

    // Jump to target (same logic as GOTO)
    int target_idx = resolve_jump_target(interp, node);
    if (target_idx < 0) return; // error already reported

    interp->pc = target_idx;
}

static void exec_return(Interpreter* interp, ASTNode* node) {
    if (interp->gosub_sp == 0) {
        fb_error(FB_ERR_RETURN_WITHOUT_GOSUB, node->line, NULL);
        return;
    }

    interp->gosub_sp--;
    interp->pc = interp->gosub_stack[interp->gosub_sp].return_pc;
}
```

---

## 13. DIM (Scalar Variables)

### 13.1 Parse DIM

Phase 1 handles only scalar `DIM`. Array `DIM` with subscripts is added in Phase 3.

**Syntax:** `DIM [SHARED] varname [AS type] [, varname [AS type]] ...`

```c
static void parse_dim(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume DIM

    int is_shared = 0;
    if (current_token(p)->kind == TOK_KW_SHARED) {
        is_shared = 1;
        advance(p);
    }

    do {
        Token* name_tok = current_token(p);
        if (!is_identifier(name_tok->kind)) {
            fb_syntax_error(line, name_tok->col, "Expected variable name");
        }

        char name[42];
        strncpy(name, name_tok->value.str.text, 41);
        name[41] = '\0';
        FBType type_hint = ident_type_from_token(name_tok);
        advance(p);

        // AS type clause
        FBType declared_type = type_hint;
        if (current_token(p)->kind == TOK_KW_AS) {
            advance(p); // consume AS
            declared_type = parse_type_name(p);
        } else if (!has_type_suffix_tok(name_tok)) {
            // No suffix and no AS → use DEFtype default
            declared_type = scope_default_type(
                /* appropriate scope */, name);
        }

        ASTNode* node = ast_dim(line, name, declared_type, is_shared);
        program_add_stmt(p->prog, node);

    } while (current_token(p)->kind == TOK_COMMA && (advance(p), 1));
}

// Parse a type keyword after AS: INTEGER, LONG, SINGLE, DOUBLE, STRING
static FBType parse_type_name(Parser* p) {
    Token* t = current_token(p);
    FBType type;
    switch (t->kind) {
        case TOK_KW_INTEGER: type = FB_INTEGER; break;
        case TOK_KW_LONG:    type = FB_LONG; break;
        case TOK_KW_SINGLE:  type = FB_SINGLE; break;
        case TOK_KW_DOUBLE:  type = FB_DOUBLE; break;
        case TOK_KW_STRING:  type = FB_STRING; break;
        default:
            fb_syntax_error(t->line, t->col,
                            "Expected type name after AS");
            return FB_SINGLE; // unreachable
    }
    advance(p);
    return type;
}
```

### 13.2 Execute DIM

```c
static void exec_dim(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.dim.name;
    FBType type = node->data.dim.type;
    int is_shared = node->data.dim.is_shared;

    Scope* target_scope = is_shared
                          ? interp->global_scope
                          : interp->current_scope;

    // Check for duplicate
    if (scope_lookup_local(target_scope, name)) {
        fb_error(FB_ERR_DUPLICATE_DEFINITION, node->line, name);
        return;
    }

    Symbol* sym = scope_insert(target_scope, name, SYM_VARIABLE, type);
    sym->is_shared = is_shared;

    // Initialize with default value
    switch (type) {
        case FB_INTEGER: sym->value = fbval_int(0); break;
        case FB_LONG:    sym->value = fbval_long(0); break;
        case FB_SINGLE:  sym->value = fbval_single(0.0f); break;
        case FB_DOUBLE:  sym->value = fbval_double(0.0); break;
        case FB_STRING:  sym->value = fbval_string_from_cstr(""); break;
    }
}
```

---

## 14. CONST

### 14.1 Parse CONST

**Syntax:** `CONST name = expr [, name = expr] ...`

```c
static void parse_const(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume CONST

    do {
        Token* name_tok = current_token(p);
        if (!is_identifier(name_tok->kind)) {
            fb_syntax_error(line, name_tok->col,
                            "Expected constant name");
        }

        char name[42];
        strncpy(name, name_tok->value.str.text, 41);
        name[41] = '\0';
        advance(p);

        expect(p, TOK_EQ);

        // CONST expressions must be evaluable at parse time.
        // For Phase 1: allow only literal values and simple constant
        // expressions (numeric literals, string literals, and arithmetic
        // on previously defined constants).
        ASTNode* value_expr = parse_expr(p, 1);

        ASTNode* node = ast_const_decl(line, name, value_expr);
        program_add_stmt(p->prog, node);

    } while (current_token(p)->kind == TOK_COMMA && (advance(p), 1));
}
```

### 14.2 Execute CONST

```c
static void exec_const(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.const_decl.name;

    // Evaluate the constant expression
    FBValue val = eval_expr(interp, node->data.const_decl.value_expr);

    // Check for duplicate
    if (scope_lookup_local(interp->current_scope, name)) {
        fb_error(FB_ERR_DUPLICATE_DEFINITION, node->line, name);
        fbval_release(&val);
        return;
    }

    // Insert as SYM_CONST — assignments to this symbol are errors
    Symbol* sym = scope_insert(interp->current_scope, name,
                               SYM_CONST, val.type);
    sym->value = val; // Transfer ownership
}
```

---

## 15. DEFtype Statements

### 15.1 Parse DEFtype

**Syntax:** `DEFINT A-Z` or `DEFSNG I-N` etc.

```c
static void parse_deftype(Parser* p) {
    int line = current_token(p)->line;
    TokenKind kw = current_token(p)->kind;
    advance(p); // consume DEFINT/DEFLNG/DEFSNG/DEFDBL/DEFSTR

    FBType type;
    switch (kw) {
        case TOK_KW_DEFINT: type = FB_INTEGER; break;
        case TOK_KW_DEFLNG: type = FB_LONG; break;
        case TOK_KW_DEFSNG: type = FB_SINGLE; break;
        case TOK_KW_DEFDBL: type = FB_DOUBLE; break;
        case TOK_KW_DEFSTR: type = FB_STRING; break;
        default: return; // unreachable
    }

    do {
        Token* start_tok = current_token(p);
        if (!is_identifier(start_tok->kind)) {
            fb_syntax_error(line, start_tok->col,
                            "Expected letter after DEFtype");
        }
        char range_start = toupper(start_tok->value.str.text[0]);
        advance(p);

        char range_end = range_start;
        if (current_token(p)->kind == TOK_MINUS) {
            advance(p); // consume '-'
            Token* end_tok = current_token(p);
            range_end = toupper(end_tok->value.str.text[0]);
            advance(p);
        }

        ASTNode* node = ast_deftype(line, type, range_start, range_end);
        program_add_stmt(p->prog, node);

    } while (current_token(p)->kind == TOK_COMMA && (advance(p), 1));
}
```

### 15.2 Execute DEFtype

```c
static void exec_deftype(Interpreter* interp, ASTNode* node) {
    char start = node->data.deftype.range_start;
    char end   = node->data.deftype.range_end;
    FBType type = node->data.deftype.type;

    // DEFtype always applies to the module-level scope
    for (char c = start; c <= end; c++) {
        interp->global_scope->deftype[c - 'A'] = type;
    }
}
```

---

## 16. Interpreter Core (`include/interpreter.h`, `src/interpreter.c`)

### 16.1 Interpreter State

```c
#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "ast.h"
#include "symtable.h"
#include "value.h"

#define GOSUB_STACK_MAX 256

typedef struct {
    int return_pc;
    int line;
} GosubFrame;

typedef struct Interpreter {
    Program*     prog;           // The parsed program
    Scope*       global_scope;   // Module-level scope
    Scope*       current_scope;  // Currently active scope

    int          pc;             // Program counter (current statement index)
    int          running;        // 1 = running, 0 = terminated

    int          print_col;      // Current print column (1-based) for zones

    // GOSUB stack
    GosubFrame   gosub_stack[GOSUB_STACK_MAX];
    int          gosub_sp;

    // FOR loop stack (for flat-model GOTO-based programs)
    // Phase 1 doesn't strictly need this if FOR is fully nested in AST,
    // but it's needed for GOTO-into-FOR and multi-variable NEXT.
    // For Phase 1, keep it simple: FOR loops are AST-nested.
} Interpreter;

// Initialize interpreter with a parsed program.
void interp_init(Interpreter* interp, Program* prog);

// Run the program from the beginning.
void interp_run(Interpreter* interp);

// Free interpreter resources.
void interp_free(Interpreter* interp);

#endif
```

### 16.2 Main Execution Loop

```c
void interp_run(Interpreter* interp) {
    interp->pc = 0;
    interp->running = 1;
    interp->gosub_sp = 0;
    interp->print_col = 1;

    while (interp->running && interp->pc < interp->prog->stmt_count) {
        ASTNode* stmt = interp->prog->statements[interp->pc];
        int old_pc = interp->pc;

        exec_statement(interp, stmt);

        // If pc was not changed by a jump (GOTO/GOSUB/RETURN),
        // advance to next statement.
        if (interp->pc == old_pc) {
            interp->pc++;
        }
    }
}
```

### 16.3 Statement Dispatcher

```c
static void exec_statement(Interpreter* interp, ASTNode* stmt) {
    switch (stmt->kind) {
        case AST_PRINT:      exec_print(interp, stmt); break;
        case AST_LET:        exec_let(interp, stmt); break;
        case AST_IF:         exec_if(interp, stmt); break;
        case AST_IF_SINGLE:  exec_if(interp, stmt); break;
        case AST_FOR:        exec_for(interp, stmt); break;
        case AST_WHILE:      exec_while(interp, stmt); break;
        case AST_DO_LOOP:    exec_do_loop(interp, stmt); break;
        case AST_GOTO:       exec_goto(interp, stmt); break;
        case AST_GOSUB:      exec_gosub(interp, stmt); break;
        case AST_RETURN:     exec_return(interp, stmt); break;
        case AST_DIM:        exec_dim(interp, stmt); break;
        case AST_CONST_DECL: exec_const(interp, stmt); break;
        case AST_DEFTYPE:    exec_deftype(interp, stmt); break;
        case AST_END:
        case AST_STOP:       exec_end(interp, stmt); break;
        case AST_REM:        /* no-op */ break;
        case AST_LABEL_DEF:  /* no-op at runtime */ break;
        default:
            fb_error(FB_ERR_SYNTAX, stmt->line,
                     "Unimplemented statement");
            break;
    }
}
```

### 16.4 Expression Evaluator

```c
static FBValue eval_expr(Interpreter* interp, ASTNode* expr) {
    switch (expr->kind) {
        case AST_LITERAL:
            return fbval_copy(&expr->data.literal.value);

        case AST_VARIABLE: {
            Symbol* sym = scope_lookup(interp->current_scope,
                                        expr->data.variable.name);
            if (!sym) {
                // Auto-create with default value (FB behavior — no error)
                FBType type = expr->data.variable.type_hint;
                if (type == FB_SINGLE && !has_type_suffix(
                        expr->data.variable.name)) {
                    type = scope_default_type(interp->current_scope,
                                              expr->data.variable.name);
                }
                sym = scope_insert(interp->current_scope,
                                   expr->data.variable.name,
                                   SYM_VARIABLE, type);
                // Default value is already zero-initialized by scope_insert
                switch (type) {
                    case FB_INTEGER: sym->value = fbval_int(0); break;
                    case FB_LONG:    sym->value = fbval_long(0); break;
                    case FB_SINGLE:  sym->value = fbval_single(0.0f); break;
                    case FB_DOUBLE:  sym->value = fbval_double(0.0); break;
                    case FB_STRING:  sym->value = fbval_string_from_cstr(""); break;
                }
            }
            return fbval_copy(&sym->value);
        }

        case AST_BINARY_OP: {
            FBValue left = eval_expr(interp, expr->data.binop.left);
            FBValue right = eval_expr(interp, expr->data.binop.right);

            FBValue result;
            TokenKind op = expr->data.binop.op;

            // Dispatch to relational, logical, or arithmetic
            if (op == TOK_EQ || op == TOK_NE || op == TOK_LT ||
                op == TOK_GT || op == TOK_LE || op == TOK_GE) {
                result = fbval_compare(&left, &right, op);
            } else if (op == TOK_KW_AND || op == TOK_KW_OR ||
                       op == TOK_KW_XOR || op == TOK_KW_EQV ||
                       op == TOK_KW_IMP) {
                result = fbval_logical_op(&left, &right, op);
            } else {
                result = fbval_binary_op(&left, &right, op);
            }

            fbval_release(&left);
            fbval_release(&right);
            return result;
        }

        case AST_UNARY_OP: {
            FBValue operand = eval_expr(interp,
                                        expr->data.unop.operand);
            FBValue result = fbval_unary_op(&operand, expr->data.unop.op);
            fbval_release(&operand);
            return result;
        }

        case AST_PAREN:
            // Parenthesized expression — evaluate inner
            return eval_expr(interp, expr->data.binop.left);
            // (AST_PAREN stores child in a convenient field)

        case AST_FUNC_CALL:
            return eval_builtin_func(interp, expr);

        default:
            fb_error(FB_ERR_SYNTAX, expr->line,
                     "Invalid expression node");
            return fbval_int(0);
    }
}
```

### 16.5 Built-in Function Evaluator (Phase 1 subset)

```c
static FBValue eval_builtin_func(Interpreter* interp, ASTNode* expr) {
    const char* name = expr->data.func_call.name;
    int argc = expr->data.func_call.arg_count;
    ASTNode** args = expr->data.func_call.args;

    // Evaluate arguments up-front
    FBValue argv[16];
    for (int i = 0; i < argc; i++) {
        argv[i] = eval_expr(interp, args[i]);
    }

    FBValue result;

    // ---- Numeric functions ----
    if (_stricmp(name, "ABS") == 0) {
        double v = fbval_to_double(&argv[0]);
        result = fbval_double(fabs(v));
        // Coerce result back to operand type
        result = fbval_coerce(&result, argv[0].type);

    } else if (_stricmp(name, "INT") == 0) {
        double v = fbval_to_double(&argv[0]);
        result = fbval_double(floor(v));

    } else if (_stricmp(name, "FIX") == 0) {
        double v = fbval_to_double(&argv[0]);
        result = fbval_double(v >= 0 ? floor(v) : ceil(v));

    } else if (_stricmp(name, "SGN") == 0) {
        double v = fbval_to_double(&argv[0]);
        int16_t s = (v > 0) ? 1 : (v < 0) ? -1 : 0;
        result = fbval_int(s);

    } else if (_stricmp(name, "SQR") == 0) {
        double v = fbval_to_double(&argv[0]);
        if (v < 0) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL,
                     expr->line, "SQR of negative");
            result = fbval_double(0);
        } else {
            result = fbval_double(sqrt(v));
        }

    } else if (_stricmp(name, "CINT") == 0) {
        result = fbval_coerce(&argv[0], FB_INTEGER);

    } else if (_stricmp(name, "CLNG") == 0) {
        result = fbval_coerce(&argv[0], FB_LONG);

    } else if (_stricmp(name, "CSNG") == 0) {
        result = fbval_coerce(&argv[0], FB_SINGLE);

    } else if (_stricmp(name, "CDBL") == 0) {
        result = fbval_coerce(&argv[0], FB_DOUBLE);

    // ---- String/Conversion functions ----
    } else if (_stricmp(name, "CHR$") == 0) {
        int32_t code = fbval_to_long(&argv[0]);
        if (code < 0 || code > 255) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL,
                     expr->line, "CHR$ out of range");
            result = fbval_string_from_cstr("");
        } else {
            char buf[2] = { (char)code, '\0' };
            result = fbval_string_from_cstr(buf);
        }

    } else if (_stricmp(name, "ASC") == 0) {
        if (argv[0].type != FB_STRING || argv[0].as.str->len == 0) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL,
                     expr->line, "ASC of empty string");
            result = fbval_int(0);
        } else {
            result = fbval_int(
                (int16_t)(unsigned char)argv[0].as.str->data[0]);
        }

    } else if (_stricmp(name, "STR$") == 0) {
        char* formatted = fbval_format_print(&argv[0]);
        // STR$ returns the formatted string (with leading space for positive)
        // but WITHOUT the trailing space that PRINT adds.
        int len = (int)strlen(formatted);
        if (len > 0 && formatted[len - 1] == ' ') {
            formatted[len - 1] = '\0';
        }
        result = fbval_string_from_cstr(formatted);
        free(formatted);

    } else if (_stricmp(name, "VAL") == 0) {
        if (argv[0].type != FB_STRING) {
            fb_error(FB_ERR_TYPE_MISMATCH, expr->line, NULL);
            result = fbval_double(0);
        } else {
            double v = atof(argv[0].as.str->data);
            result = fbval_double(v);
        }

    } else if (_stricmp(name, "LEN") == 0) {
        if (argv[0].type != FB_STRING) {
            fb_error(FB_ERR_TYPE_MISMATCH, expr->line, NULL);
            result = fbval_int(0);
        } else {
            result = fbval_long(argv[0].as.str->len);
        }

    } else {
        fb_error(FB_ERR_SYNTAX, expr->line,
                 "Unknown function");
        result = fbval_int(0);
    }

    // Release arguments
    for (int i = 0; i < argc; i++) {
        fbval_release(&argv[i]);
    }

    return result;
}
```

---

## 17. Updated Main Entry Point (`src/main.c`)

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

    // 3. Parse
    Program prog = {0};
    if (parser_parse(lex.tokens, lex.token_count, &prog) != 0) {
        fprintf(stderr, "Parser failed.\n");
        lexer_free(&lex);
        free(source);
        return 1;
    }

    // 4. Interpret
    Interpreter interp;
    interp_init(&interp, &prog);
    interp_run(&interp);

    // 5. Clean up
    interp_free(&interp);
    program_free(&prog);
    lexer_free(&lex);
    free(source);

    return 0;
}
```

---

## 18. Verification Test Files

### 18.1 `tests/verify/phase1_print.bas` — PRINT Formatting

```basic
REM Phase 1 Test: PRINT formatting
PRINT "Hello, World!"
PRINT 42
PRINT -7
PRINT 3.14
PRINT
PRINT 1; 2; 3
PRINT 1, 2, 3
PRINT "A"; "B"; "C"
PRINT "No newline ";
PRINT "after this"
PRINT "Value:"; 42
```

**Expected output (`tests/verify/phase1_expected/print.txt`):**
```
Hello, World!
 42
-7
 3.14

 1  2  3
 1             2             3
ABC
No newline after this
Value: 42
```

### 18.2 `tests/verify/phase1_expr.bas` — Expression Evaluation

```basic
REM Phase 1 Test: Expressions and operators
PRINT 2 + 3 * 4
PRINT (2 + 3) * 4
PRINT 10 \ 3
PRINT 10 MOD 3
PRINT 2 ^ 8
PRINT -5 + 3
PRINT NOT 0
PRINT 255 AND 15
PRINT 240 OR 15
PRINT 255 XOR 15
PRINT 5 > 3
PRINT 3 > 5
PRINT "ABC" < "DEF"
PRINT "ABC" = "ABC"
PRINT ABS(-42)
PRINT INT(3.7)
PRINT INT(-3.7)
PRINT FIX(-3.7)
PRINT SGN(-5)
PRINT SGN(0)
PRINT SGN(5)
PRINT SQR(144)
PRINT LEN("Hello")
PRINT ASC("A")
PRINT CHR$(65)
PRINT VAL("3.14")
PRINT STR$(42)
```

**Expected output (`tests/verify/phase1_expected/expr.txt`):**
```
 14
 20
 3
 1
 256
-2
-1
 15
 255
 240
-1
 0
-1
-1
 42
 3
-4
-3
-1
 0
 1
 12
 5
 65
A
 3.14
 42
```

### 18.3 `tests/verify/phase1_if.bas` — IF Blocks

```basic
REM Phase 1 Test: IF statements
DIM x AS INTEGER
x = 10

' Single-line IF
IF x = 10 THEN PRINT "x is 10"
IF x = 5 THEN PRINT "FAIL" ELSE PRINT "x is not 5"

' Block IF
IF x > 5 THEN
    PRINT "x > 5"
ELSEIF x > 0 THEN
    PRINT "x > 0"
ELSE
    PRINT "x <= 0"
END IF

' Nested IF
IF x > 0 THEN
    IF x > 5 THEN
        PRINT "x > 5 (nested)"
    END IF
END IF

' Boolean expressions
IF x > 5 AND x < 20 THEN
    PRINT "5 < x < 20"
END IF
```

**Expected output (`tests/verify/phase1_expected/if.txt`):**
```
x is 10
x is not 5
x > 5
x > 5 (nested)
5 < x < 20
```

### 18.4 `tests/verify/phase1_for.bas` — FOR...NEXT

```basic
REM Phase 1 Test: FOR...NEXT loops
' Basic counting
FOR i% = 1 TO 5
    PRINT i%;
NEXT i%
PRINT

' STEP
FOR i% = 10 TO 1 STEP -2
    PRINT i%;
NEXT
PRINT

' Nested FOR
FOR i% = 1 TO 3
    FOR j% = 1 TO 3
        PRINT i% * 10 + j%;
    NEXT j%
NEXT i%
PRINT

' FOR that doesn't execute (start > end with positive step)
FOR i% = 10 TO 1
    PRINT "FAIL"
NEXT
PRINT "Skipped correctly"

' Floating-point FOR
FOR x! = 0 TO 1 STEP 0.5
    PRINT x!;
NEXT
PRINT
```

**Expected output (`tests/verify/phase1_expected/for.txt`):**
```
 1  2  3  4  5
 10  8  6  4  2
 11  12  13  21  22  23  31  32  33
Skipped correctly
 0  .5  1
```

### 18.5 `tests/verify/phase1_loops.bas` — WHILE/WEND + DO/LOOP

```basic
REM Phase 1 Test: Loop constructs
' WHILE...WEND
DIM n AS INTEGER
n = 1
WHILE n <= 5
    PRINT n;
    n = n + 1
WEND
PRINT

' DO WHILE...LOOP
n = 1
DO WHILE n <= 5
    PRINT n;
    n = n + 1
LOOP
PRINT

' DO UNTIL...LOOP (pre-test)
n = 1
DO UNTIL n > 5
    PRINT n;
    n = n + 1
LOOP
PRINT

' DO...LOOP WHILE (post-test — runs at least once)
n = 10
DO
    PRINT n;
    n = n + 1
LOOP WHILE n < 10
PRINT

' DO...LOOP UNTIL (post-test)
n = 1
DO
    PRINT n;
    n = n + 1
LOOP UNTIL n > 3
PRINT
```

**Expected output (`tests/verify/phase1_expected/loops.txt`):**
```
 1  2  3  4  5
 1  2  3  4  5
 1  2  3  4  5
 10
 1  2  3
```

### 18.6 `tests/verify/phase1_goto.bas` — GOTO + GOSUB/RETURN

```basic
REM Phase 1 Test: GOTO and GOSUB
PRINT "Start"
GOTO skip
PRINT "FAIL: should be skipped"
skip:
PRINT "After GOTO"

GOSUB mySub
PRINT "After GOSUB"
GOTO done

mySub:
    PRINT "In subroutine"
    RETURN

done:
PRINT "End"
```

**Expected output (`tests/verify/phase1_expected/goto.txt`):**
```
Start
After GOTO
In subroutine
After GOSUB
End
```

### 18.7 `tests/verify/phase1_fizzbuzz.bas` — Milestone: FizzBuzz

```basic
REM FizzBuzz - Phase 1 Milestone
DEFINT A-Z
FOR i = 1 TO 30
    IF i MOD 15 = 0 THEN
        PRINT "FizzBuzz"
    ELSEIF i MOD 3 = 0 THEN
        PRINT "Fizz"
    ELSEIF i MOD 5 = 0 THEN
        PRINT "Buzz"
    ELSE
        PRINT i
    END IF
NEXT i
```

**Expected output (`tests/verify/phase1_expected/fizzbuzz.txt`):**
```
 1
 2
Fizz
 4
Buzz
Fizz
 7
 8
Fizz
Buzz
 11
Fizz
 13
 14
FizzBuzz
 16
 17
Fizz
 19
Buzz
Fizz
 22
 23
Fizz
Buzz
 26
Fizz
 28
 29
FizzBuzz
```

### 18.8 `tests/verify/phase1_fibonacci.bas` — Milestone: Fibonacci

```basic
REM Fibonacci Sequence - Phase 1 Milestone
DEFINT A-Z
DIM a AS LONG, b AS LONG, c AS LONG
a = 0
b = 1
PRINT "Fibonacci sequence (first 20 terms):"
FOR i = 1 TO 20
    PRINT a;
    c = a + b
    a = b
    b = c
NEXT i
PRINT
```

**Expected output (`tests/verify/phase1_expected/fibonacci.txt`):**
```
Fibonacci sequence (first 20 terms):
 0  1  1  2  3  5  8  13  21  34  55  89  144  233  377  610  987  1597  2584  4181
```

### 18.9 `tests/verify/phase1_primes.bas` — Milestone: Prime Sieve

```basic
REM Prime Sieve (Sieve of Eratosthenes) - Phase 1 Milestone
DEFINT A-Z
CONST LIMIT = 50

' Use variables as a simple "array" simulation with IF chains
' (True arrays come in Phase 3 — for now, use a loop approach)
PRINT "Primes up to"; LIMIT; ":"
FOR n = 2 TO LIMIT
    isPrime% = 1
    FOR d = 2 TO n - 1
        IF n MOD d = 0 THEN
            isPrime% = 0
        END IF
    NEXT d
    IF isPrime% THEN
        PRINT n;
    END IF
NEXT n
PRINT
```

**Expected output (`tests/verify/phase1_expected/primes.txt`):**
```
Primes up to 50 :
 2  3  5  7  11  13  17  19  23  29  31  37  41  43  47
```

---

## 19. Updated Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -g -fsanitize=address
LDFLAGS = -fsanitize=address -lm

SRC = src/main.c src/lexer.c src/value.c src/symtable.c src/ast.c \
      src/parser.c src/interpreter.c src/coerce.c src/error.c
OBJ = $(SRC:.c=.o)

fbasic: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Phase 0 tests (still valid)
test-phase0: fbasic
	./fbasic tests/verify/phase0_lex1.bas
	./fbasic tests/verify/phase0_lex2.bas
	./fbasic tests/verify/phase0_lex3.bas

# Phase 1 tests — run programs and compare output
test-phase1: fbasic
	./fbasic tests/verify/phase1_print.bas > /tmp/p1_print.txt && \
		diff /tmp/p1_print.txt tests/verify/phase1_expected/print.txt
	./fbasic tests/verify/phase1_expr.bas > /tmp/p1_expr.txt && \
		diff /tmp/p1_expr.txt tests/verify/phase1_expected/expr.txt
	./fbasic tests/verify/phase1_if.bas > /tmp/p1_if.txt && \
		diff /tmp/p1_if.txt tests/verify/phase1_expected/if.txt
	./fbasic tests/verify/phase1_for.bas > /tmp/p1_for.txt && \
		diff /tmp/p1_for.txt tests/verify/phase1_expected/for.txt
	./fbasic tests/verify/phase1_loops.bas > /tmp/p1_loops.txt && \
		diff /tmp/p1_loops.txt tests/verify/phase1_expected/loops.txt
	./fbasic tests/verify/phase1_goto.bas > /tmp/p1_goto.txt && \
		diff /tmp/p1_goto.txt tests/verify/phase1_expected/goto.txt
	./fbasic tests/verify/phase1_fizzbuzz.bas > /tmp/p1_fizz.txt && \
		diff /tmp/p1_fizz.txt tests/verify/phase1_expected/fizzbuzz.txt
	./fbasic tests/verify/phase1_fibonacci.bas > /tmp/p1_fib.txt && \
		diff /tmp/p1_fib.txt tests/verify/phase1_expected/fibonacci.txt
	./fbasic tests/verify/phase1_primes.bas > /tmp/p1_primes.txt && \
		diff /tmp/p1_primes.txt tests/verify/phase1_expected/primes.txt

test: test-phase0 test-phase1
	@echo "All tests passed."

clean:
	rm -f $(OBJ) fbasic

.PHONY: test test-phase0 test-phase1 clean
```

---

## 20. AST Additions for Phase 1 (`include/ast.h`)

Add the following constructors and AST node support not yet present from Phase 0:

```c
// Additional AST constructors needed for Phase 1

// Create a parenthesized expression wrapper.
ASTNode* ast_paren(int line, ASTNode* inner);

// Create a single-line IF node.
ASTNode* ast_if_single(int line, ASTNode* condition,
                       ASTNode** then_body, int then_count,
                       ASTNode** else_body, int else_count);

// Create a block IF node with ELSEIF chains.
ASTNode* ast_if_block(int line, ASTNode* condition,
                      ASTNode** then_body, int then_count,
                      ASTNode** elseif_conds, ASTNode*** elseif_bodies,
                      int* elseif_counts, int elseif_n,
                      ASTNode** else_body, int else_count);

// Create a loop node (WHILE/WEND or DO/LOOP).
ASTNode* ast_loop(int line, ASTNode* condition,
                  ASTNode** body, int body_count,
                  int is_until, int is_post);

// Create DIM node.
ASTNode* ast_dim(int line, const char* name, FBType type, int is_shared);

// Create CONST declaration node.
ASTNode* ast_const_decl(int line, const char* name, ASTNode* value_expr);

// Create DEFtype node.
ASTNode* ast_deftype(int line, FBType type, char range_start, char range_end);

// Create END/STOP node.
ASTNode* ast_end(int line, ASTKind kind);  // AST_END or AST_STOP

// Create a literal from a Token (convenience).
ASTNode* ast_literal_from_token(const Token* tok);

// Create function call node.
ASTNode* ast_func_call(int line, const char* name,
                       ASTNode** args, int arg_count);
```

---

## 21. Parser Helper Functions

```c
// Advance to next token. Returns consumed token.
static Token* advance(Parser* p);

// Return current token without consuming.
static Token* current_token(Parser* p);

// Return token at offset from current position.
static Token* peek_token(Parser* p, int offset);

// Consume current token if it matches expected kind. Error otherwise.
static void expect(Parser* p, TokenKind expected);

// Consume EOL token (with optional skip of blank lines).
static void expect_eol(Parser* p);

// Check if a token kind is an identifier type.
static int is_identifier(TokenKind kind);

// Get FBType from identifier token kind.
static FBType ident_type_from_token(const Token* tok);

// Check if an identifier has an explicit type suffix.
static int has_type_suffix_tok(const Token* tok);

// Check if a name string has a type suffix character.
static int has_type_suffix(const char* name);

// Check if a name is a known built-in function.
static int is_builtin_function(const char* name);

// Parse a block of statements until one of the terminator tokens is found.
// Does NOT consume the terminator.
static void parse_block(Parser* p, ASTNode*** out_stmts, int* out_count,
                        TokenKind term1, TokenKind term2, TokenKind term3);

// Parse statements until NEXT keyword is found (for FOR body).
static void parse_block_until_next(Parser* p,
                                   ASTNode*** out_stmts, int* out_count);

// Parse a single inline statement (for single-line IF bodies).
static ASTNode* parse_inline_statement(Parser* p);

// Add a statement to the program.
static void program_add_stmt(Program* prog, ASTNode* stmt);

// Add a line number mapping.
static void program_add_lineno(Program* prog, int32_t lineno, int stmt_idx);

// Add a label mapping.
static void program_add_label(Program* prog, const char* name, int stmt_idx);

// Copy an array of ASTNode pointers to the heap.
static ASTNode** copy_node_array(ASTNode** src, int count);
```

---

## 22. Phase 1 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **Expression Parser** | Full precedence-climbing parser handles all FB operators in correct precedence order. Parentheses, unary operators, function calls all parse correctly. `2 + 3 * 4 = 14`, `(2 + 3) * 4 = 20`, `2 ^ 3 ^ 2 = 512` (right-associative), `NOT 0 = -1`, `255 AND 15 = 15`. String concatenation and comparison work. |
| 2 | **Assignment** | `LET x = 5` and `x = 5` (implicit LET) both work. Type coercion on assignment: `x% = 3.7` assigns 4 (banker's rounding). Auto-creation of undeclared variables with DEFtype-resolved types. CONST cannot be reassigned. |
| 3 | **PRINT** | Correct number formatting (leading space, trailing space, `-` for negatives). Semicolon = compact, comma = 14-char zones. Trailing `;` suppresses newline. Empty `PRINT` outputs blank line. Mixed string/number output correct. |
| 4 | **REM / '** | Both `REM` and `'` comments are silently skipped. No output, no errors. Inline `'` after statements works (`x = 5 ' set x`). |
| 5 | **END / STOP / SYSTEM** | `END` terminates cleanly. `STOP` prints "Break in line NNN" and terminates. `SYSTEM` behaves like END. |
| 6 | **GOTO** | Jump to labels and line numbers. Forward and backward jumps. Undefined target → runtime error 8 (Undefined label). |
| 7 | **IF...THEN...ELSE** | Single-line `IF x THEN PRINT "yes" ELSE PRINT "no"` works. `IF x THEN 100` (line number after THEN) works. Block IF / ELSEIF / ELSE / END IF with correct nesting. Nested IF blocks work. Boolean AND/OR in conditions work. |
| 8 | **FOR...NEXT** | Basic counting, STEP (positive and negative), nested FOR loops, floating-point counters. Loop body skipped when initial condition fails (e.g., `FOR i = 10 TO 1`). `NEXT` with and without variable name. |
| 9 | **WHILE...WEND** | Pre-test loop, body skipped if condition false initially. |
| 10 | **DO...LOOP** | All 4 variants (DO WHILE/UNTIL pre-test, LOOP WHILE/UNTIL post-test). Post-test executes body at least once. Infinite DO...LOOP (no condition). |
| 11 | **GOSUB...RETURN** | Subroutine call and return via runtime stack. Nested GOSUB works. RETURN without GOSUB → error 3. Stack overflow (256 deep) → error 7. |
| 12 | **DIM** | Scalar variables with `AS type`. `DIM SHARED` flag stored. Duplicate DIM → error 10. Variables initialized to default (0 or empty string). |
| 13 | **CONST** | Constant defined and usable in expressions. Reassignment → error. Multiple CONST on one line with commas. |
| 14 | **DEFtype** | `DEFINT A-Z` et al. change default type for variables starting with those letters. Verified: variable without suffix or DIM uses DEFtype-resolved type. |
| 15 | **Built-in Functions** | ABS, INT, FIX, SGN, SQR, CINT, CLNG, CSNG, CDBL, CHR$, ASC, STR$, VAL, LEN all return correct results. Invalid arguments (SQR(-1), ASC("")) raise appropriate errors. |
| 16 | **Milestone Programs** | FizzBuzz, Fibonacci, and prime sieve produce correct output matching expected files. |
| 17 | **GOTO/GOSUB Interaction** | GOTO into/out of loops works (FB allows it). GOSUB from inside a loop body works and RETURN resumes correctly. |
| 18 | **No Leaks** | All test programs pass with `-fsanitize=address` and zero leak/error reports. AST nodes freed after execution. String values properly ref-counted throughout. |

---

## 23. Key Implementation Warnings

1. **GOTO vs nested AST:** If using a nested-AST approach (FOR body is an array of child nodes), then GOTO out of a FOR loop is problematic — it must unwind the FOR. Consider a hybrid: store top-level statements flat, and structured blocks (IF/FOR/WHILE/DO) as nested AST nodes. GOTO targets can only be top-level statements. GOTO into a structured block's body is a FB runtime error in some cases — handle gracefully.

2. **FOR with GOTO:** FB allows `GOTO` to jump out of a FOR loop without executing NEXT. The FOR's loop variable retains its last value. If using a FOR-stack runtime model, the FOR entry must be popped when GOTO exits the loop. If using nested AST (exec_for handles the loop), GOTO out requires `longjmp` or a flag check.

3. **PRINT trailing spaces:** A number formatted by `fbval_format_print` includes a trailing space. When semicolon-separated, this produces the correct 1-space gap between numbers. Do NOT add extra spaces between semicolon-separated items.

4. **Single-line IF with colon:** `IF x THEN a = 1: b = 2 ELSE c = 3: d = 4` — the colons are part of the THEN/ELSE clause, NOT statement separators at the top level. The parser must treat everything between THEN and ELSE (or EOL) as the THEN-clause, including colon-separated statements.

5. **DEFtype executes at parse time conceptually:** In FB, DEFtype statements take effect for all variables in the module, regardless of position. However, for Phase 1 simplicity, execute DEFtype as a runtime statement that sets scope defaults before subsequent variable references. Place DEFtype statements at the top of programs (as FB convention dictates).

6. **`=` ambiguity resolved by parser context:** In statement position, `=` is assignment. Inside an expression (including IF conditions, FOR bounds, function arguments), `=` is comparison. The precedence-climbing parser treats `=` as a binary comparison operator at precedence level 7. Assignment is handled by `parse_assignment()` at statement level, BEFORE entering the expression parser.

7. **FB variable auto-creation:** Referencing an undeclared variable in an expression does NOT error — it auto-creates with default value (0 or ""). This is fundamental FB behavior. The `eval_expr` handler for `AST_VARIABLE` must create the variable if not found, using DEFtype rules.

8. **Integer overflow in FOR loops:** When `i%` (INTEGER, int16) reaches 32767 and `STEP 1`, the next increment wraps to -32768. FB does this silently. C's int16_t overflow is undefined in C, so explicitly cast: `(int16_t)((uint16_t)val + (uint16_t)step)`.

9. **Multi-variable NEXT:** `NEXT j, i` is equivalent to `NEXT j` followed by `NEXT i`. In a nested AST, the parser should close the inner FOR (j) and the outer FOR (i) with a single NEXT token sequence. This is tricky if FORs are fully nested — the parser must match variables to their enclosing FORs.

10. **`THEN` followed by line number:** `IF x = 5 THEN 100` means `IF x = 5 THEN GOTO 100`. The parser must check if the token after THEN is a numeric literal and generate an implicit GOTO node.
