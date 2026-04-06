/*
 * parser.c — Recursive descent parser for FBasic
 */
#include "parser.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Parser state */
typedef struct {
    const Token* tokens;
    int          token_count;
    int          pos;
    Program*     prog;
    int          last_fixed_str_len; /* set by parse_as_type for STRING * n */
} Parser;

/* Forward declarations */
static ASTNode* parse_expression(Parser* p);
static ASTNode* parse_statement(Parser* p);
static ASTNode* ast_alloc_helper(ASTKind kind, int line);
static ASTNode* calloc_node(ASTKind kind, int line);

/* --- Token navigation --- */

static const Token* peek(Parser* p) {
    if (p->pos >= p->token_count) {
        static Token eof_tok = { TOK_EOF, 0, 0 };
        return &eof_tok;
    }
    return &p->tokens[p->pos];
}

static const Token* peek_ahead(Parser* p, int offset) {
    int idx = p->pos + offset;
    if (idx >= p->token_count) {
        static Token eof_tok = { TOK_EOF, 0, 0 };
        return &eof_tok;
    }
    return &p->tokens[idx];
}

static const Token* advance(Parser* p) {
    const Token* t = peek(p);
    if (t->kind != TOK_EOF) p->pos++;
    return t;
}

static int at(Parser* p, TokenKind k) {
    return peek(p)->kind == k;
}

static int match(Parser* p, TokenKind k) {
    if (peek(p)->kind == k) { advance(p); return 1; }
    return 0;
}

static const Token* expect(Parser* p, TokenKind k) {
    const Token* t = peek(p);
    if (t->kind != k) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Expected %s, got %s",
                 token_kind_name(k), token_kind_name(t->kind));
        fb_syntax_error(t->line, t->col, buf);
    }
    return advance(p);
}

static void skip_eol(Parser* p) {
    while (at(p, TOK_EOL)) advance(p);
}

static int at_end_of_statement(Parser* p) {
    TokenKind k = peek(p)->kind;
    return k == TOK_EOL || k == TOK_EOF || k == TOK_COLON;
}

static void expect_end_of_statement(Parser* p) {
    if (!at_end_of_statement(p) && peek(p)->kind != TOK_KW_ELSE) {
        fb_syntax_error(peek(p)->line, peek(p)->col, "Expected end of statement");
    }
    if (at(p, TOK_COLON)) advance(p);
}

/* Determine FBType from identifier token kind */
static FBType type_from_ident_token(TokenKind k) {
    switch (k) {
        case TOK_IDENT_INT:    return FB_INTEGER;
        case TOK_IDENT_LONG:   return FB_LONG;
        case TOK_IDENT_SINGLE: return FB_SINGLE;
        case TOK_IDENT_DOUBLE: return FB_DOUBLE;
        case TOK_IDENT_STR:    return FB_STRING;
        default:               return (FBType)-1; /* Unspecified, use deftype */
    }
}

/* Parse AS type clause, returns the type */
static FBType parse_as_type(Parser* p, char* udt_name) {
    if (udt_name) udt_name[0] = '\0';
    if (!match(p, TOK_KW_AS)) return (FBType)-1;

    const Token* t = peek(p);
    if (t->kind == TOK_KW_INTEGER)      { advance(p); return FB_INTEGER; }
    else if (t->kind == TOK_KW_LONG)    { advance(p); return FB_LONG; }
    else if (t->kind == TOK_KW_SINGLE)  { advance(p); return FB_SINGLE; }
    else if (t->kind == TOK_KW_DOUBLE)  { advance(p); return FB_DOUBLE; }
    else if (t->kind == TOK_KW_STRING)  {
        advance(p);
        /* Check for STRING * n */
        if (match(p, TOK_STAR)) {
            /* Fixed-length string — capture the length */
            const Token* lt = peek(p);
            if (lt->kind == TOK_INTEGER_LIT) {
                p->last_fixed_str_len = lt->value.int_val;
            } else if (lt->kind == TOK_LONG_LIT) {
                p->last_fixed_str_len = lt->value.long_val;
            } else {
                p->last_fixed_str_len = 0;
            }
            advance(p); /* consume length */
        } else {
            p->last_fixed_str_len = 0;
        }
        return FB_STRING;
    }
    else if (t->kind == TOK_IDENT) {
        /* User-defined type name */
        if (udt_name) strncpy(udt_name, t->value.str.text, 41);
        advance(p);
        return FB_LONG; /* UDTs treated as LONG for now */
    }

    fb_syntax_error(t->line, t->col, "Expected type name after AS");
    return FB_SINGLE;
}

/* --- Expression parser (precedence climbing) --- */

/* Precedence levels from lowest to highest */
static int get_binop_prec(TokenKind k) {
    switch (k) {
        case TOK_KW_IMP: return 1;
        case TOK_KW_EQV: return 2;
        case TOK_KW_XOR: return 3;
        case TOK_KW_OR:  return 4;
        case TOK_KW_AND: return 5;
        case TOK_KW_NOT: return 6; /* NOT is unary, but also used in precedence */
        case TOK_EQ: case TOK_NE:
        case TOK_LT: case TOK_GT:
        case TOK_LE: case TOK_GE: return 7;
        case TOK_PLUS: case TOK_MINUS: return 8;
        case TOK_KW_MOD: return 9;
        case TOK_BACKSLASH: return 10;
        case TOK_STAR: case TOK_SLASH: return 11;
        case TOK_CARET: return 13; /* Highest arithmetic */
        default: return 0;
    }
}

static int is_relop(TokenKind k) {
    return k == TOK_EQ || k == TOK_NE || k == TOK_LT ||
           k == TOK_GT || k == TOK_LE || k == TOK_GE;
}

static int is_identifier(TokenKind k) {
    return k >= TOK_IDENT && k <= TOK_IDENT_DOUBLE;
}

/* Check if a keyword token is a built-in function (returns 1 if yes) */
static int is_builtin_func(TokenKind k) {
    switch (k) {
        case TOK_KW_ABS: case TOK_KW_ASC: case TOK_KW_ATN:
        case TOK_KW_CDBL: case TOK_KW_CHR: case TOK_KW_CINT:
        case TOK_KW_CLNG: case TOK_KW_COS: case TOK_KW_CSNG:
        case TOK_KW_CSRLIN:
        case TOK_KW_CVD: case TOK_KW_CVDMBF: case TOK_KW_CVI:
        case TOK_KW_CVL: case TOK_KW_CVS: case TOK_KW_CVSMBF:
        case TOK_KW_DATE:
        case TOK_KW_ENVIRON:
        case TOK_KW_EOF: case TOK_KW_EXP: case TOK_KW_ERR: case TOK_KW_ERL:
        case TOK_KW_FILEATTR: case TOK_KW_FIX: case TOK_KW_FRE:
        case TOK_KW_FREEFILE:
        case TOK_KW_HEX:
        case TOK_KW_INKEY: case TOK_KW_INP: case TOK_KW_INPUT:
        case TOK_KW_INSTR: case TOK_KW_INT:
        case TOK_KW_LBOUND: case TOK_KW_LCASE: case TOK_KW_LEFT:
        case TOK_KW_LEN: case TOK_KW_LOC: case TOK_KW_LOF:
        case TOK_KW_LOG: case TOK_KW_LTRIM:
        case TOK_KW_MID: case TOK_KW_MKD: case TOK_KW_MKDMBF:
        case TOK_KW_MKI: case TOK_KW_MKL: case TOK_KW_MKS:
        case TOK_KW_MKSMBF:
        case TOK_KW_OCT:
        case TOK_KW_PEEK: case TOK_KW_PLAY: case TOK_KW_PMAP:
        case TOK_KW_POINT: case TOK_KW_POS:
        case TOK_KW_RIGHT: case TOK_KW_RND: case TOK_KW_RTRIM:
        case TOK_KW_SADD: case TOK_KW_SEEK: case TOK_KW_SGN:
        case TOK_KW_SIN: case TOK_KW_SPACE: case TOK_KW_SPC:
        case TOK_KW_SQR: case TOK_KW_STR: case TOK_KW_STRING:
        case TOK_KW_TAB: case TOK_KW_TAN: case TOK_KW_TIME:
        case TOK_KW_TIMER:
        case TOK_KW_UBOUND: case TOK_KW_UCASE:
        case TOK_KW_VAL: case TOK_KW_VARPTR: case TOK_KW_VARSEG:
        case TOK_KW_COMMAND:
            return 1;
        default: return 0;
    }
}

static ASTNode* parse_primary(Parser* p) {
    const Token* t = peek(p);

    /* Numeric literals */
    if (t->kind == TOK_INTEGER_LIT) {
        advance(p);
        return ast_literal(t->line, fbval_int(t->value.int_val));
    }
    if (t->kind == TOK_LONG_LIT) {
        advance(p);
        return ast_literal(t->line, fbval_long(t->value.long_val));
    }
    if (t->kind == TOK_SINGLE_LIT) {
        advance(p);
        return ast_literal(t->line, fbval_single(t->value.single_val));
    }
    if (t->kind == TOK_DOUBLE_LIT) {
        advance(p);
        return ast_literal(t->line, fbval_double(t->value.double_val));
    }
    if (t->kind == TOK_STRING_LIT) {
        advance(p);
        FBValue sv = fbval_string_from_cstr(t->value.str.text);
        ASTNode* n = ast_literal(t->line, sv);
        fbval_release(&sv);
        return n;
    }

    /* Parenthesized expression */
    if (t->kind == TOK_LPAREN) {
        advance(p);
        ASTNode* expr = parse_expression(p);
        expect(p, TOK_RPAREN);
        return ast_paren(t->line, expr);
    }

    /* Built-in functions */
    if (is_builtin_func(t->kind)) {
        const Token* func_tok = advance(p);
        char fname[42];
        if (func_tok->value.str.text)
            strncpy(fname, func_tok->value.str.text, sizeof(fname) - 1);
        else
            strncpy(fname, token_kind_name(func_tok->kind), sizeof(fname) - 1);
        fname[41] = '\0';

        /* Some functions don't need parentheses (e.g., TIMER, INKEY$, CSRLIN, FREEFILE, DATE$, TIME$, ERR, ERL, RND, COMMAND$) */
        if (!at(p, TOK_LPAREN)) {
            /* Zero-arg function */
            ASTNode** args = NULL;
            return ast_func_call(func_tok->line, fname, args, 0);
        }

        expect(p, TOK_LPAREN);
        /* Parse argument list */
        ASTNode** args = NULL;
        int arg_count = 0;
        int arg_cap = 0;

        if (!at(p, TOK_RPAREN)) {
            do {
                ASTNode* arg = parse_expression(p);
                if (arg_count >= arg_cap) {
                    arg_cap = arg_cap ? arg_cap * 2 : 4;
                    args = realloc(args, arg_cap * sizeof(ASTNode*));
                }
                args[arg_count++] = arg;
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN);
        return ast_func_call(func_tok->line, fname, args, arg_count);
    }

    /* Identifier (variable or function call or array access) */
    if (is_identifier(t->kind)) {
        const Token* id_tok = advance(p);
        FBType type_hint = type_from_ident_token(id_tok->kind);

        /* Check for ( — could be array access or function call */
        if (at(p, TOK_LPAREN)) {
            advance(p); /* consume ( */
            ASTNode** args = NULL;
            int arg_count = 0;
            int arg_cap = 0;

            if (!at(p, TOK_RPAREN)) {
                do {
                    ASTNode* arg = parse_expression(p);
                    if (arg_count >= arg_cap) {
                        arg_cap = arg_cap ? arg_cap * 2 : 4;
                        args = realloc(args, arg_cap * sizeof(ASTNode*));
                    }
                    args[arg_count++] = arg;
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN);

            /* We'll create this as an array access; the interpreter
               will check if it's actually a function call */
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_ARRAY_ACCESS;
            n->line = id_tok->line;
            strncpy(n->data.array_access.name, id_tok->value.str.text,
                    sizeof(n->data.array_access.name) - 1);
            n->data.array_access.subscripts = args;
            n->data.array_access.nsubs = arg_count;

            /* Check for .field (UDT member access on array element) */
            ASTNode* var = n;
            while (at(p, TOK_DOT)) {
                advance(p); /* consume . */
                const Token* field = peek(p);
                if (!is_identifier(field->kind) && !(field->kind >= TOK_KW_ABS && field->kind < TOK_LINENO)) {
                    fb_syntax_error(field->line, field->col, "Expected field name after '.'");
                }
                const Token* ft = advance(p);
                ASTNode* udt = calloc(1, sizeof(ASTNode));
                udt->kind = AST_UDT_MEMBER;
                udt->line = ft->line;
                udt->data.udt_member.base = var;
                strncpy(udt->data.udt_member.field, ft->value.str.text,
                        sizeof(udt->data.udt_member.field) - 1);
                var = udt;
            }
            return var;
        }

        /* Check for .field (UDT member access) */
        ASTNode* var = ast_variable(id_tok->line, id_tok->value.str.text, type_hint);
        while (at(p, TOK_DOT)) {
            advance(p); /* consume . */
            const Token* field = peek(p);
            if (!is_identifier(field->kind) && !(field->kind >= TOK_KW_ABS && field->kind < TOK_LINENO)) {
                fb_syntax_error(field->line, field->col, "Expected field name after '.'");
            }
            const Token* ft = advance(p);
            ASTNode* udt = calloc(1, sizeof(ASTNode));
            udt->kind = AST_UDT_MEMBER;
            udt->line = ft->line;
            udt->data.udt_member.base = var;
            strncpy(udt->data.udt_member.field, ft->value.str.text,
                    sizeof(udt->data.udt_member.field) - 1);
            var = udt;
        }
        return var;
    }

    /* Line number as literal (for GOTO 100, etc.) */
    if (t->kind == TOK_LINENO) {
        advance(p);
        return ast_literal(t->line, fbval_long(t->value.long_val));
    }

    fb_syntax_error(t->line, t->col, "Unexpected token in expression");
    return NULL;
}

static ASTNode* parse_unary(Parser* p) {
    const Token* t = peek(p);

    if (t->kind == TOK_MINUS) {
        advance(p);
        ASTNode* operand = parse_unary(p);
        return ast_unop(t->line, TOK_MINUS, operand);
    }
    if (t->kind == TOK_PLUS) {
        advance(p);
        return parse_unary(p);
    }
    if (t->kind == TOK_KW_NOT) {
        advance(p);
        ASTNode* operand = parse_unary(p);
        return ast_unop(t->line, TOK_KW_NOT, operand);
    }

    return parse_primary(p);
}

static ASTNode* parse_binop(Parser* p, int min_prec) {
    ASTNode* left = parse_unary(p);

    while (1) {
        TokenKind op = peek(p)->kind;
        int prec = get_binop_prec(op);
        if (prec == 0 || prec < min_prec) break;

        int line = peek(p)->line;
        advance(p);

        /* Right-associativity for ^ */
        int next_min = (op == TOK_CARET) ? prec : prec + 1;
        ASTNode* right = parse_binop(p, next_min);

        if (is_relop(op))
            left = ast_binop(line, op, left, right); /* comparisons are also binops */
        else
            left = ast_binop(line, op, left, right);
    }

    return left;
}

static ASTNode* parse_expression(Parser* p) {
    return parse_binop(p, 1);
}

/* --- Statement parsers --- */

/* Parse a variable target for assignment (identifier, array element, udt.field) */
static ASTNode* parse_lvalue(Parser* p) {
    return parse_primary(p); /* Returns variable, array access, or UDT member */
}

static ASTNode* parse_print(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume PRINT */

    int filenum = 0;
    /* Check for PRINT # */
    if (at(p, TOK_HASH)) {
        advance(p);
        const Token* fn = expect(p, TOK_INTEGER_LIT);
        filenum = fn->value.int_val;
        expect(p, TOK_COMMA);
    }

    /* Check for PRINT USING */
    if (at(p, TOK_KW_USING)) {
        advance(p);
        ASTNode* format_expr = parse_expression(p);
        expect(p, TOK_SEMICOLON);

        ASTNode** items = NULL;
        int item_count = 0;
        int item_cap = 0;

        while (!at_end_of_statement(p)) {
            ASTNode* item = parse_expression(p);
            if (item_count >= item_cap) {
                item_cap = item_cap ? item_cap * 2 : 4;
                items = realloc(items, item_cap * sizeof(ASTNode*));
            }
            items[item_count++] = item;
            if (!match(p, TOK_SEMICOLON) && !match(p, TOK_COMMA))
                break;
        }

        ASTNode* n = calloc(1, sizeof(ASTNode));
        n->kind = AST_PRINT_USING;
        n->line = line;
        n->data.print_using.format_expr = format_expr;
        n->data.print_using.items = items;
        n->data.print_using.item_count = item_count;
        n->data.print_using.filenum = filenum;
        return n;
    }

    ASTNode** items = NULL;
    int* seps = NULL;
    int count = 0;
    int cap = 0;
    int trailing_sep = 0;

    while (!at_end_of_statement(p) && peek(p)->kind != TOK_KW_ELSE) {
        /* Handle SPC and TAB specially */
        if (at(p, TOK_KW_SPC) || at(p, TOK_KW_TAB)) {
            const Token* func_tok = advance(p);
            expect(p, TOK_LPAREN);
            ASTNode* arg = parse_expression(p);
            expect(p, TOK_RPAREN);
            ASTNode** args = malloc(sizeof(ASTNode*));
            args[0] = arg;
            ASTNode* fc = ast_func_call(func_tok->line, func_tok->value.str.text, args, 1);

            if (count >= cap) {
                cap = cap ? cap * 2 : 8;
                items = realloc(items, cap * sizeof(ASTNode*));
                seps = realloc(seps, cap * sizeof(int));
            }
            items[count] = fc;
            seps[count] = 0;
            count++;
            trailing_sep = 0;

            if (at(p, TOK_SEMICOLON)) { seps[count-1] = TOK_SEMICOLON; advance(p); trailing_sep = 1; }
            else if (at(p, TOK_COMMA)) { seps[count-1] = TOK_COMMA; advance(p); trailing_sep = 1; }
            continue;
        }

        ASTNode* item = parse_expression(p);
        if (count >= cap) {
            cap = cap ? cap * 2 : 8;
            items = realloc(items, cap * sizeof(ASTNode*));
            seps = realloc(seps, cap * sizeof(int));
        }
        items[count] = item;
        seps[count] = 0;
        count++;
        trailing_sep = 0;

        if (at(p, TOK_SEMICOLON)) {
            seps[count-1] = TOK_SEMICOLON;
            advance(p);
            trailing_sep = 1;
        } else if (at(p, TOK_COMMA)) {
            seps[count-1] = TOK_COMMA;
            advance(p);
            trailing_sep = 1;
        }
    }

    ASTNode* n = ast_print(line, items, seps, count, trailing_sep);
    n->data.print.filenum = filenum;
    return n;
}

static ASTNode* parse_if(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume IF */

    ASTNode* condition = parse_expression(p);
    expect(p, TOK_KW_THEN);

    /* Decide: block IF or single-line IF */
    if (at_end_of_statement(p)) {
        /* Block IF */
        if (at(p, TOK_COLON)) advance(p);
        skip_eol(p);

        /* Parse THEN body */
        int then_count = 0;
        int then_cap = 0;
        ASTNode** then_body = NULL;

        while (!at(p, TOK_KW_ELSEIF) && !at(p, TOK_KW_ELSE) && !at(p, TOK_KW_END) && !at(p, TOK_EOF)) {
            ASTNode* stmt = parse_statement(p);
            if (stmt) {
                if (then_count >= then_cap) {
                    then_cap = then_cap ? then_cap * 2 : 8;
                    then_body = realloc(then_body, then_cap * sizeof(ASTNode*));
                }
                then_body[then_count++] = stmt;
            }
            if (at(p, TOK_COLON)) advance(p);
            else { skip_eol(p); }
        }

        /* Parse ELSEIF clauses */
        ASTNode** elseif_cond = NULL;
        ASTNode*** elseif_body = NULL;
        int* elseif_count = NULL;
        int elseif_n = 0;
        int elseif_cap = 0;

        while (at(p, TOK_KW_ELSEIF)) {
            advance(p);
            ASTNode* eicond = parse_expression(p);
            expect(p, TOK_KW_THEN);
            skip_eol(p);

            int ebc = 0;
            int ebc_cap = 0;
            ASTNode** eibody = NULL;

            while (!at(p, TOK_KW_ELSEIF) && !at(p, TOK_KW_ELSE) && !at(p, TOK_KW_END) && !at(p, TOK_EOF)) {
                ASTNode* stmt = parse_statement(p);
                if (stmt) {
                    if (ebc >= ebc_cap) {
                        ebc_cap = ebc_cap ? ebc_cap * 2 : 8;
                        eibody = realloc(eibody, ebc_cap * sizeof(ASTNode*));
                    }
                    eibody[ebc++] = stmt;
                }
                if (at(p, TOK_COLON)) advance(p);
                else { skip_eol(p); }
            }

            if (elseif_n >= elseif_cap) {
                elseif_cap = elseif_cap ? elseif_cap * 2 : 4;
                elseif_cond = realloc(elseif_cond, elseif_cap * sizeof(ASTNode*));
                elseif_body = realloc(elseif_body, elseif_cap * sizeof(ASTNode**));
                elseif_count = realloc(elseif_count, elseif_cap * sizeof(int));
            }
            elseif_cond[elseif_n] = eicond;
            elseif_body[elseif_n] = eibody;
            elseif_count[elseif_n] = ebc;
            elseif_n++;
        }

        /* Parse ELSE clause */
        int else_count = 0;
        int else_cap = 0;
        ASTNode** else_body = NULL;

        if (at(p, TOK_KW_ELSE)) {
            advance(p);
            skip_eol(p);

            while (!at(p, TOK_KW_END) && !at(p, TOK_EOF)) {
                ASTNode* stmt = parse_statement(p);
                if (stmt) {
                    if (else_count >= else_cap) {
                        else_cap = else_cap ? else_cap * 2 : 8;
                        else_body = realloc(else_body, else_cap * sizeof(ASTNode*));
                    }
                    else_body[else_count++] = stmt;
                }
                if (at(p, TOK_COLON)) advance(p);
                else { skip_eol(p); }
            }
        }

        /* Expect END IF */
        expect(p, TOK_KW_END);
        expect(p, TOK_KW_IF);

        ASTNode* n = calloc(1, sizeof(ASTNode));
        n->kind = AST_IF;
        n->line = line;
        n->data.if_block.condition = condition;
        n->data.if_block.then_body = then_body;
        n->data.if_block.then_count = then_count;
        n->data.if_block.elseif_cond = elseif_cond;
        n->data.if_block.elseif_body = elseif_body;
        n->data.if_block.elseif_count = elseif_count;
        n->data.if_block.elseif_n = elseif_n;
        n->data.if_block.else_body = else_body;
        n->data.if_block.else_count = else_count;
        return n;
    } else {
        /* Single-line IF */
        /* THEN can be followed by statements or a line number */
        int then_count = 0;
        int then_cap = 0;
        ASTNode** then_body = NULL;

        /* Check if THEN is followed by a line number (GOTO shorthand) */
        if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT) || at(p, TOK_LINENO)) {
            const Token* lt = advance(p);
            int32_t ln = (lt->kind == TOK_INTEGER_LIT) ? lt->value.int_val : lt->value.long_val;
            ASTNode* g = ast_goto(line, NULL, ln);
            then_body = malloc(sizeof(ASTNode*));
            then_body[0] = g;
            then_count = 1;
        } else {
            while (!at_end_of_statement(p) && !at(p, TOK_KW_ELSE)) {
                ASTNode* stmt = parse_statement(p);
                if (stmt) {
                    if (then_count >= then_cap) {
                        then_cap = then_cap ? then_cap * 2 : 4;
                        then_body = realloc(then_body, then_cap * sizeof(ASTNode*));
                    }
                    then_body[then_count++] = stmt;
                }
                if (at(p, TOK_COLON)) advance(p);
                else break;
            }
        }

        /* Parse optional ELSE */
        int else_count = 0;
        ASTNode** else_body = NULL;

        if (at(p, TOK_KW_ELSE)) {
            advance(p);
            int else_cap = 0;

            /* ELSE followed by line number */
            if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT) || at(p, TOK_LINENO)) {
                const Token* lt = advance(p);
                int32_t ln = (lt->kind == TOK_INTEGER_LIT) ? lt->value.int_val : lt->value.long_val;
                ASTNode* g = ast_goto(line, NULL, ln);
                else_body = malloc(sizeof(ASTNode*));
                else_body[0] = g;
                else_count = 1;
            } else {
                while (!at_end_of_statement(p)) {
                    ASTNode* stmt = parse_statement(p);
                    if (stmt) {
                        if (else_count >= else_cap) {
                            else_cap = else_cap ? else_cap * 2 : 4;
                            else_body = realloc(else_body, else_cap * sizeof(ASTNode*));
                        }
                        else_body[else_count++] = stmt;
                    }
                    if (at(p, TOK_COLON)) advance(p);
                    else break;
                }
            }
        }

        ASTNode* n = calloc(1, sizeof(ASTNode));
        n->kind = AST_IF;
        n->line = line;
        n->data.if_block.condition = condition;
        n->data.if_block.then_body = then_body;
        n->data.if_block.then_count = then_count;
        n->data.if_block.elseif_cond = NULL;
        n->data.if_block.elseif_body = NULL;
        n->data.if_block.elseif_count = NULL;
        n->data.if_block.elseif_n = 0;
        n->data.if_block.else_body = else_body;
        n->data.if_block.else_count = else_count;
        return n;
    }
}

static ASTNode* parse_for(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume FOR */

    ASTNode* var = parse_lvalue(p);
    expect(p, TOK_EQ);
    ASTNode* start = parse_expression(p);
    expect(p, TOK_KW_TO);
    ASTNode* end = parse_expression(p);

    ASTNode* step = NULL;
    if (match(p, TOK_KW_STEP)) {
        step = parse_expression(p);
    }

    skip_eol(p);

    /* Parse body until NEXT */
    int body_count = 0;
    int body_cap = 0;
    ASTNode** body = NULL;

    while (!at(p, TOK_KW_NEXT) && !at(p, TOK_EOF)) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (body_count >= body_cap) {
                body_cap = body_cap ? body_cap * 2 : 8;
                body = realloc(body, body_cap * sizeof(ASTNode*));
            }
            body[body_count++] = stmt;
        }
        if (at(p, TOK_COLON)) advance(p);
        else skip_eol(p);
    }

    expect(p, TOK_KW_NEXT);
    /* Optional variable after NEXT */
    if (is_identifier(peek(p)->kind)) {
        advance(p); /* skip the variable name — we already know which FOR */
        /* Handle NEXT i, j (multiple NEXTs) — just skip extra commas + vars */
        while (match(p, TOK_COMMA)) {
            if (is_identifier(peek(p)->kind)) advance(p);
        }
    }

    return ast_for(line, var, start, end, step, body, body_count);
}

static ASTNode* parse_while(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume WHILE */

    ASTNode* condition = parse_expression(p);
    skip_eol(p);

    int body_count = 0;
    int body_cap = 0;
    ASTNode** body = NULL;

    while (!at(p, TOK_KW_WEND) && !at(p, TOK_EOF)) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (body_count >= body_cap) {
                body_cap = body_cap ? body_cap * 2 : 8;
                body = realloc(body, body_cap * sizeof(ASTNode*));
            }
            body[body_count++] = stmt;
        }
        if (at(p, TOK_COLON)) advance(p);
        else skip_eol(p);
    }

    expect(p, TOK_KW_WEND);

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_WHILE;
    n->line = line;
    n->data.loop.condition = condition;
    n->data.loop.body = body;
    n->data.loop.body_count = body_count;
    n->data.loop.is_until = 0;
    n->data.loop.is_post = 0;
    return n;
}

static ASTNode* parse_do_loop(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume DO */

    ASTNode* condition = NULL;
    int is_until = 0;
    int is_post = 0;

    /* Check for WHILE/UNTIL after DO */
    if (at(p, TOK_KW_WHILE)) {
        advance(p);
        condition = parse_expression(p);
        is_until = 0;
    } else if (at(p, TOK_KW_UNTIL)) {
        advance(p);
        condition = parse_expression(p);
        is_until = 1;
    }

    skip_eol(p);

    int body_count = 0;
    int body_cap = 0;
    ASTNode** body = NULL;

    while (!at(p, TOK_KW_LOOP) && !at(p, TOK_EOF)) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (body_count >= body_cap) {
                body_cap = body_cap ? body_cap * 2 : 8;
                body = realloc(body, body_cap * sizeof(ASTNode*));
            }
            body[body_count++] = stmt;
        }
        if (at(p, TOK_COLON)) advance(p);
        else skip_eol(p);
    }

    expect(p, TOK_KW_LOOP);

    /* Check for WHILE/UNTIL after LOOP (post-test) */
    if (!condition) {
        if (at(p, TOK_KW_WHILE)) {
            advance(p);
            condition = parse_expression(p);
            is_until = 0;
            is_post = 1;
        } else if (at(p, TOK_KW_UNTIL)) {
            advance(p);
            condition = parse_expression(p);
            is_until = 1;
            is_post = 1;
        }
        /* If still no condition, it's an infinite loop (DO...LOOP) */
    }

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_DO_LOOP;
    n->line = line;
    n->data.loop.condition = condition;
    n->data.loop.body = body;
    n->data.loop.body_count = body_count;
    n->data.loop.is_until = is_until;
    n->data.loop.is_post = is_post;
    return n;
}

static ASTNode* parse_select_case(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume SELECT */
    expect(p, TOK_KW_CASE);

    ASTNode* test_expr = parse_expression(p);
    skip_eol(p);

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_SELECT_CASE;
    n->line = line;
    n->data.select_case.test_expr = test_expr;
    n->data.select_case.cases = NULL;
    n->data.select_case.case_count = 0;
    n->data.select_case.else_body = NULL;
    n->data.select_case.else_count = 0;

    int case_cap = 0;

    while (at(p, TOK_KW_CASE)) {
        advance(p); /* consume CASE */

        /* Check for CASE ELSE */
        if (at(p, TOK_KW_ELSE)) {
            advance(p);
            skip_eol(p);
            int ec = 0;
            int ec_cap = 0;
            ASTNode** eb = NULL;
            while (!at(p, TOK_KW_CASE) && !at(p, TOK_KW_END) && !at(p, TOK_EOF)) {
                ASTNode* stmt = parse_statement(p);
                if (stmt) {
                    if (ec >= ec_cap) {
                        ec_cap = ec_cap ? ec_cap * 2 : 4;
                        eb = realloc(eb, ec_cap * sizeof(ASTNode*));
                    }
                    eb[ec++] = stmt;
                }
                if (at(p, TOK_COLON)) advance(p);
                else skip_eol(p);
            }
            n->data.select_case.else_body = eb;
            n->data.select_case.else_count = ec;
            continue;
        }

        /* Parse case values */
        if (n->data.select_case.case_count >= case_cap) {
            case_cap = case_cap ? case_cap * 2 : 4;
            n->data.select_case.cases = realloc(n->data.select_case.cases,
                case_cap * sizeof(n->data.select_case.cases[0]));
        }
        int ci = n->data.select_case.case_count;
        memset(&n->data.select_case.cases[ci], 0, sizeof(n->data.select_case.cases[0]));

        int vc = 0;
        int vc_cap = 0;
        ASTNode** vals = NULL;
        int* kinds = NULL;
        TokenKind* relops = NULL;
        ASTNode** range_ends = NULL;

        do {
            if (vc >= vc_cap) {
                vc_cap = vc_cap ? vc_cap * 2 : 4;
                vals = realloc(vals, vc_cap * sizeof(ASTNode*));
                kinds = realloc(kinds, vc_cap * sizeof(int));
                relops = realloc(relops, vc_cap * sizeof(TokenKind));
                range_ends = realloc(range_ends, vc_cap * sizeof(ASTNode*));
            }
            range_ends[vc] = NULL;
            relops[vc] = TOK_EQ;

            if (at(p, TOK_KW_IS)) {
                advance(p);
                /* IS relop expr */
                TokenKind rop = peek(p)->kind;
                if (is_relop(rop)) advance(p);
                else rop = TOK_EQ;
                vals[vc] = parse_expression(p);
                kinds[vc] = 2;
                relops[vc] = rop;
            } else {
                vals[vc] = parse_expression(p);
                if (at(p, TOK_KW_TO)) {
                    advance(p);
                    range_ends[vc] = parse_expression(p);
                    kinds[vc] = 1; /* Range */
                } else {
                    kinds[vc] = 0; /* Simple value */
                }
            }
            vc++;
        } while (match(p, TOK_COMMA));

        n->data.select_case.cases[ci].values = vals;
        n->data.select_case.cases[ci].kinds = kinds;
        n->data.select_case.cases[ci].relops = relops;
        n->data.select_case.cases[ci].range_ends = range_ends;
        n->data.select_case.cases[ci].value_count = vc;

        skip_eol(p);

        /* Parse case body */
        int bc = 0;
        int bc_cap = 0;
        ASTNode** cb = NULL;
        while (!at(p, TOK_KW_CASE) && !at(p, TOK_KW_END) && !at(p, TOK_EOF)) {
            ASTNode* stmt = parse_statement(p);
            if (stmt) {
                if (bc >= bc_cap) {
                    bc_cap = bc_cap ? bc_cap * 2 : 4;
                    cb = realloc(cb, bc_cap * sizeof(ASTNode*));
                }
                cb[bc++] = stmt;
            }
            if (at(p, TOK_COLON)) advance(p);
            else skip_eol(p);
        }
        n->data.select_case.cases[ci].body = cb;
        n->data.select_case.cases[ci].body_count = bc;
        n->data.select_case.case_count++;
    }

    /* Expect END SELECT */
    expect(p, TOK_KW_END);
    expect(p, TOK_KW_SELECT);
    return n;
}

static ASTNode* parse_dim(Parser* p, int is_redim) {
    int line = peek(p)->line;
    advance(p); /* consume DIM or REDIM */

    int is_shared = 0;
    int is_static = 0;
    if (match(p, TOK_KW_SHARED)) is_shared = 1;
    if (match(p, TOK_KW_STATIC)) is_static = 1;

    /* DIM can declare multiple variables separated by commas */
    /* For simplicity, we handle one at a time and parse the first */
    do {
        const Token* name_tok = peek(p);
        if (!is_identifier(name_tok->kind)) {
            fb_syntax_error(name_tok->line, name_tok->col, "Expected variable name in DIM");
        }
        const Token* nt = advance(p);

        ASTNode* n = calloc(1, sizeof(ASTNode));
        n->kind = is_redim ? AST_REDIM : AST_DIM;
        n->line = line;
        strncpy(n->data.dim.name, nt->value.str.text, sizeof(n->data.dim.name) - 1);
        n->data.dim.type = type_from_ident_token(nt->kind);
        n->data.dim.is_shared = is_shared;
        n->data.dim.is_static = is_static;
        n->data.dim.is_dynamic = is_redim;
        n->data.dim.bounds = NULL;
        n->data.dim.ndims = 0;
        n->data.dim.as_type_name[0] = '\0';

        /* Check for array bounds */
        if (at(p, TOK_LPAREN)) {
            advance(p);
            if (!at(p, TOK_RPAREN)) {
                int ndims = 0;
                int bounds_cap = 0;
                ASTNode** bounds = NULL;

                do {
                    ASTNode* first = parse_expression(p);
                    ASTNode* lower = NULL;
                    ASTNode* upper = NULL;

                    if (match(p, TOK_KW_TO)) {
                        lower = first;
                        upper = parse_expression(p);
                    } else {
                        lower = NULL; /* Use OPTION BASE default */
                        upper = first;
                    }

                    if (ndims * 2 + 1 >= bounds_cap) {
                        bounds_cap = (bounds_cap + 2) * 2;
                        bounds = realloc(bounds, bounds_cap * sizeof(ASTNode*));
                    }
                    bounds[ndims * 2] = lower;
                    bounds[ndims * 2 + 1] = upper;
                    ndims++;
                } while (match(p, TOK_COMMA));

                n->data.dim.bounds = bounds;
                n->data.dim.ndims = ndims;
            }
            expect(p, TOK_RPAREN);
        }

        /* Check for AS type */
        char udt_name[42] = {0};
        FBType as_type = parse_as_type(p, udt_name);
        if ((int)as_type != -1) {
            n->data.dim.type = as_type;
        }
        if (udt_name[0]) {
            strncpy(n->data.dim.as_type_name, udt_name, sizeof(n->data.dim.as_type_name) - 1);
        }

        /* Add to program statements */
        if (p->prog->stmt_count >= p->prog->stmt_cap) {
            p->prog->stmt_cap = p->prog->stmt_cap ? p->prog->stmt_cap * 2 : 64;
            p->prog->statements = realloc(p->prog->statements,
                p->prog->stmt_cap * sizeof(ASTNode*));
        }
        p->prog->statements[p->prog->stmt_count++] = n;

    } while (match(p, TOK_COMMA));

    /* Return the last DIM node (the caller will skip adding it since we added them above) */
    return NULL;
}

static ASTNode* parse_const(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume CONST */

    do {
        const Token* name_tok = peek(p);
        if (!is_identifier(name_tok->kind)) {
            fb_syntax_error(name_tok->line, name_tok->col, "Expected constant name");
        }
        const Token* nt = advance(p);
        expect(p, TOK_EQ);
        ASTNode* value_expr = parse_expression(p);

        ASTNode* n = calloc(1, sizeof(ASTNode));
        n->kind = AST_CONST_DECL;
        n->line = line;
        strncpy(n->data.const_decl.name, nt->value.str.text,
                sizeof(n->data.const_decl.name) - 1);
        n->data.const_decl.value_expr = value_expr;

        if (p->prog->stmt_count >= p->prog->stmt_cap) {
            p->prog->stmt_cap = p->prog->stmt_cap ? p->prog->stmt_cap * 2 : 64;
            p->prog->statements = realloc(p->prog->statements,
                p->prog->stmt_cap * sizeof(ASTNode*));
        }
        p->prog->statements[p->prog->stmt_count++] = n;
    } while (match(p, TOK_COMMA));

    return NULL;
}

static ASTNode* parse_input(Parser* p) {
    int line = peek(p)->line;
    int is_line_input = 0;

    if (at(p, TOK_KW_LINE)) {
        advance(p); /* consume LINE */
        expect(p, TOK_KW_INPUT);
        is_line_input = 1;
    } else {
        advance(p); /* consume INPUT */
    }

    int filenum = 0;
    if (at(p, TOK_HASH)) {
        advance(p);
        const Token* fn = peek(p);
        if (fn->kind == TOK_INTEGER_LIT) {
            filenum = fn->value.int_val;
            advance(p);
        }
        expect(p, TOK_COMMA);
    }

    ASTNode* prompt = NULL;
    int no_newline = 0;

    /* Check for prompt string */
    if (filenum == 0 && at(p, TOK_STRING_LIT)) {
        const Token* st = peek(p);
        /* Look ahead to see if it's followed by ; or , (making it a prompt) */
        const Token* after = peek_ahead(p, 1);
        if (after->kind == TOK_SEMICOLON || after->kind == TOK_COMMA) {
            FBValue sv = fbval_string_from_cstr(st->value.str.text);
            prompt = ast_literal(st->line, sv);
            fbval_release(&sv);
            advance(p);
            if (match(p, TOK_SEMICOLON)) no_newline = 1;
            else if (match(p, TOK_COMMA)) no_newline = 0;
        }
    }

    /* Parse variable list */
    ASTNode** vars = NULL;
    int var_count = 0;
    int var_cap = 0;

    do {
        ASTNode* var = parse_lvalue(p);
        if (var_count >= var_cap) {
            var_cap = var_cap ? var_cap * 2 : 4;
            vars = realloc(vars, var_cap * sizeof(ASTNode*));
        }
        vars[var_count++] = var;
    } while (match(p, TOK_COMMA));

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = is_line_input ? AST_LINE_INPUT : AST_INPUT;
    n->line = line;
    n->data.input.prompt = prompt;
    n->data.input.vars = vars;
    n->data.input.var_count = var_count;
    n->data.input.no_newline = no_newline;
    n->data.input.filenum = filenum;
    return n;
}

static ASTNode* parse_deftype(Parser* p) {
    int line = peek(p)->line;
    const Token* dt = advance(p);

    FBType type;
    switch (dt->kind) {
        case TOK_KW_DEFINT: type = FB_INTEGER; break;
        case TOK_KW_DEFLNG: type = FB_LONG; break;
        case TOK_KW_DEFSNG: type = FB_SINGLE; break;
        case TOK_KW_DEFDBL: type = FB_DOUBLE; break;
        case TOK_KW_DEFSTR: type = FB_STRING; break;
        default: type = FB_SINGLE; break;
    }

    /* Parse letter ranges: A-Z, or A, B */
    do {
        const Token* start_tok = peek(p);
        char range_start, range_end;
        if (is_identifier(start_tok->kind) && start_tok->value.str.length == 1) {
            range_start = toupper(start_tok->value.str.text[0]);
            advance(p);
        } else {
            fb_syntax_error(start_tok->line, start_tok->col,
                           "Expected letter in DEFTYPE statement");
            return NULL;
        }

        if (match(p, TOK_MINUS)) {
            const Token* end_tok = peek(p);
            if (is_identifier(end_tok->kind) && end_tok->value.str.length == 1) {
                range_end = toupper(end_tok->value.str.text[0]);
                advance(p);
            } else {
                range_end = range_start;
            }
        } else {
            range_end = range_start;
        }

        ASTNode* n = calloc(1, sizeof(ASTNode));
        n->kind = AST_DEFTYPE;
        n->line = line;
        n->data.deftype.type = type;
        n->data.deftype.range_start = range_start;
        n->data.deftype.range_end = range_end;

        if (p->prog->stmt_count >= p->prog->stmt_cap) {
            p->prog->stmt_cap = p->prog->stmt_cap ? p->prog->stmt_cap * 2 : 64;
            p->prog->statements = realloc(p->prog->statements,
                p->prog->stmt_cap * sizeof(ASTNode*));
        }
        p->prog->statements[p->prog->stmt_count++] = n;
    } while (match(p, TOK_COMMA));

    return NULL;
}

static ASTNode* parse_sub_def(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume SUB */

    const Token* name_tok = peek(p);
    if (!is_identifier(name_tok->kind)) {
        fb_syntax_error(name_tok->line, name_tok->col, "Expected SUB name");
    }
    const Token* nt = advance(p);

    int is_static = 0;

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_SUB_DEF;
    n->line = line;
    strncpy(n->data.proc_def.name, nt->value.str.text, sizeof(n->data.proc_def.name) - 1);
    n->data.proc_def.params = NULL;
    n->data.proc_def.param_count = 0;
    n->data.proc_def.is_static = 0;
    n->data.proc_def.return_type = FB_INTEGER; /* N/A for SUB */

    /* Parse parameters */
    if (at(p, TOK_LPAREN)) {
        advance(p);
        if (!at(p, TOK_RPAREN)) {
            int pcap = 0;
            do {
                int is_byval = 0;
                if (match(p, TOK_KW_BYVAL)) is_byval = 1; /* FB doesn't have BYVAL keyword but we support it */

                const Token* pname = peek(p);
                if (!is_identifier(pname->kind)) break;
                const Token* pt = advance(p);

                FBType ptype = type_from_ident_token(pt->kind);
                int is_array = 0;

                /* Check for () after param name (array parameter) */
                if (at(p, TOK_LPAREN)) {
                    advance(p);
                    expect(p, TOK_RPAREN);
                    is_array = 1;
                }

                /* Check for AS type */
                char dummy[42];
                FBType at_type = parse_as_type(p, dummy);
                if ((int)at_type != -1) ptype = at_type;

                if (n->data.proc_def.param_count >= pcap) {
                    pcap = pcap ? pcap * 2 : 4;
                    n->data.proc_def.params = realloc(n->data.proc_def.params,
                        pcap * sizeof(n->data.proc_def.params[0]));
                }
                int pi = n->data.proc_def.param_count++;
                strncpy(n->data.proc_def.params[pi].pname, pt->value.str.text,
                        sizeof(n->data.proc_def.params[pi].pname) - 1);
                n->data.proc_def.params[pi].ptype = ptype;
                n->data.proc_def.params[pi].is_byval = is_byval;
                n->data.proc_def.params[pi].is_array = is_array;
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN);
    }

    if (match(p, TOK_KW_STATIC)) {
        is_static = 1;
        n->data.proc_def.is_static = 1;
    }
    (void)is_static;

    skip_eol(p);

    /* Parse body until END SUB */
    int body_count = 0;
    int body_cap = 0;
    ASTNode** body = NULL;

    while (!(at(p, TOK_KW_END) && peek_ahead(p, 1)->kind == TOK_KW_SUB) && !at(p, TOK_EOF)) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (body_count >= body_cap) {
                body_cap = body_cap ? body_cap * 2 : 8;
                body = realloc(body, body_cap * sizeof(ASTNode*));
            }
            body[body_count++] = stmt;
        }
        if (at(p, TOK_COLON)) advance(p);
        else skip_eol(p);
    }

    expect(p, TOK_KW_END);
    expect(p, TOK_KW_SUB);

    n->data.proc_def.body = body;
    n->data.proc_def.body_count = body_count;
    return n;
}

static ASTNode* parse_function_def(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume FUNCTION */

    const Token* name_tok = peek(p);
    if (!is_identifier(name_tok->kind)) {
        fb_syntax_error(name_tok->line, name_tok->col, "Expected FUNCTION name");
    }
    const Token* nt = advance(p);

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_FUNCTION_DEF;
    n->line = line;
    strncpy(n->data.proc_def.name, nt->value.str.text, sizeof(n->data.proc_def.name) - 1);
    n->data.proc_def.params = NULL;
    n->data.proc_def.param_count = 0;
    n->data.proc_def.is_static = 0;
    n->data.proc_def.return_type = type_from_ident_token(nt->kind);

    /* Parse parameters */
    if (at(p, TOK_LPAREN)) {
        advance(p);
        if (!at(p, TOK_RPAREN)) {
            int pcap = 0;
            do {
                const Token* pname = peek(p);
                if (!is_identifier(pname->kind)) break;
                const Token* pt = advance(p);
                FBType ptype = type_from_ident_token(pt->kind);

                int is_array = 0;
                if (at(p, TOK_LPAREN)) { advance(p); expect(p, TOK_RPAREN); is_array = 1; }

                char dummy[42];
                FBType at_type = parse_as_type(p, dummy);
                if ((int)at_type != -1) ptype = at_type;

                if (n->data.proc_def.param_count >= pcap) {
                    pcap = pcap ? pcap * 2 : 4;
                    n->data.proc_def.params = realloc(n->data.proc_def.params,
                        pcap * sizeof(n->data.proc_def.params[0]));
                }
                int pi = n->data.proc_def.param_count++;
                strncpy(n->data.proc_def.params[pi].pname, pt->value.str.text, 41);
                n->data.proc_def.params[pi].ptype = ptype;
                n->data.proc_def.params[pi].is_byval = 0;
                n->data.proc_def.params[pi].is_array = is_array;
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN);
    }

    /* AS return type */
    char dummy[42];
    FBType ret_type = parse_as_type(p, dummy);
    if ((int)ret_type != -1) n->data.proc_def.return_type = ret_type;

    if (match(p, TOK_KW_STATIC)) n->data.proc_def.is_static = 1;

    skip_eol(p);

    /* Parse body until END FUNCTION */
    int body_count = 0;
    int body_cap = 0;
    ASTNode** body = NULL;

    while (!(at(p, TOK_KW_END) && peek_ahead(p, 1)->kind == TOK_KW_FUNCTION) && !at(p, TOK_EOF)) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (body_count >= body_cap) {
                body_cap = body_cap ? body_cap * 2 : 8;
                body = realloc(body, body_cap * sizeof(ASTNode*));
            }
            body[body_count++] = stmt;
        }
        if (at(p, TOK_COLON)) advance(p);
        else skip_eol(p);
    }

    expect(p, TOK_KW_END);
    expect(p, TOK_KW_FUNCTION);

    n->data.proc_def.body = body;
    n->data.proc_def.body_count = body_count;
    return n;
}

static ASTNode* parse_declare(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume DECLARE */

    int is_function = 0;
    if (match(p, TOK_KW_FUNCTION)) is_function = 1;
    else expect(p, TOK_KW_SUB);

    const Token* nt = peek(p);
    if (!is_identifier(nt->kind)) {
        fb_syntax_error(nt->line, nt->col, "Expected procedure name in DECLARE");
    }
    advance(p);

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_DECLARE;
    n->line = line;
    n->data.declare.is_function = is_function;
    strncpy(n->data.declare.name, nt->value.str.text, sizeof(n->data.declare.name) - 1);
    n->data.declare.params = NULL;
    n->data.declare.param_count = 0;
    n->data.declare.return_type = type_from_ident_token(nt->kind);

    /* Parse parameter list */
    if (at(p, TOK_LPAREN)) {
        advance(p);
        if (!at(p, TOK_RPAREN)) {
            int pcap = 0;
            do {
                const Token* pt = peek(p);
                if (!is_identifier(pt->kind)) break;
                advance(p);
                FBType ptype = type_from_ident_token(pt->kind);

                int is_array = 0;
                if (at(p, TOK_LPAREN)) { advance(p); expect(p, TOK_RPAREN); is_array = 1; }

                char dummy[42];
                FBType at_type = parse_as_type(p, dummy);
                if ((int)at_type != -1) ptype = at_type;

                if (n->data.declare.param_count >= pcap) {
                    pcap = pcap ? pcap * 2 : 4;
                    n->data.declare.params = realloc(n->data.declare.params,
                        pcap * sizeof(n->data.declare.params[0]));
                }
                int pi = n->data.declare.param_count++;
                strncpy(n->data.declare.params[pi].pname, pt->value.str.text, 41);
                n->data.declare.params[pi].ptype = ptype;
                n->data.declare.params[pi].is_byval = 0;
                n->data.declare.params[pi].is_array = is_array;
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN);
    }

    return n;
}

static ASTNode* parse_type_def(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume TYPE */

    const Token* nt = peek(p);
    if (!is_identifier(nt->kind)) {
        fb_syntax_error(nt->line, nt->col, "Expected type name");
    }
    advance(p);
    skip_eol(p);

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_TYPE_DEF;
    n->line = line;
    strncpy(n->data.type_def.name, nt->value.str.text, sizeof(n->data.type_def.name) - 1);
    n->data.type_def.fields = NULL;
    n->data.type_def.field_count = 0;

    int fcap = 0;

    while (!(at(p, TOK_KW_END) && peek_ahead(p, 1)->kind == TOK_KW_TYPE) && !at(p, TOK_EOF)) {
        const Token* fname = peek(p);
        if (!is_identifier(fname->kind)) {
            skip_eol(p);
            continue;
        }
        advance(p);

        if (n->data.type_def.field_count >= fcap) {
            fcap = fcap ? fcap * 2 : 8;
            n->data.type_def.fields = realloc(n->data.type_def.fields,
                fcap * sizeof(n->data.type_def.fields[0]));
        }
        int fi = n->data.type_def.field_count++;
        memset(&n->data.type_def.fields[fi], 0, sizeof(n->data.type_def.fields[0]));
        strncpy(n->data.type_def.fields[fi].field_name, fname->value.str.text,
                sizeof(n->data.type_def.fields[fi].field_name) - 1);

        char dummy[42];
        p->last_fixed_str_len = 0;
        FBType ftype = parse_as_type(p, dummy);
        if ((int)ftype != -1) {
            n->data.type_def.fields[fi].type = ftype;
            n->data.type_def.fields[fi].string_len = p->last_fixed_str_len;
        } else {
            n->data.type_def.fields[fi].type = type_from_ident_token(fname->kind);
        }

        skip_eol(p);
    }

    expect(p, TOK_KW_END);
    expect(p, TOK_KW_TYPE);
    return n;
}

static ASTNode* parse_open(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume OPEN */

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_OPEN;
    n->line = line;
    n->data.open.reclen = NULL;
    n->data.open.access_mode = 0;
    n->data.open.lock_mode = 0;

    ASTNode* filename = parse_expression(p);
    n->data.open.filename = filename;

    /* Detect syntax: modern (OPEN file FOR mode AS #n) or legacy (OPEN mode$, #n, file$) */
    if (at(p, TOK_KW_FOR)) {
        advance(p);
        /* Parse mode */
        if (match(p, TOK_KW_INPUT)) n->data.open.mode = 0;
        else if (match(p, TOK_KW_OUTPUT)) n->data.open.mode = 1;
        else if (match(p, TOK_KW_APPEND)) n->data.open.mode = 2;
        else if (match(p, TOK_KW_RANDOM)) n->data.open.mode = 3;
        else if (match(p, TOK_KW_BINARY)) n->data.open.mode = 4;
        else fb_syntax_error(peek(p)->line, peek(p)->col, "Expected file mode");

        /* Optional ACCESS */
        if (match(p, TOK_KW_ACCESS)) {
            if (match(p, TOK_KW_READ)) n->data.open.access_mode = 1;
            else if (match(p, TOK_KW_WRITE)) n->data.open.access_mode = 2;
        }

        /* Optional LOCK */
        if (match(p, TOK_KW_LOCK)) {
            if (match(p, TOK_KW_READ)) n->data.open.lock_mode = 1;
            else if (match(p, TOK_KW_WRITE)) n->data.open.lock_mode = 2;
            else if (match(p, TOK_KW_SHARED)) n->data.open.lock_mode = 3;
        }

        expect(p, TOK_KW_AS);
        match(p, TOK_HASH);
        n->data.open.filenum = parse_expression(p);

        if (match(p, TOK_KW_LEN)) {
            expect(p, TOK_EQ);
            n->data.open.reclen = parse_expression(p);
        }
    } else if (at(p, TOK_COMMA)) {
        /* Legacy syntax: OPEN "mode", #n, "file" */
        /* First arg was the mode string, filename comes later */
        advance(p); /* consume , */
        match(p, TOK_HASH);
        n->data.open.filenum = parse_expression(p);
        expect(p, TOK_COMMA);

        /* Now the real filename */
        ast_free(n->data.open.filename);
        n->data.open.filename = parse_expression(p);

        /* The first expression was the mode string — we'd need to evaluate at runtime */
        n->data.open.mode = 0; /* Default; interpreter handles */

        if (match(p, TOK_COMMA)) {
            n->data.open.reclen = parse_expression(p);
        }
    } else {
        /* Just OPEN filename AS #n (defaults to RANDOM) */
        expect(p, TOK_KW_AS);
        match(p, TOK_HASH);
        n->data.open.filenum = parse_expression(p);
        n->data.open.mode = 3; /* RANDOM */
    }

    return n;
}

static ASTNode* parse_close(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume CLOSE */

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_CLOSE;
    n->line = line;
    n->data.close.filenums = NULL;
    n->data.close.filenum_count = 0;
    n->data.close.close_all = 0;

    if (at_end_of_statement(p)) {
        n->data.close.close_all = 1;
        return n;
    }

    int cap = 0;
    do {
        match(p, TOK_HASH);
        const Token* fn = peek(p);
        int fnum = 0;
        if (fn->kind == TOK_INTEGER_LIT) { fnum = fn->value.int_val; advance(p); }
        else {
            ASTNode* expr = parse_expression(p);
            /* For simplicity, only handle literal file numbers */
            if (expr->kind == AST_LITERAL) fnum = fbval_to_long(&expr->data.literal.value);
            ast_free(expr);
        }

        if (n->data.close.filenum_count >= cap) {
            cap = cap ? cap * 2 : 4;
            n->data.close.filenums = realloc(n->data.close.filenums, cap * sizeof(int));
        }
        n->data.close.filenums[n->data.close.filenum_count++] = fnum;
    } while (match(p, TOK_COMMA));

    return n;
}

static ASTNode* parse_data(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume DATA */

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_DATA;
    n->line = line;
    n->data.data.values = NULL;
    n->data.data.value_count = 0;

    int cap = 0;
    do {
        FBValue val;
        if (at(p, TOK_STRING_LIT)) {
            val = fbval_string_from_cstr(peek(p)->value.str.text);
            advance(p);
        } else if (at(p, TOK_INTEGER_LIT)) {
            val = fbval_int(peek(p)->value.int_val);
            advance(p);
        } else if (at(p, TOK_LONG_LIT)) {
            val = fbval_long(peek(p)->value.long_val);
            advance(p);
        } else if (at(p, TOK_SINGLE_LIT)) {
            val = fbval_single(peek(p)->value.single_val);
            advance(p);
        } else if (at(p, TOK_DOUBLE_LIT)) {
            val = fbval_double(peek(p)->value.double_val);
            advance(p);
        } else if (at(p, TOK_MINUS)) {
            advance(p);
            if (at(p, TOK_INTEGER_LIT)) { val = fbval_int(-peek(p)->value.int_val); advance(p); }
            else if (at(p, TOK_LONG_LIT)) { val = fbval_long(-peek(p)->value.long_val); advance(p); }
            else if (at(p, TOK_SINGLE_LIT)) { val = fbval_single(-peek(p)->value.single_val); advance(p); }
            else if (at(p, TOK_DOUBLE_LIT)) { val = fbval_double(-peek(p)->value.double_val); advance(p); }
            else {
                /* Unquoted string data — read until comma or EOL */
                val = fbval_string_from_cstr("-");
            }
        } else if (is_identifier(peek(p)->kind)) {
            /* Unquoted string data */
            char buf[256];
            int len = 0;
            while (!at(p, TOK_COMMA) && !at_end_of_statement(p)) {
                const Token* t = advance(p);
                if (t->value.str.text) {
                    int slen = strlen(t->value.str.text);
                    if (len + slen < 255) {
                        memcpy(buf + len, t->value.str.text, slen);
                        len += slen;
                    }
                }
            }
            buf[len] = '\0';
            val = fbval_string_from_cstr(buf);
        } else {
            /* Empty data item → empty string */
            val = fbval_string_from_cstr("");
        }

        if (n->data.data.value_count >= cap) {
            cap = cap ? cap * 2 : 4;
            n->data.data.values = realloc(n->data.data.values, cap * sizeof(FBValue));
        }
        n->data.data.values[n->data.data.value_count++] = val;
    } while (match(p, TOK_COMMA));

    return n;
}

static ASTNode* parse_read(Parser* p) {
    int line = peek(p)->line;
    advance(p); /* consume READ */

    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = AST_READ;
    n->line = line;

    ASTNode** vars = NULL;
    int count = 0;
    int cap = 0;

    do {
        ASTNode* var = parse_lvalue(p);
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            vars = realloc(vars, cap * sizeof(ASTNode*));
        }
        vars[count++] = var;
    } while (match(p, TOK_COMMA));

    n->data.read_stmt.vars = vars;
    n->data.read_stmt.var_count = count;
    return n;
}

/* Parse a generic statement */
static ASTNode* parse_statement(Parser* p) {
    const Token* t = peek(p);

    /* Skip blank lines */
    while (t->kind == TOK_EOL) {
        advance(p);
        t = peek(p);
    }

    if (t->kind == TOK_EOF) return NULL;

    /* Line number definition */
    if (t->kind == TOK_LINENO) {
        int32_t lineno = t->value.long_val;
        int line = t->line;
        advance(p);

        /* Register in line_map */
        if (p->prog->linemap_count >= p->prog->linemap_cap) {
            p->prog->linemap_cap = p->prog->linemap_cap ? p->prog->linemap_cap * 2 : 32;
            p->prog->line_map = realloc(p->prog->line_map,
                p->prog->linemap_cap * sizeof(p->prog->line_map[0]));
        }
        p->prog->line_map[p->prog->linemap_count].lineno = lineno;
        p->prog->line_map[p->prog->linemap_count].stmt_index = p->prog->stmt_count;
        p->prog->linemap_count++;

        /* Create label definition node */
        char lbl[42];
        snprintf(lbl, sizeof(lbl), "%d", lineno);
        ASTNode* n = ast_label_def(line, lbl);
        return n;
    }

    /* Label definition */
    if (t->kind == TOK_LABEL) {
        int line = t->line;
        char label[42];
        strncpy(label, t->value.str.text, sizeof(label) - 1);
        label[41] = '\0';
        advance(p);

        /* Register in label table */
        if (p->prog->label_count >= p->prog->label_cap) {
            p->prog->label_cap = p->prog->label_cap ? p->prog->label_cap * 2 : 32;
            p->prog->labels = realloc(p->prog->labels,
                p->prog->label_cap * sizeof(p->prog->labels[0]));
        }
        strncpy(p->prog->labels[p->prog->label_count].name, label, 41);
        p->prog->labels[p->prog->label_count].stmt_index = p->prog->stmt_count;
        p->prog->label_count++;

        return ast_label_def(line, label);
    }

    switch (t->kind) {
        case TOK_KW_PRINT:
        case TOK_KW_LPRINT:
            return parse_print(p);

        case TOK_KW_IF:
            return parse_if(p);

        case TOK_KW_FOR:
            return parse_for(p);

        case TOK_KW_WHILE:
            return parse_while(p);

        case TOK_KW_DO:
            return parse_do_loop(p);

        case TOK_KW_SELECT:
            return parse_select_case(p);

        case TOK_KW_DIM:
            parse_dim(p, 0);
            return NULL;

        case TOK_KW_REDIM:
            parse_dim(p, 1);
            return NULL;

        case TOK_KW_CONST:
            parse_const(p);
            return NULL;

        case TOK_KW_DEFINT:
        case TOK_KW_DEFLNG:
        case TOK_KW_DEFSNG:
        case TOK_KW_DEFDBL:
        case TOK_KW_DEFSTR:
            parse_deftype(p);
            return NULL;

        case TOK_KW_GOTO: {
            int line2 = t->line;
            advance(p);
            if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                const Token* lt = advance(p);
                int32_t ln = (lt->kind == TOK_INTEGER_LIT) ? lt->value.int_val : lt->value.long_val;
                return ast_goto(line2, NULL, ln);
            }
            const Token* lbl = peek(p);
            if (is_identifier(lbl->kind)) {
                advance(p);
                return ast_goto(line2, lbl->value.str.text, -1);
            }
            fb_syntax_error(t->line, t->col, "Expected label or line number after GOTO");
            return NULL;
        }

        case TOK_KW_GOSUB: {
            int line2 = t->line;
            advance(p);
            if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                const Token* lt = advance(p);
                int32_t ln = (lt->kind == TOK_INTEGER_LIT) ? lt->value.int_val : lt->value.long_val;
                return ast_gosub(line2, NULL, ln);
            }
            const Token* lbl = peek(p);
            if (is_identifier(lbl->kind)) {
                advance(p);
                return ast_gosub(line2, lbl->value.str.text, -1);
            }
            fb_syntax_error(t->line, t->col, "Expected label or line number after GOSUB");
            return NULL;
        }

        case TOK_KW_RETURN:
            advance(p);
            return ast_return(t->line);

        case TOK_KW_END: {
            int line2 = t->line;
            advance(p);
            /* Check if END is followed by a block keyword (END IF, END SUB, etc.) */
            /* These are handled by their parent parsers, so just return AST_END */
            if (at(p, TOK_KW_IF) || at(p, TOK_KW_SUB) || at(p, TOK_KW_FUNCTION) ||
                at(p, TOK_KW_SELECT) || at(p, TOK_KW_TYPE)) {
                /* Put END back and let parent handle */
                p->pos--;
                return NULL;
            }
            return ast_end(line2);
        }

        case TOK_KW_STOP:
            advance(p);
            return ast_stop(t->line);

        case TOK_KW_SYSTEM:
            advance(p);
            return ast_end(t->line); /* SYSTEM acts like END */

        case TOK_KW_REM:
            advance(p);
            return ast_rem(t->line);

        case TOK_KW_CLS:
            advance(p);
            return ast_cls(t->line);

        case TOK_KW_BEEP:
            advance(p);
            return ast_beep(t->line);

        case TOK_KW_INPUT:
            return parse_input(p);

        case TOK_KW_LINE: {
            /* LINE INPUT or LINE (x1,y1)-(x2,y2) for graphics */
            if (peek_ahead(p, 1)->kind == TOK_KW_INPUT) {
                return parse_input(p);
            }
            /* Graphics LINE — placeholder */
            advance(p);
            /* Skip rest of line for now */
            while (!at_end_of_statement(p)) advance(p);
            return ast_rem(t->line);
        }

        case TOK_KW_WRITE: {
            int line2 = t->line;
            advance(p);
            int filenum = 0;
            if (at(p, TOK_HASH)) {
                advance(p);
                if (at(p, TOK_INTEGER_LIT)) { filenum = peek(p)->value.int_val; advance(p); }
                expect(p, TOK_COMMA);
            }
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_WRITE_STMT;
            n->line = line2;
            n->data.write_stmt.filenum = filenum;
            n->data.write_stmt.items = NULL;
            n->data.write_stmt.item_count = 0;
            int cap2 = 0;
            while (!at_end_of_statement(p)) {
                ASTNode* item = parse_expression(p);
                if (n->data.write_stmt.item_count >= cap2) {
                    cap2 = cap2 ? cap2 * 2 : 4;
                    n->data.write_stmt.items = realloc(n->data.write_stmt.items,
                        cap2 * sizeof(ASTNode*));
                }
                n->data.write_stmt.items[n->data.write_stmt.item_count++] = item;
                if (!match(p, TOK_COMMA)) break;
            }
            return n;
        }

        case TOK_KW_LET: {
            advance(p); /* consume LET */
            ASTNode* target = parse_lvalue(p);
            expect(p, TOK_EQ);
            ASTNode* expr = parse_expression(p);
            return ast_let(t->line, target, expr);
        }

        case TOK_KW_LOCATE: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_LOCATE;
            n->line = line2;
            n->data.locate.row = NULL;
            n->data.locate.col = NULL;
            if (!at_end_of_statement(p) && !at(p, TOK_COMMA)) {
                n->data.locate.row = parse_expression(p);
            }
            if (match(p, TOK_COMMA)) {
                if (!at_end_of_statement(p)) {
                    n->data.locate.col = parse_expression(p);
                }
            }
            return n;
        }

        case TOK_KW_COLOR: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_COLOR;
            n->line = line2;
            n->data.color.fg = NULL;
            n->data.color.bg = NULL;
            if (!at_end_of_statement(p)) {
                n->data.color.fg = parse_expression(p);
            }
            if (match(p, TOK_COMMA)) {
                n->data.color.bg = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_DATA:
            return parse_data(p);

        case TOK_KW_READ:
            return parse_read(p);

        case TOK_KW_RESTORE: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_RESTORE;
            n->line = line2;
            n->data.restore.label[0] = '\0';
            n->data.restore.lineno = -1;
            if (!at_end_of_statement(p)) {
                if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                    const Token* lt = advance(p);
                    n->data.restore.lineno = (lt->kind == TOK_INTEGER_LIT) ?
                        lt->value.int_val : lt->value.long_val;
                } else if (is_identifier(peek(p)->kind)) {
                    strncpy(n->data.restore.label, peek(p)->value.str.text, 41);
                    advance(p);
                }
            }
            return n;
        }

        case TOK_KW_SWAP: {
            int line2 = t->line;
            advance(p);
            ASTNode* a = parse_lvalue(p);
            expect(p, TOK_COMMA);
            ASTNode* b = parse_lvalue(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SWAP;
            n->line = line2;
            n->data.swap.a = a;
            n->data.swap.b = b;
            return n;
        }

        case TOK_KW_ERASE: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_ERASE;
            n->line = line2;
            int cap2 = 0;
            n->data.erase.names = NULL;
            n->data.erase.name_count = 0;
            do {
                const Token* nt2 = peek(p);
                if (!is_identifier(nt2->kind)) break;
                advance(p);
                if (n->data.erase.name_count >= cap2) {
                    cap2 = cap2 ? cap2 * 2 : 4;
                    n->data.erase.names = realloc(n->data.erase.names, cap2 * 42);
                }
                strncpy(n->data.erase.names[n->data.erase.name_count], nt2->value.str.text, 41);
                n->data.erase.name_count++;
            } while (match(p, TOK_COMMA));
            return n;
        }

        case TOK_KW_OPTION: {
            int line2 = t->line;
            advance(p);
            expect(p, TOK_KW_BASE);
            const Token* b = expect(p, TOK_INTEGER_LIT);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_OPTION_BASE;
            n->line = line2;
            n->data.option_base.base = b->value.int_val;
            return n;
        }

        case TOK_KW_EXIT: {
            int line2 = t->line;
            advance(p);
            int what = 0;
            if (match(p, TOK_KW_FOR)) what = 0;
            else if (match(p, TOK_KW_DO)) what = 1;
            else if (match(p, TOK_KW_WHILE)) what = 2;
            else if (match(p, TOK_KW_SUB)) what = 3;
            else if (match(p, TOK_KW_FUNCTION)) what = 4;
            else if (match(p, TOK_KW_DEF)) what = 5;
            return ast_exit(line2, what);
        }

        case TOK_KW_SUB:
            return parse_sub_def(p);

        case TOK_KW_FUNCTION:
            return parse_function_def(p);

        case TOK_KW_DECLARE:
            return parse_declare(p);

        case TOK_KW_TYPE:
            return parse_type_def(p);

        case TOK_KW_CALL: {
            int line2 = t->line;
            advance(p);
            const Token* nt2 = peek(p);
            if (!is_identifier(nt2->kind)) {
                fb_syntax_error(nt2->line, nt2->col, "Expected SUB name after CALL");
            }
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_CALL;
            n->line = line2;
            strncpy(n->data.call.name, nt2->value.str.text, sizeof(n->data.call.name) - 1);
            n->data.call.args = NULL;
            n->data.call.arg_count = 0;
            int cap2 = 0;
            if (match(p, TOK_LPAREN)) {
                if (!at(p, TOK_RPAREN)) {
                    do {
                        ASTNode* arg = parse_expression(p);
                        if (n->data.call.arg_count >= cap2) {
                            cap2 = cap2 ? cap2 * 2 : 4;
                            n->data.call.args = realloc(n->data.call.args,
                                cap2 * sizeof(ASTNode*));
                        }
                        n->data.call.args[n->data.call.arg_count++] = arg;
                    } while (match(p, TOK_COMMA));
                }
                expect(p, TOK_RPAREN);
            }
            return n;
        }

        case TOK_KW_OPEN:
            return parse_open(p);

        case TOK_KW_CLOSE:
            return parse_close(p);

        case TOK_KW_RESET: {
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_RESET;
            n->line = t->line;
            return n;
        }

        case TOK_KW_ON: {
            int line2 = t->line;
            advance(p);
            /* ON ERROR, ON KEY, ON TIMER, ON expr GOTO/GOSUB */
            if (at(p, TOK_KW_ERROR)) {
                advance(p);
                expect(p, TOK_KW_GOTO);
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = AST_ON_ERROR;
                n->line = line2;
                n->data.on_error.label[0] = '\0';
                n->data.on_error.lineno = -1;
                n->data.on_error.disable = 0;
                if (at(p, TOK_INTEGER_LIT) && peek(p)->value.int_val == 0) {
                    advance(p);
                    n->data.on_error.disable = 1;
                } else if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                    const Token* lt = advance(p);
                    n->data.on_error.lineno = (lt->kind == TOK_INTEGER_LIT) ?
                        lt->value.int_val : lt->value.long_val;
                } else if (is_identifier(peek(p)->kind)) {
                    strncpy(n->data.on_error.label, peek(p)->value.str.text, 41);
                    advance(p);
                }
                return n;
            }
            if (at(p, TOK_KW_KEY)) {
                advance(p);
                expect(p, TOK_LPAREN);
                ASTNode* key_expr = parse_expression(p);
                expect(p, TOK_RPAREN);
                expect(p, TOK_KW_GOSUB);
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = AST_ON_KEY;
                n->line = line2;
                n->data.on_event.key_id = (int)fbval_to_long(&key_expr->data.literal.value);
                ast_free(key_expr);
                if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                    const Token* lt = advance(p);
                    n->data.on_event.lineno = (lt->kind == TOK_INTEGER_LIT) ?
                        lt->value.int_val : lt->value.long_val;
                } else if (is_identifier(peek(p)->kind)) {
                    strncpy(n->data.on_event.label, peek(p)->value.str.text, 41);
                    advance(p);
                }
                return n;
            }
            if (at(p, TOK_KW_TIMER)) {
                advance(p);
                expect(p, TOK_LPAREN);
                ASTNode* interval = parse_expression(p);
                expect(p, TOK_RPAREN);
                expect(p, TOK_KW_GOSUB);
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = AST_ON_TIMER;
                n->line = line2;
                n->data.on_event.interval = fbval_to_double(&interval->data.literal.value);
                ast_free(interval);
                if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                    const Token* lt = advance(p);
                    n->data.on_event.lineno = (lt->kind == TOK_INTEGER_LIT) ?
                        lt->value.int_val : lt->value.long_val;
                } else if (is_identifier(peek(p)->kind)) {
                    strncpy(n->data.on_event.label, peek(p)->value.str.text, 41);
                    advance(p);
                }
                return n;
            }
            /* ON expr GOTO/GOSUB label1, label2, ... */
            ASTNode* expr = parse_expression(p);
            int is_goto = 0;
            if (match(p, TOK_KW_GOTO)) is_goto = 1;
            else expect(p, TOK_KW_GOSUB);

            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = is_goto ? AST_ON_GOTO : AST_ON_GOSUB;
            n->line = line2;
            n->data.on_branch.expr = expr;
            int lcap = 0;
            n->data.on_branch.labels = NULL;
            n->data.on_branch.linenos = NULL;
            n->data.on_branch.label_count = 0;
            do {
                if (n->data.on_branch.label_count >= lcap) {
                    lcap = lcap ? lcap * 2 : 4;
                    n->data.on_branch.labels = realloc(n->data.on_branch.labels, lcap * 42);
                    n->data.on_branch.linenos = realloc(n->data.on_branch.linenos, lcap * sizeof(int));
                }
                int li = n->data.on_branch.label_count++;
                n->data.on_branch.linenos[li] = -1;
                n->data.on_branch.labels[li][0] = '\0';

                if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                    const Token* lt = advance(p);
                    n->data.on_branch.linenos[li] = (lt->kind == TOK_INTEGER_LIT) ?
                        lt->value.int_val : lt->value.long_val;
                } else if (is_identifier(peek(p)->kind)) {
                    strncpy(n->data.on_branch.labels[li], peek(p)->value.str.text, 41);
                    advance(p);
                }
            } while (match(p, TOK_COMMA));
            return n;
        }

        case TOK_KW_RESUME: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_RESUME;
            n->line = line2;
            n->data.resume.resume_type = 0;
            n->data.resume.label[0] = '\0';
            n->data.resume.lineno = -1;
            if (at(p, TOK_KW_NEXT)) { advance(p); n->data.resume.resume_type = 1; }
            else if (at(p, TOK_INTEGER_LIT) || at(p, TOK_LONG_LIT)) {
                const Token* lt = advance(p);
                n->data.resume.resume_type = 2;
                n->data.resume.lineno = (lt->kind == TOK_INTEGER_LIT) ?
                    lt->value.int_val : lt->value.long_val;
            } else if (is_identifier(peek(p)->kind)) {
                n->data.resume.resume_type = 2;
                strncpy(n->data.resume.label, peek(p)->value.str.text, 41);
                advance(p);
            }
            return n;
        }

        case TOK_KW_ERROR: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_ERROR_STMT;
            n->line = line2;
            n->data.error_stmt.code = parse_expression(p);
            return n;
        }

        case TOK_KW_TRON:
            advance(p);
            return ast_alloc_helper(AST_TRON, t->line);

        case TOK_KW_TROFF:
            advance(p);
            return ast_alloc_helper(AST_TROFF, t->line);

        case TOK_KW_SHELL: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SHELL;
            n->line = line2;
            n->data.single_arg.arg = NULL;
            if (!at_end_of_statement(p)) {
                n->data.single_arg.arg = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_CHDIR: case TOK_KW_MKDIR: case TOK_KW_RMDIR: case TOK_KW_KILL: {
            int line2 = t->line;
            ASTKind kind2;
            switch (t->kind) {
                case TOK_KW_CHDIR: kind2 = AST_CHDIR; break;
                case TOK_KW_MKDIR: kind2 = AST_MKDIR; break;
                case TOK_KW_RMDIR: kind2 = AST_RMDIR; break;
                case TOK_KW_KILL:  kind2 = AST_KILL_STMT; break;
                default: kind2 = AST_REM; break;
            }
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = kind2;
            n->line = line2;
            n->data.single_arg.arg = parse_expression(p);
            return n;
        }

        case TOK_KW_SLEEP: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SLEEP;
            n->line = line2;
            n->data.sleep_stmt.seconds = NULL;
            if (!at_end_of_statement(p)) {
                n->data.sleep_stmt.seconds = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_RANDOMIZE: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_RANDOMIZE;
            n->line = line2;
            n->data.randomize.seed = NULL;
            n->data.randomize.use_timer = 0;
            if (at(p, TOK_KW_TIMER)) {
                advance(p);
                n->data.randomize.use_timer = 1;
            } else if (!at_end_of_statement(p)) {
                n->data.randomize.seed = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_SOUND: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SOUND;
            n->line = line2;
            n->data.sound.freq = parse_expression(p);
            expect(p, TOK_COMMA);
            n->data.sound.duration = parse_expression(p);
            return n;
        }

        case TOK_KW_PLAY: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_PLAY;
            n->line = line2;
            n->data.draw_play.cmd_string = parse_expression(p);
            return n;
        }

        case TOK_KW_DRAW: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_DRAW;
            n->line = line2;
            n->data.draw_play.cmd_string = parse_expression(p);
            return n;
        }

        case TOK_KW_SCREEN: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SCREEN;
            n->line = line2;
            n->data.screen.mode = parse_expression(p);
            n->data.screen.colorswitch = NULL;
            n->data.screen.apage = NULL;
            n->data.screen.vpage = NULL;
            if (match(p, TOK_COMMA)) {
                if (!at(p, TOK_COMMA) && !at_end_of_statement(p))
                    n->data.screen.colorswitch = parse_expression(p);
                if (match(p, TOK_COMMA)) {
                    if (!at(p, TOK_COMMA) && !at_end_of_statement(p))
                        n->data.screen.apage = parse_expression(p);
                    if (match(p, TOK_COMMA)) {
                        n->data.screen.vpage = parse_expression(p);
                    }
                }
            }
            return n;
        }

        case TOK_KW_PALETTE: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_PALETTE;
            n->line = line2;
            n->data.palette.attr = NULL;
            n->data.palette.color = NULL;
            if (!at_end_of_statement(p)) {
                n->data.palette.attr = parse_expression(p);
                expect(p, TOK_COMMA);
                n->data.palette.color = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_VIEW: {
            int line2 = t->line;
            advance(p);
            if (at(p, TOK_KW_PRINT)) {
                advance(p);
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = AST_VIEW_PRINT;
                n->line = line2;
                n->data.view_print.top = NULL;
                n->data.view_print.bottom = NULL;
                if (!at_end_of_statement(p)) {
                    n->data.view_print.top = parse_expression(p);
                    expect(p, TOK_KW_TO);
                    n->data.view_print.bottom = parse_expression(p);
                }
                return n;
            }
            /* VIEW (graphics) — skip for now */
            while (!at_end_of_statement(p)) advance(p);
            return ast_rem(line2);
        }

        case TOK_KW_WIDTH: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_WIDTH_STMT;
            n->line = line2;
            n->data.width_stmt.cols = NULL;
            n->data.width_stmt.rows = NULL;
            if (!at_end_of_statement(p)) {
                n->data.width_stmt.cols = parse_expression(p);
                if (match(p, TOK_COMMA)) {
                    n->data.width_stmt.rows = parse_expression(p);
                }
            }
            return n;
        }

        case TOK_KW_SHARED: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SHARED_STMT;
            n->line = line2;
            int cap2 = 0;
            n->data.shared_stmt.names = NULL;
            n->data.shared_stmt.types = NULL;
            n->data.shared_stmt.name_count = 0;
            do {
                const Token* nt2 = peek(p);
                if (!is_identifier(nt2->kind)) break;
                advance(p);
                FBType vtype = type_from_ident_token(nt2->kind);
                char dummy[42];
                FBType at_type = parse_as_type(p, dummy);
                if ((int)at_type != -1) vtype = at_type;

                int nc = n->data.shared_stmt.name_count;
                if (nc >= cap2) {
                    cap2 = cap2 ? cap2 * 2 : 4;
                    n->data.shared_stmt.names = realloc(n->data.shared_stmt.names, cap2 * 42);
                    n->data.shared_stmt.types = realloc(n->data.shared_stmt.types, cap2 * sizeof(FBType));
                }
                strncpy(n->data.shared_stmt.names[nc], nt2->value.str.text, 41);
                n->data.shared_stmt.types[nc] = vtype;
                n->data.shared_stmt.name_count++;
            } while (match(p, TOK_COMMA));
            return n;
        }

        case TOK_KW_STATIC: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_STATIC_STMT;
            n->line = line2;
            int cap2 = 0;
            n->data.shared_stmt.names = NULL;
            n->data.shared_stmt.types = NULL;
            n->data.shared_stmt.name_count = 0;
            do {
                const Token* nt2 = peek(p);
                if (!is_identifier(nt2->kind)) break;
                advance(p);
                FBType vtype = type_from_ident_token(nt2->kind);
                char dummy[42];
                FBType at_type = parse_as_type(p, dummy);
                if ((int)at_type != -1) vtype = at_type;

                int nc = n->data.shared_stmt.name_count;
                if (nc >= cap2) {
                    cap2 = cap2 ? cap2 * 2 : 4;
                    n->data.shared_stmt.names = realloc(n->data.shared_stmt.names, cap2 * 42);
                    n->data.shared_stmt.types = realloc(n->data.shared_stmt.types, cap2 * sizeof(FBType));
                }
                strncpy(n->data.shared_stmt.names[nc], nt2->value.str.text, 41);
                n->data.shared_stmt.types[nc] = vtype;
                n->data.shared_stmt.name_count++;
            } while (match(p, TOK_COMMA));
            return n;
        }

        case TOK_KW_CLEAR:
            advance(p);
            return calloc_node(AST_CLEAR, t->line);

        case TOK_KW_POKE: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_POKE;
            n->line = line2;
            n->data.poke_out.addr = parse_expression(p);
            expect(p, TOK_COMMA);
            n->data.poke_out.val = parse_expression(p);
            return n;
        }

        case TOK_KW_DEF: {
            int line2 = t->line;
            advance(p);
            /* DEF FN or DEF SEG */
            if (at(p, TOK_KW_SEG)) {
                advance(p);
                /* DEF SEG — skip (DOS only) */
                if (match(p, TOK_EQ)) parse_expression(p);
                return ast_rem(line2);
            }
            /* DEF FN... */
            if (at(p, TOK_KW_FN) || (is_identifier(peek(p)->kind) &&
                strncasecmp(peek(p)->value.str.text, "FN", 2) == 0)) {
                if (at(p, TOK_KW_FN)) advance(p);
                const Token* fname = peek(p);
                if (!is_identifier(fname->kind)) {
                    fb_syntax_error(fname->line, fname->col, "Expected function name after DEF FN");
                }
                advance(p);
                /* Parse DEF FN as a simple function */
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = AST_DEF_FN;
                n->line = line2;
                strncpy(n->data.def_fn.name, fname->value.str.text, 41);
                n->data.def_fn.params = NULL;
                n->data.def_fn.param_count = 0;
                n->data.def_fn.body_expr = NULL;
                n->data.def_fn.body = NULL;
                n->data.def_fn.body_count = 0;

                if (match(p, TOK_LPAREN)) {
                    int pcap = 0;
                    if (!at(p, TOK_RPAREN)) {
                        do {
                            const Token* pt = peek(p);
                            if (!is_identifier(pt->kind)) break;
                            advance(p);
                            if (n->data.def_fn.param_count >= pcap) {
                                pcap = pcap ? pcap * 2 : 4;
                                n->data.def_fn.params = realloc(n->data.def_fn.params,
                                    pcap * sizeof(n->data.def_fn.params[0]));
                            }
                            int pi = n->data.def_fn.param_count++;
                            strncpy(n->data.def_fn.params[pi].pname, pt->value.str.text, 41);
                            n->data.def_fn.params[pi].ptype = type_from_ident_token(pt->kind);
                        } while (match(p, TOK_COMMA));
                    }
                    expect(p, TOK_RPAREN);
                }

                expect(p, TOK_EQ);
                n->data.def_fn.body_expr = parse_expression(p);
                return n;
            }
            /* Unknown DEF — skip line */
            while (!at_end_of_statement(p)) advance(p);
            return ast_rem(line2);
        }

        case TOK_KW_COMMON: {
            int line2 = t->line;
            advance(p);
            int is_shared = 0;
            if (match(p, TOK_KW_SHARED)) is_shared = 1;
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_COMMON;
            n->line = line2;
            n->data.common.is_shared = is_shared;
            int cap2 = 0;
            n->data.common.names = NULL;
            n->data.common.types = NULL;
            n->data.common.is_array = NULL;
            n->data.common.name_count = 0;
            do {
                const Token* nt2 = peek(p);
                if (!is_identifier(nt2->kind)) break;
                advance(p);
                int nc = n->data.common.name_count;
                if (nc >= cap2) {
                    cap2 = cap2 ? cap2 * 2 : 4;
                    n->data.common.names = realloc(n->data.common.names, cap2 * 42);
                    n->data.common.types = realloc(n->data.common.types, cap2 * sizeof(FBType));
                    n->data.common.is_array = realloc(n->data.common.is_array, cap2 * sizeof(int));
                }
                strncpy(n->data.common.names[nc], nt2->value.str.text, 41);
                n->data.common.types[nc] = type_from_ident_token(nt2->kind);
                n->data.common.is_array[nc] = 0;
                if (at(p, TOK_LPAREN)) { advance(p); expect(p, TOK_RPAREN); n->data.common.is_array[nc] = 1; }
                char dummy[42];
                FBType at_type = parse_as_type(p, dummy);
                if ((int)at_type != -1) n->data.common.types[nc] = at_type;
                n->data.common.name_count++;
            } while (match(p, TOK_COMMA));
            return n;
        }

        case TOK_KW_ENVIRON: {
            int line2 = t->line;
            advance(p);
            /* If used as statement: ENVIRON expr */
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_ENVIRON_STMT;
            n->line = line2;
            n->data.environ_stmt.expr = parse_expression(p);
            return n;
        }

        case TOK_KW_NAME: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_NAME_STMT;
            n->line = line2;
            n->data.name_stmt.old_name = parse_expression(p);
            expect(p, TOK_KW_AS);
            n->data.name_stmt.new_name = parse_expression(p);
            return n;
        }

        case TOK_KW_FILES: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_FILES_STMT;
            n->line = line2;
            n->data.files.pattern = NULL;
            if (!at_end_of_statement(p)) {
                n->data.files.pattern = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_KEY: {
            int line2 = t->line;
            advance(p);
            /* KEY(n) ON/OFF/STOP */
            if (at(p, TOK_LPAREN)) {
                advance(p);
                ASTNode* ke = parse_expression(p);
                expect(p, TOK_RPAREN);
                int action = 0;
                if (match(p, TOK_KW_ON)) action = 1;
                else if (match(p, TOK_KW_OFF)) action = 0;
                else if (match(p, TOK_KW_STOP)) action = 2;
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = AST_KEY_CTRL;
                n->line = line2;
                n->data.event_ctrl.key_id = (int)fbval_to_long(&ke->data.literal.value);
                n->data.event_ctrl.action = action;
                ast_free(ke);
                return n;
            }
            /* KEY LIST, KEY n, etc. — skip */
            while (!at_end_of_statement(p)) advance(p);
            return ast_rem(line2);
        }

        case TOK_KW_TIMER: {
            /* TIMER ON/OFF/STOP */
            int line2 = t->line;
            advance(p);
            int action = 0;
            if (match(p, TOK_KW_ON)) action = 1;
            else if (match(p, TOK_KW_OFF)) action = 0;
            else if (match(p, TOK_KW_STOP)) action = 2;
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_TIMER_CTRL;
            n->line = line2;
            n->data.event_ctrl.key_id = 0;
            n->data.event_ctrl.action = action;
            return n;
        }

        case TOK_KW_LSET: case TOK_KW_RSET: {
            int line2 = t->line;
            ASTKind kind2 = (t->kind == TOK_KW_LSET) ? AST_LSET : AST_RSET;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = kind2;
            n->line = line2;
            n->data.lrset.target = parse_lvalue(p);
            expect(p, TOK_EQ);
            n->data.lrset.expr = parse_expression(p);
            return n;
        }

        case TOK_KW_FIELD: {
            int line2 = t->line;
            advance(p);
            match(p, TOK_HASH);
            ASTNode* fnum_expr = parse_expression(p);
            expect(p, TOK_COMMA);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_FIELD_STMT;
            n->line = line2;
            n->data.field.filenum = fnum_expr;
            int cap2 = 0;
            n->data.field.fields = NULL;
            n->data.field.field_count = 0;
            do {
                int width = 0;
                ASTNode* we = parse_expression(p);
                if (we->kind == AST_LITERAL) width = fbval_to_long(&we->data.literal.value);
                ast_free(we);
                expect(p, TOK_KW_AS);
                const Token* vn = peek(p);
                if (!is_identifier(vn->kind)) break;
                advance(p);
                if (n->data.field.field_count >= cap2) {
                    cap2 = cap2 ? cap2 * 2 : 4;
                    n->data.field.fields = realloc(n->data.field.fields,
                        cap2 * sizeof(n->data.field.fields[0]));
                }
                int fi = n->data.field.field_count++;
                n->data.field.fields[fi].width = width;
                strncpy(n->data.field.fields[fi].var_name, vn->value.str.text, 41);
            } while (match(p, TOK_COMMA));
            return n;
        }

        case TOK_KW_GET: case TOK_KW_PUT: {
            int line2 = t->line;
            int is_put = (t->kind == TOK_KW_PUT);
            advance(p);
            /* Could be file GET/PUT or graphics GET/PUT */
            /* File: GET #n, recnum, var */
            if (at(p, TOK_HASH)) {
                advance(p);
                ASTNode* n = calloc(1, sizeof(ASTNode));
                n->kind = is_put ? AST_PUT_FILE : AST_GET_FILE;
                n->line = line2;
                n->data.file_io.filenum = parse_expression(p);
                n->data.file_io.recnum = NULL;
                n->data.file_io.var = NULL;
                if (match(p, TOK_COMMA)) {
                    if (!at(p, TOK_COMMA) && !at_end_of_statement(p))
                        n->data.file_io.recnum = parse_expression(p);
                    if (match(p, TOK_COMMA)) {
                        n->data.file_io.var = parse_lvalue(p);
                    }
                }
                return n;
            }
            /* Graphics GET/PUT — skip for now */
            while (!at_end_of_statement(p)) advance(p);
            return ast_rem(line2);
        }

        case TOK_KW_SEEK: {
            int line2 = t->line;
            advance(p);
            match(p, TOK_HASH);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_SEEK_STMT;
            n->line = line2;
            n->data.seek.filenum = parse_expression(p);
            expect(p, TOK_COMMA);
            n->data.seek.position = parse_expression(p);
            return n;
        }

        case TOK_KW_LOCK: case TOK_KW_UNLOCK: {
            int line2 = t->line;
            ASTKind kind2 = (t->kind == TOK_KW_LOCK) ? AST_LOCK_STMT : AST_UNLOCK_STMT;
            advance(p);
            match(p, TOK_HASH);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = kind2;
            n->line = line2;
            n->data.lock.filenum = parse_expression(p);
            n->data.lock.start_rec = NULL;
            n->data.lock.end_rec = NULL;
            if (match(p, TOK_COMMA)) {
                n->data.lock.start_rec = parse_expression(p);
                if (match(p, TOK_KW_TO)) {
                    n->data.lock.end_rec = parse_expression(p);
                }
            }
            return n;
        }

        case TOK_KW_RUN: {
            int line2 = t->line;
            advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_RUN;
            n->line = line2;
            n->data.run.arg = NULL;
            if (!at_end_of_statement(p)) {
                n->data.run.arg = parse_expression(p);
            }
            return n;
        }

        case TOK_KW_CHAIN: {
            int line2 = t->line;
            advance(p);
            int merge = 0;
            if (match(p, TOK_KW_MERGE) || 0) merge = 1;
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_CHAIN;
            n->line = line2;
            n->data.chain.filename = parse_expression(p);
            n->data.chain.start_line = NULL;
            n->data.chain.merge = merge;
            n->data.chain.all = 0;
            if (match(p, TOK_COMMA)) {
                if (!at_end_of_statement(p))
                    n->data.chain.start_line = parse_expression(p);
                if (match(p, TOK_COMMA)) {
                    if (match(p, TOK_KW_ALL)) n->data.chain.all = 1;
                }
            }
            return n;
        }

        default:
            break;
    }

    /* If it's an identifier, it's either an assignment or a SUB call */
    if (is_identifier(t->kind)) {
        /* Look ahead: if next is = (and not ==), it's an assignment */
        /* Also check for array element assignment: ident(expr) = expr */
        /* Try to parse as lvalue first, then check for = */

        int save_pos = p->pos;
        ASTNode* target = parse_lvalue(p);

        if (at(p, TOK_EQ)) {
            /* Assignment */
            advance(p);
            ASTNode* expr = parse_expression(p);
            return ast_let(t->line, target, expr);
        } else {
            /* SUB call without CALL keyword: subname arg1, arg2 */
            /* Rewind and parse as a call */
            ast_free(target);
            p->pos = save_pos;

            const Token* sub_name = advance(p);
            ASTNode* n = calloc(1, sizeof(ASTNode));
            n->kind = AST_CALL;
            n->line = t->line;
            strncpy(n->data.call.name, sub_name->value.str.text,
                    sizeof(n->data.call.name) - 1);
            n->data.call.args = NULL;
            n->data.call.arg_count = 0;

            if (!at_end_of_statement(p) && !at(p, TOK_KW_ELSE)) {
                int cap2 = 0;
                do {
                    ASTNode* arg = parse_expression(p);
                    if (n->data.call.arg_count >= cap2) {
                        cap2 = cap2 ? cap2 * 2 : 4;
                        n->data.call.args = realloc(n->data.call.args,
                            cap2 * sizeof(ASTNode*));
                    }
                    n->data.call.args[n->data.call.arg_count++] = arg;
                } while (match(p, TOK_COMMA));
            }
            return n;
        }
    }

    /* Unknown — skip token */
    advance(p);
    return NULL;
}

/* Helper to allocate a simple node */
static ASTNode* ast_alloc_helper(ASTKind kind, int line) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->kind = kind;
    n->line = line;
    return n;
}

static ASTNode* calloc_node(ASTKind kind, int line) {
    return ast_alloc_helper(kind, line);
}

/* --- Public API --- */

int parser_parse(const Token* tokens, int token_count, Program* prog) {
    Parser p;
    p.tokens = tokens;
    p.token_count = token_count;
    p.pos = 0;
    p.prog = prog;

    /* Initialize program */
    memset(prog, 0, sizeof(Program));

    /* First pass: collect DATA statements into data pool */
    /* We'll do this inline during parsing */

    while (!at(&p, TOK_EOF)) {
        ASTNode* stmt = parse_statement(&p);
        if (stmt) {
            /* If it's a DATA node, also add values to the data pool */
            if (stmt->kind == AST_DATA) {
                for (int i = 0; i < stmt->data.data.value_count; i++) {
                    if (prog->data_count >= prog->data_cap) {
                        prog->data_cap = prog->data_cap ? prog->data_cap * 2 : 32;
                        prog->data_pool = realloc(prog->data_pool,
                            prog->data_cap * sizeof(FBValue));
                    }
                    prog->data_pool[prog->data_count++] = fbval_copy(&stmt->data.data.values[i]);
                }
            }

            /* If it's a SUB_DEF or FUNCTION_DEF, register in procedure table */
            if (stmt->kind == AST_SUB_DEF || stmt->kind == AST_FUNCTION_DEF) {
                if (prog->proc_count >= prog->proc_cap) {
                    prog->proc_cap = prog->proc_cap ? prog->proc_cap * 2 : 8;
                    prog->procedures = realloc(prog->procedures,
                        prog->proc_cap * sizeof(prog->procedures[0]));
                }
                int pi = prog->proc_count++;
                strncpy(prog->procedures[pi].name, stmt->data.proc_def.name, 41);
                prog->procedures[pi].is_function = (stmt->kind == AST_FUNCTION_DEF);
                prog->procedures[pi].stmt_index = prog->stmt_count;
            }

            if (prog->stmt_count >= prog->stmt_cap) {
                prog->stmt_cap = prog->stmt_cap ? prog->stmt_cap * 2 : 64;
                prog->statements = realloc(prog->statements,
                    prog->stmt_cap * sizeof(ASTNode*));
            }
            prog->statements[prog->stmt_count++] = stmt;
        }

        if (at(&p, TOK_COLON)) advance(&p);
        else if (at(&p, TOK_EOL)) skip_eol(&p);
    }

    return 0;
}

void program_free(Program* prog) {
    for (int i = 0; i < prog->stmt_count; i++)
        ast_free(prog->statements[i]);
    free(prog->statements);
    free(prog->labels);
    free(prog->line_map);
    for (int i = 0; i < prog->data_count; i++)
        fbval_release(&prog->data_pool[i]);
    free(prog->data_pool);
    free(prog->data_labels);
    free(prog->procedures);
}

int program_find_label(const Program* prog, const char* name) {
    for (int i = 0; i < prog->label_count; i++) {
        if (strcasecmp(prog->labels[i].name, name) == 0)
            return prog->labels[i].stmt_index;
    }
    /* Also search line_map as string */
    for (int i = 0; i < prog->linemap_count; i++) {
        char buf[42];
        snprintf(buf, sizeof(buf), "%d", prog->line_map[i].lineno);
        if (strcmp(buf, name) == 0)
            return prog->line_map[i].stmt_index;
    }
    return -1;
}

int program_find_lineno(const Program* prog, int32_t lineno) {
    for (int i = 0; i < prog->linemap_count; i++) {
        if (prog->line_map[i].lineno == lineno)
            return prog->line_map[i].stmt_index;
    }
    return -1;
}

int program_find_proc(const Program* prog, const char* name) {
    for (int i = 0; i < prog->proc_count; i++) {
        if (strcasecmp(prog->procedures[i].name, name) == 0)
            return prog->procedures[i].stmt_index;
    }
    return -1;
}
