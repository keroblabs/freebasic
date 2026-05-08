/*
 * interpreter.c — Tree-walking interpreter for FBasic AST
 * Phases 0-2: core execution, I/O, strings, math
 */
#include "interpreter.h"
#include "coerce.h"
#include "error.h"
#include "ast.h"
#include "value.h"
#include "symtable.h"
#include "parser.h"
#include "builtins_math.h"
#include "builtins_str.h"
#include "builtins_convert.h"
#include "console.h"
#include "print_using.h"
#include "array.h"
#include "udt.h"
#include "callframe.h"
#include "system_api.h"
#include "system_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <time.h>
#include <setjmp.h>

/* Forward declarations */
static FBValue eval_expr(Interpreter* interp, ASTNode* expr);
static void exec_statement(Interpreter* interp, ASTNode* stmt);
static void exec_block(Interpreter* interp, ASTNode** stmts, int count);
static FBValue* resolve_lvalue(Interpreter* interp, ASTNode* node);

/* ---- Error handling via setjmp/longjmp ---- */

static jmp_buf interp_error_jmp;
static int     interp_has_jmp = 0;
static int     interp_error_line = 0;   /* saved across longjmp */

/* Reverse lookup: given statement index, find the BASIC line number (ERL).
   Returns the BASIC line number if the statement (or its source line) has one,
   otherwise 0. */
static int find_basic_lineno(const Program* prog, int stmt_index) {
    if (stmt_index < 0 || stmt_index >= prog->stmt_count) return 0;
    int src_line = prog->statements[stmt_index]->line;
    for (int i = 0; i < prog->linemap_count; i++) {
        if (prog->line_map[i].stmt_index == stmt_index)
            return prog->line_map[i].lineno;
        /* Multi-statement lines: check if same source line */
        if (prog->statements[prog->line_map[i].stmt_index]->line == src_line)
            return prog->line_map[i].lineno;
    }
    return 0;
}

static void interp_error_handler(FBErrorCode code, int line, const char* extra) {
    /* Global error state already set by fb_error before calling handler */
    (void)extra;
    if (interp_has_jmp) {
        interp_error_line = line;
        longjmp(interp_error_jmp, code);
    }
    /* Fallback: print and exit */
    fprintf(stderr, "Runtime error %d at line %d: %s",
            code, line, fb_error_message(code));
    if (extra) fprintf(stderr, " (%s)", extra);
    fprintf(stderr, "\n");
    exit(1);
}

/* ---- Helper: check if ident name has a type suffix ---- */

static int has_type_suffix(const char* name) {
    if (!name || !name[0]) return 0;
    int len = (int)strlen(name);
    char last = name[len - 1];
    return last == '$' || last == '%' || last == '&' || last == '!' || last == '#';
}

/* ---- Helper: determine variable type from AST_VARIABLE node ---- */

static FBType resolve_var_type(Interpreter* interp, ASTNode* var_node) {
    FBType hint = var_node->data.variable.type_hint;
    if ((int)hint != -1) return hint;
    /* No suffix specified, use deftype */
    return scope_default_type(interp->current_scope,
                              var_node->data.variable.name);
}

/* ---- Helper: resolve or auto-create a variable ---- */

static Symbol* resolve_or_create_var(Interpreter* interp, ASTNode* var_node) {
    const char* name = var_node->data.variable.name;
    Symbol* sym = scope_lookup(interp->current_scope, name);
    if (!sym) {
        FBType type = resolve_var_type(interp, var_node);
        sym = scope_insert(interp->current_scope, name, SYM_VARIABLE, type);
        if (!sym) {
            /* duplicate? try lookup again */
            sym = scope_lookup(interp->current_scope, name);
        }
    }
    return sym;
}

/* ---- Helper: Assign a value to a variable node ---- */

static void exec_assign_value(Interpreter* interp, ASTNode* target, FBValue val) {
    if (target->kind == AST_VARIABLE) {
        Symbol* sym = resolve_or_create_var(interp, target);
        if (sym->kind == SYM_CONST) {
            fb_error(FB_ERR_DUPLICATE_DEFINITION, target->line,
                     "Cannot assign to CONST");
            fbval_release(&val);
            return;
        }
        if (sym->is_ref && sym->ref_addr) {
            FBValue coerced = fbval_coerce(&val, sym->type);
            fbval_release(&val);
            fbval_release(sym->ref_addr);
            *sym->ref_addr = coerced;
            fbval_release(&sym->value);
            sym->value = fbval_copy(sym->ref_addr);
            return;
        }
        FBValue coerced = fbval_coerce(&val, sym->type);
        fbval_release(&val);
        fbval_release(&sym->value);
        sym->value = coerced;
    } else if (target->kind == AST_ARRAY_ACCESS) {
        FBValue* elem = resolve_lvalue(interp, target);
        if (!elem) {
            fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, target->line, NULL);
            fbval_release(&val);
            return;
        }
        FBValue coerced = fbval_coerce(&val, elem->type);
        fbval_release(&val);
        fbval_release(elem);
        *elem = coerced;
    } else if (target->kind == AST_UDT_MEMBER) {
        FBValue* field_ptr = resolve_lvalue(interp, target);
        if (!field_ptr) {
            fb_error(FB_ERR_TYPE_MISMATCH, target->line, "Invalid UDT field");
            fbval_release(&val);
            return;
        }
        if (field_ptr->type == FB_STRING && val.type == FB_STRING) {
            /* For fixed-length strings in UDTs, truncate/pad */
            fbval_release(field_ptr);
            *field_ptr = val;
        } else {
            FBValue coerced = fbval_coerce(&val, field_ptr->type);
            fbval_release(&val);
            fbval_release(field_ptr);
            *field_ptr = coerced;
        }
    } else {
        fb_error(FB_ERR_SYNTAX, target->line, "Invalid assignment target");
        fbval_release(&val);
    }
}

/* ---- Helper: resolve an lvalue to a FBValue pointer ---- */

static FBValue* resolve_lvalue(Interpreter* interp, ASTNode* node) {
    if (node->kind == AST_VARIABLE) {
        Symbol* sym = resolve_or_create_var(interp, node);
        if (sym->is_ref && sym->ref_addr) return sym->ref_addr;
        return &sym->value;
    } else if (node->kind == AST_ARRAY_ACCESS) {
        const char* aname = node->data.array_access.name;
        int nsubs = node->data.array_access.nsubs;

        Symbol* sym = scope_lookup(interp->current_scope, aname);

        /* Auto-dim if not yet declared */
        if (!sym || sym->kind != SYM_ARRAY) {
            /* Check if it might be a function — don't auto-dim in that case */
            if (sym && sym->kind != SYM_ARRAY) return NULL;
            FBType type = scope_default_type(interp->current_scope, aname);
            ArrayDim dims[FB_MAX_DIMENSIONS];
            for (int d = 0; d < nsubs; d++) {
                dims[d].lower = interp->option_base;
                dims[d].upper = 10;
            }
            FBArray* arr = fbarray_new(type, nsubs, dims, 0, -1);
            sym = scope_insert(interp->current_scope, aname, SYM_ARRAY, type);
            sym->array = arr;
        }

        if (!sym->array) return NULL;

        int subs[FB_MAX_DIMENSIONS];
        for (int i = 0; i < nsubs; i++) {
            FBValue v = eval_expr(interp, node->data.array_access.subscripts[i]);
            subs[i] = (int)fbval_to_long(&v);
            fbval_release(&v);
        }
        return fbarray_get(sym->array, subs, nsubs);
    } else if (node->kind == AST_UDT_MEMBER) {
        FBValue* base_ptr = resolve_lvalue(interp, node->data.udt_member.base);
        if (!base_ptr || base_ptr->type != FB_UDT) return NULL;
        int type_id = base_ptr->as.udt.type_id;
        int field_idx = udt_find_field(&interp->udt_registry, type_id,
                                       node->data.udt_member.field);
        if (field_idx < 0) return NULL;
        return &base_ptr->as.udt.fields[field_idx];
    }
    return NULL;
}

/* ---- Helper: Get variable value ---- */

static FBValue get_variable_value(Interpreter* interp, ASTNode* var_node) {
    return eval_expr(interp, var_node);
}

/* ---- Helper: resolve jump target for GOTO/GOSUB ---- */

static int resolve_jump_target(Interpreter* interp, ASTNode* node) {
    if (node->data.jump.lineno >= 0) {
        int idx = program_find_lineno(interp->prog, node->data.jump.lineno);
        if (idx < 0) {
            fb_error(FB_ERR_UNDEFINED_LABEL, node->line, "Undefined line number");
            return -1;
        }
        return idx;
    } else if (node->data.jump.label[0]) {
        int idx = program_find_label(interp->prog, node->data.jump.label);
        if (idx < 0) {
            fb_error(FB_ERR_UNDEFINED_LABEL, node->line, "Undefined label");
            return -1;
        }
        return idx;
    }
    return -1;
}

/* ======== Expression evaluator ======== */

static FBValue eval_builtin_func(Interpreter* interp, ASTNode* expr);

static FBValue eval_expr(Interpreter* interp, ASTNode* expr) {
    if (!expr) return fbval_int(0);

    switch (expr->kind) {
        case AST_LITERAL:
            return fbval_copy(&expr->data.literal.value);

        case AST_VARIABLE: {
            Symbol* sym = scope_lookup(interp->current_scope,
                                        expr->data.variable.name);
            if (!sym) {
                FBType type = resolve_var_type(interp, expr);
                sym = scope_insert(interp->current_scope,
                                   expr->data.variable.name,
                                   SYM_VARIABLE, type);
            }
            return fbval_copy(&sym->value);
        }

        case AST_BINARY_OP: {
            FBValue left = eval_expr(interp, expr->data.binop.left);
            FBValue right = eval_expr(interp, expr->data.binop.right);
            FBValue result;
            TokenKind op = expr->data.binop.op;

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
            FBValue operand = eval_expr(interp, expr->data.unop.operand);
            FBValue result = fbval_unary_op(&operand, expr->data.unop.op);
            fbval_release(&operand);
            return result;
        }

        case AST_PAREN:
            return eval_expr(interp, expr->data.paren.expr);

        case AST_FUNC_CALL:
            return eval_builtin_func(interp, expr);

        case AST_ARRAY_ACCESS: {
            /* Check if this is actually a function call (parser can't distinguish) */
            const char* aname = expr->data.array_access.name;
            int nsubs = expr->data.array_access.nsubs;

            /* Check builtin math/string/convert functions */
            if (builtin_math_lookup(aname)) {
                FBValue argv[16];
                int argc = nsubs < 16 ? nsubs : 16;
                for (int i = 0; i < argc; i++)
                    argv[i] = eval_expr(interp, expr->data.array_access.subscripts[i]);
                FBValue r = builtin_math_call(aname, argv, argc, expr->line, interp);
                for (int i = 0; i < argc; i++) fbval_release(&argv[i]);
                return r;
            }
            if (builtin_str_lookup(aname)) {
                FBValue argv[16];
                int argc = nsubs < 16 ? nsubs : 16;
                for (int i = 0; i < argc; i++)
                    argv[i] = eval_expr(interp, expr->data.array_access.subscripts[i]);
                FBValue r = builtin_str_call(aname, argv, argc, expr->line);
                for (int i = 0; i < argc; i++) fbval_release(&argv[i]);
                return r;
            }
            if (builtin_convert_lookup(aname)) {
                FBValue argv[16];
                int argc = nsubs < 16 ? nsubs : 16;
                for (int i = 0; i < argc; i++)
                    argv[i] = eval_expr(interp, expr->data.array_access.subscripts[i]);
                FBValue r = builtin_convert_call(aname, argv, argc, expr->line);
                for (int i = 0; i < argc; i++) fbval_release(&argv[i]);
                return r;
            }

            /* Check if it's a declared array */
            Symbol* sym = scope_lookup(interp->current_scope, aname);
            if (sym && sym->kind == SYM_ARRAY && sym->array) {
                int subs[FB_MAX_DIMENSIONS];
                for (int i = 0; i < nsubs; i++) {
                    FBValue v = eval_expr(interp, expr->data.array_access.subscripts[i]);
                    subs[i] = (int)fbval_to_long(&v);
                    fbval_release(&v);
                }
                FBValue* elem = fbarray_get(sym->array, subs, nsubs);
                if (!elem) {
                    fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, expr->line, NULL);
                    return fbval_int(0);
                }
                return fbval_copy(elem);
            }

            /* Check user-defined FUNCTION */
            int proc_idx = program_find_proc(interp->prog, aname);
            if (proc_idx >= 0) {
                /* Repackage as a func_call and evaluate */
                ASTNode tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.kind = AST_FUNC_CALL;
                tmp.line = expr->line;
                strncpy(tmp.data.func_call.name, aname, sizeof(tmp.data.func_call.name) - 1);
                tmp.data.func_call.args = expr->data.array_access.subscripts;
                tmp.data.func_call.arg_count = nsubs;
                return eval_builtin_func(interp, &tmp);
            }

            /* Check DEF FN */
            for (int i = 0; i < interp->def_fn_count; i++) {
                if (strcasecmp(interp->def_fns[i].name, aname) == 0) {
                    ASTNode tmp;
                    memset(&tmp, 0, sizeof(tmp));
                    tmp.kind = AST_FUNC_CALL;
                    tmp.line = expr->line;
                    strncpy(tmp.data.func_call.name, aname, sizeof(tmp.data.func_call.name) - 1);
                    tmp.data.func_call.args = expr->data.array_access.subscripts;
                    tmp.data.func_call.arg_count = nsubs;
                    return eval_builtin_func(interp, &tmp);
                }
            }

            /* Auto-dimension and access array (FB behavior) */
            {
                FBType type = scope_default_type(interp->current_scope, aname);
                ArrayDim dims[FB_MAX_DIMENSIONS];
                for (int d = 0; d < nsubs; d++) {
                    dims[d].lower = interp->option_base;
                    dims[d].upper = 10;
                }
                FBArray* arr = fbarray_new(type, nsubs, dims, 0, -1);
                sym = scope_insert(interp->current_scope, aname, SYM_ARRAY, type);
                sym->array = arr;

                int subs[FB_MAX_DIMENSIONS];
                for (int i = 0; i < nsubs; i++) {
                    FBValue v = eval_expr(interp, expr->data.array_access.subscripts[i]);
                    subs[i] = (int)fbval_to_long(&v);
                    fbval_release(&v);
                }
                FBValue* elem = fbarray_get(arr, subs, nsubs);
                if (!elem) {
                    fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, expr->line, NULL);
                    return fbval_int(0);
                }
                return fbval_copy(elem);
            }
        }

        case AST_UDT_MEMBER: {
            FBValue* field_ptr = resolve_lvalue(interp, expr);
            if (!field_ptr) {
                fb_error(FB_ERR_TYPE_MISMATCH, expr->line, "Invalid UDT field access");
                return fbval_int(0);
            }
            return fbval_copy(field_ptr);
        }

        default:
            fb_error(FB_ERR_SYNTAX, expr->line, "Invalid expression node");
            return fbval_int(0);
    }
}

/* ======== Built-in function evaluator ======== */

static FBValue eval_builtin_func(Interpreter* interp, ASTNode* expr) {
    const char* name = expr->data.func_call.name;
    int argc = expr->data.func_call.arg_count;

    /* LBOUND/UBOUND: first arg is array name (not evaluated as value) */
    if (strcasecmp(name, "LBOUND") == 0 || strcasecmp(name, "UBOUND") == 0) {
        if (argc < 1) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, expr->line, NULL);
            return fbval_int(0);
        }
        const char* arr_name = expr->data.func_call.args[0]->data.variable.name;
        Symbol* sym = scope_lookup(interp->current_scope, arr_name);
        if (!sym || sym->kind != SYM_ARRAY || !sym->array) {
            fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, expr->line, NULL);
            return fbval_int(0);
        }
        int dim = 1;
        if (argc >= 2) {
            FBValue dv = eval_expr(interp, expr->data.func_call.args[1]);
            dim = (int)fbval_to_long(&dv);
            fbval_release(&dv);
        }
        if (strcasecmp(name, "LBOUND") == 0)
            return fbval_int((int16_t)fbarray_lbound(sym->array, dim));
        else
            return fbval_int((int16_t)fbarray_ubound(sym->array, dim));
    }

    /* Evaluate arguments up-front */
    FBValue argv[16];
    int real_argc = argc < 16 ? argc : 16;
    for (int i = 0; i < real_argc; i++) {
        argv[i] = eval_expr(interp, expr->data.func_call.args[i]);
    }

    FBValue result = fbval_int(0);

    /* Try math builtins first */
    if (builtin_math_lookup(name)) {
        result = builtin_math_call(name, argv, real_argc, expr->line, interp);
        goto cleanup;
    }

    /* Try string builtins */
    if (builtin_str_lookup(name)) {
        result = builtin_str_call(name, argv, real_argc, expr->line);
        goto cleanup;
    }

    /* Try convert builtins */
    if (builtin_convert_lookup(name)) {
        result = builtin_convert_call(name, argv, real_argc, expr->line);
        goto cleanup;
    }

    /* INKEY$ */
    if (strcasecmp(name, "INKEY$") == 0) {
        int ch = console_inkey();
        if (ch == 0) {
            result = fbval_string_from_cstr("");
        } else if (ch == 0x1B) {
            /* Escape sequences for arrow keys etc. on POSIX */
            int ch2 = console_inkey();
            if (ch2 == '[') {
                int ch3 = console_inkey();
                char buf[3] = { 0, 0, 0 };
                switch (ch3) {
                    case 'A': buf[0] = '\0'; buf[1] = 'H'; break; /* Up */
                    case 'B': buf[0] = '\0'; buf[1] = 'P'; break; /* Down */
                    case 'C': buf[0] = '\0'; buf[1] = 'M'; break; /* Right */
                    case 'D': buf[0] = '\0'; buf[1] = 'K'; break; /* Left */
                    default:  buf[0] = (char)ch3; buf[1] = '\0'; break;
                }
                FBString* s = fbstr_new(buf, 2);
                result.type = FB_STRING;
                result.as.str = s;
            } else {
                char buf[2] = { (char)ch, '\0' };
                result = fbval_string_from_cstr(buf);
            }
        } else {
            char buf[2] = { (char)ch, '\0' };
            result = fbval_string_from_cstr(buf);
        }
        goto cleanup;
    }

    /* CSRLIN */
    if (strcasecmp(name, "CSRLIN") == 0) {
        result = fbval_int((int16_t)console_csrlin());
        goto cleanup;
    }

    /* POS */
    if (strcasecmp(name, "POS") == 0) {
        result = fbval_int((int16_t)console_pos());
        goto cleanup;
    }

    /* TIMER */
    if (strcasecmp(name, "TIMER") == 0) {
        result = fbval_single((float)interp->sys_ops->timer());
        goto cleanup;
    }

    /* ERR */
    if (strcasecmp(name, "ERR") == 0) {
        result = fbval_int((int16_t)interp->err_code);
        goto cleanup;
    }

    /* ERL */
    if (strcasecmp(name, "ERL") == 0) {
        result = fbval_int((int16_t)interp->err_line);
        goto cleanup;
    }

    /* FREEFILE */
    if (strcasecmp(name, "FREEFILE") == 0) {
        int fnum = fb_file_freefile(&interp->file_table);
        if (fnum < 0) {
            fb_error(FB_ERR_TOO_MANY_FILES, expr->line, NULL);
            goto cleanup;
        }
        result = fbval_int((int16_t)fnum);
        goto cleanup;
    }

    /* EOF */
    if (strcasecmp(name, "EOF") == 0) {
        if (argc >= 1) {
            int fnum = (int)fbval_to_long(&argv[0]);
            FBFile* f = fb_file_get(&interp->file_table, fnum);
            if (f) {
                result = fbval_int(fb_file_eof(&interp->file_table, f) ? FB_TRUE : FB_FALSE);
            } else {
                result = fbval_int(FB_TRUE);
            }
        } else {
            result = fbval_int(FB_TRUE);
        }
        goto cleanup;
    }

    /* LOF */
    if (strcasecmp(name, "LOF") == 0) {
        if (argc >= 1) {
            int fnum = (int)fbval_to_long(&argv[0]);
            FBFile* f = fb_file_get(&interp->file_table, fnum);
            result = fbval_long(f ? (int32_t)fb_file_lof(&interp->file_table, f) : 0);
        } else {
            result = fbval_long(0);
        }
        goto cleanup;
    }

    /* LOC */
    if (strcasecmp(name, "LOC") == 0) {
        if (argc >= 1) {
            int fnum = (int)fbval_to_long(&argv[0]);
            FBFile* f = fb_file_get(&interp->file_table, fnum);
            result = fbval_long(f ? (int32_t)fb_file_loc(&interp->file_table, f) : 0);
        } else {
            result = fbval_long(0);
        }
        goto cleanup;
    }

    /* SEEK function (returns current position) */
    if (strcasecmp(name, "SEEK") == 0) {
        if (argc >= 1) {
            int fnum = (int)fbval_to_long(&argv[0]);
            FBFile* f = fb_file_get(&interp->file_table, fnum);
            if (f) {
                result = fbval_long((int32_t)fb_file_seek_get(&interp->file_table, f));
            } else {
                result = fbval_long(0);
            }
        } else {
            result = fbval_long(0);
        }
        goto cleanup;
    }

    /* FILEATTR */
    if (strcasecmp(name, "FILEATTR") == 0) {
        if (argc >= 2) {
            int fnum = (int)fbval_to_long(&argv[0]);
            int attr = (int)fbval_to_long(&argv[1]);
            FBFile* f = fb_file_get(&interp->file_table, fnum);
            if (f && attr == 1) {
                /* Return file mode: 1=INPUT, 2=OUTPUT, 4=RANDOM, 8=APPEND, 32=BINARY */
                switch (f->mode) {
                    case FMODE_INPUT:  result = fbval_int(1); break;
                    case FMODE_OUTPUT: result = fbval_int(2); break;
                    case FMODE_RANDOM: result = fbval_int(4); break;
                    case FMODE_APPEND: result = fbval_int(8); break;
                    case FMODE_BINARY: result = fbval_int(32); break;
                    default:           result = fbval_int(0); break;
                }
            } else {
                result = fbval_int(0);
            }
        } else {
            result = fbval_int(0);
        }
        goto cleanup;
    }

    /* DATE$ */
    if (strcasecmp(name, "DATE$") == 0) {
        int yr, mo, dy;
        interp->sys_ops->get_date(&yr, &mo, &dy);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d-%02d-%04d", mo, dy, yr);
        result = fbval_string_from_cstr(buf);
        goto cleanup;
    }

    /* TIME$ */
    if (strcasecmp(name, "TIME$") == 0) {
        int hr, mn, sc;
        interp->sys_ops->get_time(&hr, &mn, &sc);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hr, mn, sc);
        result = fbval_string_from_cstr(buf);
        goto cleanup;
    }

    /* INPUT$(n) - read n chars from keyboard */
    if (strcasecmp(name, "INPUT$") == 0) {
        int n = (int)fbval_to_long(&argv[0]);
        if (n <= 0) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, expr->line, NULL);
            goto cleanup;
        }
        char* buf = fb_malloc(n + 1);
        for (int i = 0; i < n; i++) {
            int ch;
            do { ch = fgetc(stdin); } while (ch == EOF);
            buf[i] = (char)ch;
        }
        buf[n] = '\0';
        FBString* s = fbstr_new(buf, n);
        fb_free(buf);
        result.type = FB_STRING;
        result.as.str = s;
        goto cleanup;
    }

    /* COMMAND$ */
    if (strcasecmp(name, "COMMAND$") == 0) {
        result = fbval_string_from_cstr(
            interp->command_line ? interp->command_line : "");
        goto cleanup;
    }

    /* ENVIRON$ */
    if (strcasecmp(name, "ENVIRON$") == 0) {
        if (real_argc < 1) {
            result = fbval_string_from_cstr("");
            goto cleanup;
        }
        if (argv[0].type == FB_STRING) {
            const char* nm = (argv[0].as.str) ? argv[0].as.str->data : "";
            const char* val = interp->sys_ops->environ_get(nm);
            result = fbval_string_from_cstr(val ? val : "");
        } else {
            int n = (int)fbval_to_long(&argv[0]);
            const char* val = interp->sys_ops->environ_get_nth(n);
            result = fbval_string_from_cstr(val ? val : "");
        }
        goto cleanup;
    }

    /* FRE */
    if (strcasecmp(name, "FRE") == 0) {
        result = fbval_long(640L * 1024L);
        goto cleanup;
    }

    /* PEEK */
    if (strcasecmp(name, "PEEK") == 0) {
        long addr = (real_argc >= 1) ? fbval_to_long(&argv[0]) : 0;
        result = fbval_int((int16_t)interp->sys_ops->peek(addr));
        goto cleanup;
    }

    /* VARPTR */
    if (strcasecmp(name, "VARPTR") == 0) {
        result = fbval_int((int16_t)0x1000);
        goto cleanup;
    }

    /* VARSEG */
    if (strcasecmp(name, "VARSEG") == 0) {
        result = fbval_int((int16_t)interp->def_seg);
        goto cleanup;
    }

    /* SETMEM */
    if (strcasecmp(name, "SETMEM") == 0) {
        result = fbval_long(0);
        goto cleanup;
    }

    /* Try user SUB/FUNCTION call */
    {
        int proc_idx = program_find_proc(interp->prog, name);
        if (proc_idx >= 0) {
            ASTNode* proc_def = interp->prog->statements[proc_idx];
            if (proc_def->kind == AST_FUNCTION_DEF) {
                Scope* saved_scope = interp->current_scope;
                Scope* func_scope = scope_new(interp->global_scope);
                interp->current_scope = func_scope;

                /* Set up call frame */
                CallFrame frame;
                memset(&frame, 0, sizeof(frame));
                frame.kind = FRAME_FUNCTION;
                frame.return_pc = interp->pc;
                frame.source_line = expr->line;
                strncpy(frame.func_name, name, 41);
                frame.return_type = proc_def->data.proc_def.return_type;
                frame.local_scope = func_scope;

                int pcount = proc_def->data.proc_def.param_count;
                int actual_count = pcount < real_argc ? pcount : real_argc;
                frame.param_count = actual_count;
                frame.param_bindings = NULL;
                if (actual_count > 0) {
                    frame.param_bindings = fb_calloc(actual_count, sizeof(ParamBinding));
                }

                /* We already evaluated argv[] above, so for FUNCTION calls from
                   AST_FUNC_CALL, we pass by value using the already-evaluated args.
                   For calls via AST_ARRAY_ACCESS repackaged, the original args are used. */
                for (int i = 0; i < actual_count; i++) {
                    FBType ptype = proc_def->data.proc_def.params[i].ptype;
                    strncpy(frame.param_bindings[i].param_name,
                            proc_def->data.proc_def.params[i].pname, 41);

                    /* Check if we can do by-reference — we need the original AST arg */
                    int is_byval = proc_def->data.proc_def.params[i].is_byval;
                    ASTNode* orig_arg = expr->data.func_call.args[i];
                    FBValue* caller_addr = NULL;

                    if (!is_byval && (orig_arg->kind == AST_VARIABLE ||
                                      orig_arg->kind == AST_ARRAY_ACCESS)) {
                        caller_addr = resolve_lvalue(interp, orig_arg);
                    }
                    if (orig_arg->kind == AST_PAREN) {
                        is_byval = 1;
                        caller_addr = NULL;
                    }

                    if (!is_byval && caller_addr) {
                        frame.param_bindings[i].is_byval = 0;
                        frame.param_bindings[i].caller_addr = caller_addr;
                        Symbol* param_sym = scope_insert(func_scope,
                            proc_def->data.proc_def.params[i].pname,
                            SYM_VARIABLE, ptype);
                        param_sym->is_ref = 1;
                        param_sym->ref_addr = caller_addr;
                        FBValue coerced = fbval_coerce(caller_addr, ptype);
                        fbval_release(&param_sym->value);
                        param_sym->value = coerced;
                    } else {
                        frame.param_bindings[i].is_byval = 1;
                        frame.param_bindings[i].caller_addr = NULL;
                        Symbol* param_sym = scope_insert(func_scope,
                            proc_def->data.proc_def.params[i].pname,
                            SYM_VARIABLE, ptype);
                        FBValue coerced = fbval_coerce(&argv[i], ptype);
                        fbval_release(&param_sym->value);
                        param_sym->value = coerced;
                    }
                }

                /* Create return variable (function name = return value) */
                Symbol* ret_sym = scope_insert(func_scope, name,
                    SYM_VARIABLE, proc_def->data.proc_def.return_type);

                callstack_push(&interp->call_stack, &frame);
                interp->exit_function = 0;
                interp->exit_procedure = 0;

                exec_block(interp, proc_def->data.proc_def.body,
                           proc_def->data.proc_def.body_count);

                interp->exit_function = 0;
                interp->exit_procedure = 0;

                /* Get return value */
                if (ret_sym) {
                    result = fbval_copy(&ret_sym->value);
                } else {
                    result = fbval_int(0);
                }

                /* Write back by-ref values */
                for (int i = 0; i < frame.param_count; i++) {
                    if (!frame.param_bindings[i].is_byval &&
                        frame.param_bindings[i].caller_addr) {
                        Symbol* sym = scope_lookup_local(func_scope,
                                          frame.param_bindings[i].param_name);
                        if (sym) {
                            fbval_release(frame.param_bindings[i].caller_addr);
                            *frame.param_bindings[i].caller_addr = fbval_copy(&sym->value);
                        }
                    }
                }

                interp->current_scope = saved_scope;
                callstack_pop(&interp->call_stack);
                scope_free(func_scope);
                fb_free(frame.param_bindings);
                goto cleanup;
            }
        }
    }

    /* Check DEF FN */
    for (int i = 0; i < interp->def_fn_count; i++) {
        if (strcasecmp(interp->def_fns[i].name, name) == 0) {
            ASTNode* def = interp->def_fns[i].node;

            /* DEF FN shares module scope — save param values, shadow them */
            FBValue saved_params[16];
            int pc = def->data.def_fn.param_count;
            if (pc > 16) pc = 16;
            for (int j = 0; j < pc && j < real_argc; j++) {
                Symbol* sym = scope_lookup(interp->current_scope,
                                  def->data.def_fn.params[j].pname);
                if (sym) {
                    saved_params[j] = fbval_copy(&sym->value);
                } else {
                    saved_params[j] = fbval_int(0);
                }
            }

            /* Assign parameter values */
            for (int j = 0; j < pc && j < real_argc; j++) {
                Symbol* sym = scope_lookup(interp->current_scope,
                                  def->data.def_fn.params[j].pname);
                if (!sym) {
                    sym = scope_insert(interp->current_scope,
                              def->data.def_fn.params[j].pname,
                              SYM_VARIABLE,
                              def->data.def_fn.params[j].ptype);
                }
                FBValue coerced = fbval_coerce(&argv[j], sym->type);
                fbval_release(&sym->value);
                sym->value = coerced;
            }

            /* Create return variable */
            Symbol* ret = scope_lookup(interp->current_scope, name);
            if (!ret) {
                FBType rtype = scope_default_type(interp->current_scope, name);
                ret = scope_insert(interp->current_scope, name, SYM_VARIABLE, rtype);
            }
            fbval_release(&ret->value);
            ret->value = fbval_int(0);

            if (def->data.def_fn.body_expr) {
                result = eval_expr(interp, def->data.def_fn.body_expr);
            } else {
                exec_block(interp, def->data.def_fn.body,
                           def->data.def_fn.body_count);
                result = fbval_copy(&ret->value);
            }

            /* Restore saved parameter values */
            for (int j = 0; j < pc && j < real_argc; j++) {
                Symbol* sym = scope_lookup(interp->current_scope,
                                  def->data.def_fn.params[j].pname);
                if (sym) {
                    fbval_release(&sym->value);
                    sym->value = saved_params[j];
                } else {
                    fbval_release(&saved_params[j]);
                }
            }

            goto cleanup;
        }
    }

    fb_error(FB_ERR_UNDEFINED_FUNCTION, expr->line, name);

cleanup:
    for (int i = 0; i < real_argc; i++) {
        fbval_release(&argv[i]);
    }
    return result;
}

/* ======== Statement execution ======== */

/* Forward declarations for file I/O helpers (defined after line ~1950) */
static void exec_print_to_file(Interpreter* interp, ASTNode* node, int filenum);
static void exec_write_to_file(Interpreter* interp, ASTNode* node, int filenum);
static void exec_input_from_file(Interpreter* interp, ASTNode* node, int filenum);
static void exec_line_input_from_file(Interpreter* interp, ASTNode* node, int filenum);

/* ---- PRINT ---- */
static void exec_print(Interpreter* interp, ASTNode* node) {
    if (node->data.print.filenum > 0) {
        exec_print_to_file(interp, node, node->data.print.filenum);
        return;
    }
    int col = interp->print_col;

    for (int i = 0; i < node->data.print.item_count; i++) {
        ASTNode* item = node->data.print.items[i];

        /* Check for SPC/TAB pseudo-functions */
        if (item->kind == AST_FUNC_CALL) {
            if (strcasecmp(item->data.func_call.name, "SPC") == 0 &&
                item->data.func_call.arg_count > 0) {
                FBValue v = eval_expr(interp, item->data.func_call.args[0]);
                int n = (int)fbval_to_long(&v);
                fbval_release(&v);
                for (int j = 0; j < n; j++) { putchar(' '); col++; }
                goto next_sep;
            }
            if (strcasecmp(item->data.func_call.name, "TAB") == 0 &&
                item->data.func_call.arg_count > 0) {
                FBValue v = eval_expr(interp, item->data.func_call.args[0]);
                int target = (int)fbval_to_long(&v);
                fbval_release(&v);
                if (col > target) { putchar('\n'); col = 1; }
                while (col < target) { putchar(' '); col++; }
                goto next_sep;
            }
        }

        {
            FBValue val = eval_expr(interp, item);
            char* text = fbval_format_print(&val);
            printf("%s", text);
            col += (int)strlen(text);
            fb_free(text);
            fbval_release(&val);
        }

next_sep:
        if (node->data.print.separators) {
            int sep = node->data.print.separators[i];
            if (sep == TOK_COMMA) {
                int next_zone = ((col - 1) / 14 + 1) * 14 + 1;
                while (col < next_zone) { putchar(' '); col++; }
            }
            /* TOK_SEMICOLON: no spacing */
        }
    }

    if (!node->data.print.trailing_sep) {
        putchar('\n');
        col = 1;
    }

    interp->print_col = col;
    fflush(stdout);
}

/* ---- LET (assignment) ---- */
static void exec_let(Interpreter* interp, ASTNode* node) {
    FBValue rhs = eval_expr(interp, node->data.let.expr);
    exec_assign_value(interp, node->data.let.target, rhs);
}

/* ---- IF ---- */
static void exec_if(Interpreter* interp, ASTNode* node) {
    FBValue cond = eval_expr(interp, node->data.if_block.condition);
    int is_true = fbval_is_true(&cond);
    fbval_release(&cond);

    if (is_true) {
        exec_block(interp, node->data.if_block.then_body,
                   node->data.if_block.then_count);
        return;
    }

    /* ELSEIF chains */
    for (int i = 0; i < node->data.if_block.elseif_n; i++) {
        FBValue econd = eval_expr(interp, node->data.if_block.elseif_cond[i]);
        int etrue = fbval_is_true(&econd);
        fbval_release(&econd);
        if (etrue) {
            exec_block(interp, node->data.if_block.elseif_body[i],
                       node->data.if_block.elseif_count[i]);
            return;
        }
    }

    /* ELSE */
    if (node->data.if_block.else_body) {
        exec_block(interp, node->data.if_block.else_body,
                   node->data.if_block.else_count);
    }
}

/* ---- FOR ---- */
static void exec_for(Interpreter* interp, ASTNode* node) {
    FBValue start_val = eval_expr(interp, node->data.for_loop.start);
    exec_assign_value(interp, node->data.for_loop.var, start_val);

    FBValue end_val = eval_expr(interp, node->data.for_loop.end);
    FBValue step_val;
    if (node->data.for_loop.step) {
        step_val = eval_expr(interp, node->data.for_loop.step);
    } else {
        step_val = fbval_int(1);
    }

    double step_d = fbval_to_double(&step_val);
    int step_positive = (step_d >= 0.0);

    while (interp->running && !interp->exit_for) {
        FBValue current = get_variable_value(interp, node->data.for_loop.var);
        double cur_d = fbval_to_double(&current);
        double end_d = fbval_to_double(&end_val);
        fbval_release(&current);

        if (step_d != 0.0) {
            if (step_positive && cur_d > end_d) break;
            if (!step_positive && cur_d < end_d) break;
        }

        exec_block(interp, node->data.for_loop.body,
                   node->data.for_loop.body_count);

        if (!interp->running || interp->exit_for) break;

        /* Increment */
        FBValue cur = get_variable_value(interp, node->data.for_loop.var);
        FBValue new_val = fbval_binary_op(&cur, &step_val, TOK_PLUS);
        exec_assign_value(interp, node->data.for_loop.var, new_val);
        fbval_release(&cur);
    }

    interp->exit_for = 0;
    fbval_release(&end_val);
    fbval_release(&step_val);
}

/* ---- WHILE...WEND ---- */
static void exec_while(Interpreter* interp, ASTNode* node) {
    while (interp->running && !interp->exit_while) {
        if (node->data.loop.condition) {
            FBValue cond = eval_expr(interp, node->data.loop.condition);
            int is_true = fbval_is_true(&cond);
            fbval_release(&cond);
            if (!is_true) break;
        }
        exec_block(interp, node->data.loop.body, node->data.loop.body_count);
    }
    interp->exit_while = 0;
}

/* ---- DO...LOOP ---- */
static void exec_do_loop(Interpreter* interp, ASTNode* node) {
    while (interp->running && !interp->exit_do) {
        /* Pre-test */
        if (node->data.loop.condition && !node->data.loop.is_post) {
            FBValue cond = eval_expr(interp, node->data.loop.condition);
            int result = fbval_is_true(&cond);
            fbval_release(&cond);
            if (node->data.loop.is_until) result = !result;
            if (!result) break;
        }

        exec_block(interp, node->data.loop.body, node->data.loop.body_count);
        if (!interp->running || interp->exit_do) break;

        /* Post-test */
        if (node->data.loop.condition && node->data.loop.is_post) {
            FBValue cond = eval_expr(interp, node->data.loop.condition);
            int result = fbval_is_true(&cond);
            fbval_release(&cond);
            if (node->data.loop.is_until) result = !result;
            if (!result) break;
        }
    }
    interp->exit_do = 0;
}

/* ---- GOTO ---- */
static void exec_goto(Interpreter* interp, ASTNode* node) {
    int target = resolve_jump_target(interp, node);
    if (target >= 0) {
        interp->pc = target;
    }
}

/* ---- GOSUB ---- */
static void exec_gosub(Interpreter* interp, ASTNode* node) {
    if (interp->gosub_sp >= MAX_GOSUB_STACK) {
        fb_error(FB_ERR_OUT_OF_MEMORY, node->line, "GOSUB stack overflow");
        return;
    }
    interp->gosub_stack[interp->gosub_sp] = interp->pc + 1;
    interp->gosub_sp++;

    int target = resolve_jump_target(interp, node);
    if (target >= 0) {
        interp->pc = target;
    }
}

/* ---- RETURN ---- */
static void exec_return(Interpreter* interp, ASTNode* node) {
    if (interp->gosub_sp == 0) {
        fb_error(FB_ERR_RETURN_WITHOUT_GOSUB, node->line, NULL);
        return;
    }
    interp->gosub_sp--;
    interp->pc = interp->gosub_stack[interp->gosub_sp];
}

/* ---- END / STOP ---- */
static void exec_end(Interpreter* interp, ASTNode* node) {
    interp->running = 0;
    if (node->kind == AST_STOP) {
        printf("Break in line %d\n", node->line);
    }
}

/* ---- DIM ---- */
static void exec_dim(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.dim.name;
    FBType type = node->data.dim.type;
    int is_shared = node->data.dim.is_shared;

    Scope* target = is_shared ? interp->global_scope : interp->current_scope;

    /* Check if it's a DIM with array bounds */
    if (node->data.dim.ndims > 0) {
        Symbol* existing = scope_lookup_local(target, name);
        if (existing && existing->kind == SYM_ARRAY) return;

        int ndims = node->data.dim.ndims;
        ArrayDim dims[FB_MAX_DIMENSIONS];
        for (int d = 0; d < ndims; d++) {
            ASTNode* lo = node->data.dim.bounds[d * 2];
            ASTNode* hi = node->data.dim.bounds[d * 2 + 1];
            if (lo) {
                FBValue lv = eval_expr(interp, lo);
                dims[d].lower = (int)fbval_to_long(&lv);
                fbval_release(&lv);
            } else {
                dims[d].lower = interp->option_base;
            }
            FBValue hv = eval_expr(interp, hi);
            dims[d].upper = (int)fbval_to_long(&hv);
            fbval_release(&hv);
        }

        int is_dynamic = node->data.dim.is_dynamic;
        int udt_type_id = -1;

        /* Check AS typename for UDT */
        if (node->data.dim.as_type_name[0]) {
            udt_type_id = udt_find(&interp->udt_registry, node->data.dim.as_type_name);
            if (udt_type_id >= 0) type = FB_UDT;
        }

        FBArray* arr = fbarray_new(type, ndims, dims, is_dynamic, udt_type_id);
        if (!arr) {
            fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, node->line, "Invalid array bounds");
            return;
        }

        /* If UDT array, allocate UDT instances for each element */
        if (udt_type_id >= 0) {
            for (int i = 0; i < arr->total_elements; i++) {
                FBValue* fields = udt_alloc_instance(&interp->udt_registry, udt_type_id);
                arr->data[i] = fbval_udt(udt_type_id, fields);
            }
        }

        Symbol* sym = scope_insert(target, name, SYM_ARRAY, type);
        sym->array = arr;
        sym->is_shared = is_shared;
        return;
    }

    /* Scalar DIM — may be a UDT variable */
    if (node->data.dim.as_type_name[0]) {
        int udt_type_id = udt_find(&interp->udt_registry, node->data.dim.as_type_name);
        if (udt_type_id >= 0) {
            Symbol* existing = scope_lookup_local(target, name);
            if (existing) return;
            Symbol* sym = scope_insert(target, name, SYM_VARIABLE, FB_UDT);
            sym->udt_type_id = udt_type_id;
            sym->is_shared = is_shared;
            FBValue* fields = udt_alloc_instance(&interp->udt_registry, udt_type_id);
            fbval_release(&sym->value);
            sym->value = fbval_udt(udt_type_id, fields);
            return;
        }
    }

    /* Plain scalar DIM */
    Symbol* existing = scope_lookup_local(target, name);
    if (existing) return;

    Symbol* sym = scope_insert(target, name, SYM_VARIABLE, type);
    sym->is_shared = is_shared;
}

/* ---- CONST ---- */
static void exec_const(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.const_decl.name;
    FBValue val = eval_expr(interp, node->data.const_decl.value_expr);

    Symbol* existing = scope_lookup_local(interp->current_scope, name);
    if (existing) {
        fbval_release(&val);
        return;
    }

    Symbol* sym = scope_insert(interp->current_scope, name, SYM_CONST, val.type);
    if (sym) {
        fbval_release(&sym->value);
        sym->value = val;
    } else {
        fbval_release(&val);
    }
}

/* ---- DEFTYPE ---- */
static void exec_deftype(Interpreter* interp, ASTNode* node) {
    char start = node->data.deftype.range_start;
    char end   = node->data.deftype.range_end;
    FBType type = node->data.deftype.type;

    for (char c = toupper(start); c <= toupper(end); c++) {
        interp->global_scope->deftype[c - 'A'] = type;
    }
}

/* ---- REDIM ---- */
static void exec_redim(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.dim.name;
    Symbol* sym = scope_lookup(interp->current_scope, name);
    if (sym && sym->kind == SYM_ARRAY && sym->array) {
        if (!sym->array->is_dynamic) {
            fb_error(FB_ERR_DUPLICATE_DEFINITION, node->line,
                     "Cannot REDIM static array");
            return;
        }
        int ndims = node->data.dim.ndims;
        ArrayDim dims[FB_MAX_DIMENSIONS];
        for (int d = 0; d < ndims; d++) {
            ASTNode* lo = node->data.dim.bounds[d * 2];
            ASTNode* hi = node->data.dim.bounds[d * 2 + 1];
            if (lo) {
                FBValue lv = eval_expr(interp, lo);
                dims[d].lower = (int)fbval_to_long(&lv);
                fbval_release(&lv);
            } else {
                dims[d].lower = interp->option_base;
            }
            FBValue hv = eval_expr(interp, hi);
            dims[d].upper = (int)fbval_to_long(&hv);
            fbval_release(&hv);
        }
        fbarray_redim(sym->array, ndims, dims);
    } else {
        /* First time — create as dynamic. Reuse dim logic but force dynamic. */
        ASTNode modified = *node;
        modified.data.dim.is_dynamic = 1;
        modified.kind = AST_DIM;
        exec_dim(interp, &modified);
    }
}

/* ---- ERASE ---- */
static void exec_erase(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.erase.name_count; i++) {
        Symbol* sym = scope_lookup(interp->current_scope,
                                   node->data.erase.names[i]);
        if (!sym || sym->kind != SYM_ARRAY || !sym->array) continue;

        if (sym->array->is_dynamic) {
            fbarray_free(sym->array);
            sym->array = NULL;
        } else {
            fbarray_reinit(sym->array);
        }
    }
}

/* ---- TYPE...END TYPE ---- */
static void exec_type_def(Interpreter* interp, ASTNode* node) {
    const char* tname = node->data.type_def.name;
    int type_id = udt_register(&interp->udt_registry, tname);
    if (type_id < 0) return; /* Already registered */

    for (int i = 0; i < node->data.type_def.field_count; i++) {
        FBType ft = node->data.type_def.fields[i].type;
        int slen = node->data.type_def.fields[i].string_len;
        udt_add_field(&interp->udt_registry, type_id,
                       node->data.type_def.fields[i].field_name,
                       ft, slen, -1);
    }
    udt_finalize(&interp->udt_registry, type_id);
}

/* ---- SHARED statement (inside SUB/FUNCTION) ---- */
static void exec_shared_stmt(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.shared_stmt.name_count; i++) {
        Symbol* global = scope_lookup_local(interp->global_scope,
                                            node->data.shared_stmt.names[i]);
        if (!global) {
            /* Create in global scope if not exists */
            FBType type = node->data.shared_stmt.types[i];
            if ((int)type == -1 || type == 0)
                type = scope_default_type(interp->global_scope,
                                         node->data.shared_stmt.names[i]);
            global = scope_insert(interp->global_scope,
                                  node->data.shared_stmt.names[i],
                                  SYM_VARIABLE, type);
        }
        /* Create alias in current scope */
        Symbol* local = scope_insert(interp->current_scope,
                                     node->data.shared_stmt.names[i],
                                     global->kind, global->type);
        local->is_ref = 1;
        if (global->kind == SYM_ARRAY) {
            local->kind = SYM_ARRAY;
            local->array = global->array;
        }
        local->ref_addr = &global->value;
        local->value = fbval_copy(&global->value);
    }
}

/* ---- STATIC statement (inside SUB/FUNCTION) ---- */
static void exec_static_stmt(Interpreter* interp, ASTNode* node) {
    /* STATIC variables are just regular variables with is_static flag.
       The real effect is in call frame handling — but we mark them here. */
    for (int i = 0; i < node->data.shared_stmt.name_count; i++) {
        Symbol* sym = scope_lookup_local(interp->current_scope,
                                         node->data.shared_stmt.names[i]);
        if (!sym) {
            FBType type = node->data.shared_stmt.types[i];
            if ((int)type == -1 || type == 0)
                type = scope_default_type(interp->current_scope,
                                         node->data.shared_stmt.names[i]);
            sym = scope_insert(interp->current_scope,
                               node->data.shared_stmt.names[i],
                               SYM_VARIABLE, type);
        }
        sym->is_static = 1;
    }
}

/* ---- LSET ---- */
static void exec_lset(Interpreter* interp, ASTNode* node) {
    Symbol* sym = resolve_or_create_var(interp, node->data.lrset.target);
    FBValue rhs = eval_expr(interp, node->data.lrset.expr);

    if (sym->value.type != FB_STRING || rhs.type != FB_STRING) {
        fbval_release(&rhs);
        return;
    }

    int target_len = sym->value.as.str->len;
    sym->value.as.str = fbstr_cow(sym->value.as.str);
    memset(sym->value.as.str->data, ' ', target_len);
    int copy_len = rhs.as.str->len;
    if (copy_len > target_len) copy_len = target_len;
    memcpy(sym->value.as.str->data, rhs.as.str->data, copy_len);
    fbval_release(&rhs);
}

/* ---- RSET ---- */
static void exec_rset(Interpreter* interp, ASTNode* node) {
    Symbol* sym = resolve_or_create_var(interp, node->data.lrset.target);
    FBValue rhs = eval_expr(interp, node->data.lrset.expr);

    if (sym->value.type != FB_STRING || rhs.type != FB_STRING) {
        fbval_release(&rhs);
        return;
    }

    int target_len = sym->value.as.str->len;
    sym->value.as.str = fbstr_cow(sym->value.as.str);
    memset(sym->value.as.str->data, ' ', target_len);
    int copy_len = rhs.as.str->len;
    if (copy_len > target_len) copy_len = target_len;
    int offset = target_len - copy_len;
    memcpy(sym->value.as.str->data + offset, rhs.as.str->data, copy_len);
    fbval_release(&rhs);
}

/* ---- SELECT CASE ---- */
static void exec_select_case(Interpreter* interp, ASTNode* node) {
    FBValue test = eval_expr(interp, node->data.select_case.test_expr);

    for (int i = 0; i < node->data.select_case.case_count; i++) {
        int matched = 0;
        int vc = node->data.select_case.cases[i].value_count;

        for (int j = 0; j < vc; j++) {
            int kind = node->data.select_case.cases[i].kinds[j];

            if (kind == 0) {
                /* Single value match */
                FBValue v = eval_expr(interp,
                    node->data.select_case.cases[i].values[j]);
                FBValue cmp = fbval_compare(&test, &v, TOK_EQ);
                matched = fbval_is_true(&cmp);
                fbval_release(&v);
            } else if (kind == 1) {
                /* Range: value TO range_end */
                FBValue lo = eval_expr(interp,
                    node->data.select_case.cases[i].values[j]);
                FBValue hi = eval_expr(interp,
                    node->data.select_case.cases[i].range_ends[j]);
                FBValue cmp_lo = fbval_compare(&test, &lo, TOK_GE);
                FBValue cmp_hi = fbval_compare(&test, &hi, TOK_LE);
                matched = fbval_is_true(&cmp_lo) && fbval_is_true(&cmp_hi);
                fbval_release(&lo);
                fbval_release(&hi);
            } else if (kind == 2) {
                /* IS relop */
                TokenKind relop = node->data.select_case.cases[i].relops[j];
                FBValue v = eval_expr(interp,
                    node->data.select_case.cases[i].values[j]);
                FBValue cmp = fbval_compare(&test, &v, relop);
                matched = fbval_is_true(&cmp);
                fbval_release(&v);
            }

            if (matched) break;
        }

        if (matched) {
            exec_block(interp,
                node->data.select_case.cases[i].body,
                node->data.select_case.cases[i].body_count);
            fbval_release(&test);
            return;
        }
    }

    /* CASE ELSE */
    if (node->data.select_case.else_body) {
        exec_block(interp, node->data.select_case.else_body,
                   node->data.select_case.else_count);
    }

    fbval_release(&test);
}

/* ---- INPUT ---- */
static void exec_input(Interpreter* interp, ASTNode* node) {
    if (node->data.input.filenum > 0) {
        exec_input_from_file(interp, node, node->data.input.filenum);
        return;
    }
retry:;
    /* Print prompt */
    if (node->data.input.prompt) {
        FBValue pv = eval_expr(interp, node->data.input.prompt);
        if (pv.type == FB_STRING && pv.as.str) {
            printf("%s", pv.as.str->data);
        }
        fbval_release(&pv);
    }
    if (!node->data.input.no_newline || node->data.input.prompt) {
        printf("? ");
    }
    fflush(stdout);

    char linebuf[4096];
    if (!fgets(linebuf, sizeof(linebuf), stdin)) {
        interp->running = 0;
        return;
    }
    int len = (int)strlen(linebuf);
    while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
        linebuf[--len] = '\0';

    /* Split by commas */
    char* fields[64];
    int field_count = 0;
    char* ptr = linebuf;
    fields[field_count++] = ptr;
    while (*ptr) {
        if (*ptr == ',') {
            *ptr = '\0';
            ptr++;
            while (*ptr == ' ') ptr++;
            fields[field_count++] = ptr;
        } else {
            ptr++;
        }
    }

    if (field_count != node->data.input.var_count) {
        printf("Redo from start\n");
        goto retry;
    }

    for (int i = 0; i < node->data.input.var_count; i++) {
        Symbol* sym = resolve_or_create_var(interp, node->data.input.vars[i]);
        if (sym->type == FB_STRING) {
            fbval_release(&sym->value);
            sym->value = fbval_string_from_cstr(fields[i]);
        } else {
            char* endp;
            double val = strtod(fields[i], &endp);
            while (*endp == ' ') endp++;
            if (*endp != '\0') {
                printf("Redo from start\n");
                goto retry;
            }
            FBValue numval = fbval_double(val);
            FBValue coerced = fbval_coerce(&numval, sym->type);
            fbval_release(&sym->value);
            sym->value = coerced;
        }
    }
}

/* ---- LINE INPUT ---- */
static void exec_line_input(Interpreter* interp, ASTNode* node) {
    if (node->data.input.filenum > 0) {
        exec_line_input_from_file(interp, node, node->data.input.filenum);
        return;
    }
    if (node->data.input.prompt) {
        FBValue pv = eval_expr(interp, node->data.input.prompt);
        if (pv.type == FB_STRING && pv.as.str) {
            printf("%s", pv.as.str->data);
        }
        fbval_release(&pv);
    }
    fflush(stdout);

    char linebuf[32768];
    if (!fgets(linebuf, sizeof(linebuf), stdin)) {
        interp->running = 0;
        return;
    }
    int len = (int)strlen(linebuf);
    while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r'))
        linebuf[--len] = '\0';

    if (node->data.input.var_count > 0) {
        Symbol* sym = resolve_or_create_var(interp, node->data.input.vars[0]);
        fbval_release(&sym->value);
        sym->value = fbval_string_from_cstr(linebuf);
    }
}

/* ---- WRITE ---- */
static void exec_write(Interpreter* interp, ASTNode* node) {
    if (node->data.write_stmt.filenum > 0) {
        exec_write_to_file(interp, node, node->data.write_stmt.filenum);
        return;
    }
    for (int i = 0; i < node->data.write_stmt.item_count; i++) {
        if (i > 0) printf(",");
        FBValue val = eval_expr(interp, node->data.write_stmt.items[i]);
        if (val.type == FB_STRING) {
            printf("\"%s\"", val.as.str ? val.as.str->data : "");
        } else if (val.type == FB_INTEGER || val.type == FB_LONG) {
            printf("%d", (int)fbval_to_long(&val));
        } else {
            printf("%g", fbval_to_double(&val));
        }
        fbval_release(&val);
    }
    printf("\n");
    fflush(stdout);
}

/* ---- CLS ---- */
static void exec_cls(Interpreter* interp, ASTNode* node) {
    (void)node;
    console_cls();
    interp->print_col = 1;
}

/* ---- LOCATE ---- */
static void exec_locate(Interpreter* interp, ASTNode* node) {
    int row, col;
    if (node->data.locate.row) {
        FBValue v = eval_expr(interp, node->data.locate.row);
        row = (int)fbval_to_long(&v);
        fbval_release(&v);
    } else {
        row = console_csrlin();
    }
    if (node->data.locate.col) {
        FBValue v = eval_expr(interp, node->data.locate.col);
        col = (int)fbval_to_long(&v);
        fbval_release(&v);
    } else {
        col = console_pos();
    }
    console_locate(row, col);
    interp->print_col = col;
}

/* ---- COLOR ---- */
static void exec_color(Interpreter* interp, ASTNode* node) {
    int fg, bg;
    if (node->data.color.fg) {
        FBValue v = eval_expr(interp, node->data.color.fg);
        fg = (int)fbval_to_long(&v);
        fbval_release(&v);
    } else {
        fg = interp->current_fg;
    }
    if (node->data.color.bg) {
        FBValue v = eval_expr(interp, node->data.color.bg);
        bg = (int)fbval_to_long(&v);
        fbval_release(&v);
    } else {
        bg = interp->current_bg;
    }
    interp->current_fg = fg;
    interp->current_bg = bg;
    console_color(fg, bg);
}

/* ---- VIEW PRINT ---- */
static void exec_view_print(Interpreter* interp, ASTNode* node) {
    if (node->data.view_print.top && node->data.view_print.bottom) {
        FBValue vt = eval_expr(interp, node->data.view_print.top);
        FBValue vb = eval_expr(interp, node->data.view_print.bottom);
        int top = (int)fbval_to_long(&vt);
        int bot = (int)fbval_to_long(&vb);
        fbval_release(&vt);
        fbval_release(&vb);
        interp->scroll_top = top;
        interp->scroll_bottom = bot;
        printf("\033[%d;%dr", top, bot);
    } else {
        interp->scroll_top = 1;
        interp->scroll_bottom = 25;
        printf("\033[r");
    }
    fflush(stdout);
}

/* ---- WIDTH ---- */
static void exec_width(Interpreter* interp, ASTNode* node) {
    FBValue vc = eval_expr(interp, node->data.width_stmt.cols);
    int cols = (int)fbval_to_long(&vc);
    fbval_release(&vc);
    int rows = 25;
    if (node->data.width_stmt.rows) {
        FBValue vr = eval_expr(interp, node->data.width_stmt.rows);
        rows = (int)fbval_to_long(&vr);
        fbval_release(&vr);
    }
    interp->screen_width = cols;
    interp->screen_height = rows;
    console_width(cols, rows);
}

/* ---- BEEP ---- */
static void exec_beep(Interpreter* interp, ASTNode* node) {
    (void)interp; (void)node;
    console_beep();
}

/* ---- DATA (no-op at runtime, already collected at parse time) ---- */
/* ---- READ ---- */
static void exec_read(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.read_stmt.var_count; i++) {
        if (interp->data_ptr >= interp->prog->data_count) {
            fb_error(FB_ERR_OUT_OF_DATA, node->line, NULL);
            return;
        }

        FBValue data_val = fbval_copy(&interp->prog->data_pool[interp->data_ptr]);
        interp->data_ptr++;

        ASTNode* var_node = node->data.read_stmt.vars[i];

        /* Use resolve_lvalue for array/UDT targets, fall back to resolve_or_create_var */
        FBValue* target_ptr = NULL;
        FBType target_type = FB_SINGLE;

        if (var_node->kind == AST_ARRAY_ACCESS || var_node->kind == AST_UDT_MEMBER) {
            target_ptr = resolve_lvalue(interp, var_node);
            if (target_ptr) target_type = target_ptr->type;
        }

        if (!target_ptr) {
            Symbol* sym = resolve_or_create_var(interp, var_node);
            if (sym->is_ref && sym->ref_addr) {
                target_ptr = sym->ref_addr;
            } else {
                target_ptr = &sym->value;
            }
            target_type = sym->type;
        }

        /* Coerce data to target type */
        if (target_type == FB_STRING && data_val.type != FB_STRING) {
            char* fmt = fbval_format_print(&data_val);
            fbval_release(&data_val);
            fbval_release(target_ptr);
            *target_ptr = fbval_string_from_cstr(fmt);
            fb_free(fmt);
        } else if (target_type != FB_STRING && data_val.type == FB_STRING) {
            double v = data_val.as.str ? atof(data_val.as.str->data) : 0.0;
            FBValue numval = fbval_double(v);
            FBValue coerced = fbval_coerce(&numval, target_type);
            fbval_release(&data_val);
            fbval_release(target_ptr);
            *target_ptr = coerced;
        } else {
            FBValue coerced = fbval_coerce(&data_val, target_type);
            fbval_release(&data_val);
            fbval_release(target_ptr);
            *target_ptr = coerced;
        }
    }
}

/* ---- RESTORE ---- */
static void exec_restore(Interpreter* interp, ASTNode* node) {
    if (node->data.restore.label[0] || node->data.restore.lineno >= 0) {
        /* Find the data index for a specific label/line */
        for (int i = 0; i < interp->prog->data_label_count; i++) {
            if (node->data.restore.lineno >= 0 &&
                interp->prog->data_labels[i].lineno == node->data.restore.lineno) {
                interp->data_ptr = interp->prog->data_labels[i].data_index;
                return;
            }
            if (node->data.restore.label[0] &&
                strcasecmp(interp->prog->data_labels[i].label,
                          node->data.restore.label) == 0) {
                interp->data_ptr = interp->prog->data_labels[i].data_index;
                return;
            }
        }
        /* Not found, reset to beginning */
        interp->data_ptr = 0;
    } else {
        interp->data_ptr = 0;
    }
}

/* ---- PRINT USING ---- */
static void exec_print_using_stmt(Interpreter* interp, ASTNode* node) {
    if (node->data.print_using.filenum > 0) {
        /* PRINT #n, USING ... — format and write to file */
        FBFile* f = fb_file_get(&interp->file_table, node->data.print_using.filenum);
        if (!f) {
            fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
            return;
        }
        FBValue fmt_val = eval_expr(interp, node->data.print_using.format_expr);
        if (fmt_val.type != FB_STRING || !fmt_val.as.str) {
            fb_error(FB_ERR_TYPE_MISMATCH, node->line, "PRINT USING format must be string");
            fbval_release(&fmt_val);
            return;
        }
        FBValue* values = NULL;
        int value_count = node->data.print_using.item_count;
        if (value_count > 0) {
            values = fb_malloc(value_count * sizeof(FBValue));
            for (int i = 0; i < value_count; i++)
                values[i] = eval_expr(interp, node->data.print_using.items[i]);
        }
        /* Format to a string buffer, then write to file */
        char* result = format_print_using(fmt_val.as.str->data, values, value_count);
        if (result) {
            fb_file_write_bytes(&interp->file_table, f, result, strlen(result));
            fb_free(result);
        }
        fb_file_write_bytes(&interp->file_table, f, "\r\n", 2);
        for (int i = 0; i < value_count; i++) fbval_release(&values[i]);
        fb_free(values);
        fbval_release(&fmt_val);
        return;
    }
    FBValue fmt_val = eval_expr(interp, node->data.print_using.format_expr);
    if (fmt_val.type != FB_STRING || !fmt_val.as.str) {
        fb_error(FB_ERR_TYPE_MISMATCH, node->line, "PRINT USING format must be string");
        fbval_release(&fmt_val);
        return;
    }

    FBValue* values = NULL;
    int value_count = node->data.print_using.item_count;
    if (value_count > 0) {
        values = fb_malloc(value_count * sizeof(FBValue));
        for (int i = 0; i < value_count; i++) {
            values[i] = eval_expr(interp, node->data.print_using.items[i]);
        }
    }

    exec_print_using(fmt_val.as.str->data, values, value_count);

    for (int i = 0; i < value_count; i++) {
        fbval_release(&values[i]);
    }
    fb_free(values);
    fbval_release(&fmt_val);
    fflush(stdout);
}

/* ---- RANDOMIZE ---- */
static void exec_randomize(Interpreter* interp, ASTNode* node) {
    if (node->data.randomize.use_timer) {
        interp->rnd_seed = (unsigned int)time(NULL);
    } else if (node->data.randomize.seed) {
        FBValue val = eval_expr(interp, node->data.randomize.seed);
        interp->rnd_seed = (unsigned int)fbval_to_long(&val);
        fbval_release(&val);
    } else {
        printf("Random-number seed (-32768 to 32767)? ");
        fflush(stdout);
        char buf[64];
        if (fgets(buf, sizeof(buf), stdin)) {
            interp->rnd_seed = (unsigned int)atoi(buf);
        }
    }
}

/* ---- SWAP ---- */
static void exec_swap(Interpreter* interp, ASTNode* node) {
    FBValue* loc1 = resolve_lvalue(interp, node->data.swap.a);
    FBValue* loc2 = resolve_lvalue(interp, node->data.swap.b);

    if (!loc1 || !loc2) {
        fb_error(FB_ERR_TYPE_MISMATCH, node->line, "SWAP: invalid target");
        return;
    }

    FBValue tmp = *loc1;
    *loc1 = *loc2;
    *loc2 = tmp;
}

/* ---- EXIT ---- */
static void exec_exit(Interpreter* interp, ASTNode* node) {
    switch (node->data.exit_stmt.exit_what) {
        case 0: interp->exit_for = 1; break;
        case 1: interp->exit_do = 1; break;
        case 2: interp->exit_while = 1; break;
        case 3: interp->exit_sub = 1; break;
        case 4: interp->exit_function = 1; break;
        default: break;
    }
}

/* ---- ON GOTO / ON GOSUB ---- */
static void exec_on_branch(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.on_branch.expr);
    int idx = (int)fbval_to_long(&val);
    fbval_release(&val);

    if (idx < 1 || idx > node->data.on_branch.label_count) return;
    idx--; /* 0-based */

    int target = -1;
    if (node->data.on_branch.linenos[idx] >= 0) {
        target = program_find_lineno(interp->prog, node->data.on_branch.linenos[idx]);
    } else if (node->data.on_branch.labels[idx][0]) {
        target = program_find_label(interp->prog, node->data.on_branch.labels[idx]);
    }

    if (target < 0) return;

    if (node->kind == AST_ON_GOSUB) {
        if (interp->gosub_sp >= MAX_GOSUB_STACK) {
            fb_error(FB_ERR_OUT_OF_MEMORY, node->line, "GOSUB stack overflow");
            return;
        }
        interp->gosub_stack[interp->gosub_sp] = interp->pc + 1;
        interp->gosub_sp++;
    }

    interp->pc = target;
}

/* ---- SUB/FUNCTION def (skip at runtime) ---- */
/* ---- CALL (SUB invocation) ---- */
static void exec_call(Interpreter* interp, ASTNode* node) {
    const char* name = node->data.call.name;
    int proc_idx = program_find_proc(interp->prog, name);
    if (proc_idx < 0) {
        fb_error(FB_ERR_SUBPROGRAM_NOT_DEFINED, node->line, name);
        return;
    }

    ASTNode* proc_def = interp->prog->statements[proc_idx];

    /* Set up call frame */
    CallFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.kind = FRAME_SUB;
    frame.return_pc = interp->pc;
    frame.source_line = node->line;
    frame.is_static = proc_def->data.proc_def.is_static;

    /* Create local scope */
    Scope* saved_scope = interp->current_scope;
    Scope* sub_scope = scope_new(interp->global_scope);
    frame.local_scope = sub_scope;

    /* Bind parameters */
    int pcount = proc_def->data.proc_def.param_count;
    int argc = node->data.call.arg_count;
    frame.param_count = pcount < argc ? pcount : argc;
    frame.param_bindings = NULL;
    if (frame.param_count > 0) {
        frame.param_bindings = fb_calloc(frame.param_count, sizeof(ParamBinding));
    }

    for (int i = 0; i < frame.param_count; i++) {
        strncpy(frame.param_bindings[i].param_name,
                proc_def->data.proc_def.params[i].pname, 41);
        FBType ptype = proc_def->data.proc_def.params[i].ptype;
        int is_byval = proc_def->data.proc_def.params[i].is_byval;

        /* Determine if by-ref or by-val */
        ASTNode* arg = node->data.call.args[i];
        FBValue* caller_addr = NULL;

        if (!is_byval && (arg->kind == AST_VARIABLE || arg->kind == AST_ARRAY_ACCESS)) {
            /* Possible by-reference */
            caller_addr = resolve_lvalue(interp, arg);
        }
        if (arg->kind == AST_PAREN) {
            /* Extra parens force by-value */
            is_byval = 1;
            caller_addr = NULL;
        }

        if (!is_byval && caller_addr) {
            /* By reference */
            frame.param_bindings[i].is_byval = 0;
            frame.param_bindings[i].caller_addr = caller_addr;
            Symbol* param_sym = scope_insert(sub_scope,
                proc_def->data.proc_def.params[i].pname,
                SYM_VARIABLE, ptype);
            param_sym->is_ref = 1;
            param_sym->ref_addr = caller_addr;
            FBValue coerced = fbval_coerce(caller_addr, ptype);
            fbval_release(&param_sym->value);
            param_sym->value = coerced;
        } else {
            /* By value */
            frame.param_bindings[i].is_byval = 1;
            frame.param_bindings[i].caller_addr = NULL;
            FBValue arg_val = eval_expr(interp, arg);
            Symbol* param_sym = scope_insert(sub_scope,
                proc_def->data.proc_def.params[i].pname,
                SYM_VARIABLE, ptype);
            FBValue coerced = fbval_coerce(&arg_val, ptype);
            fbval_release(&arg_val);
            fbval_release(&param_sym->value);
            param_sym->value = coerced;
        }
    }

    /* Push frame and execute */
    callstack_push(&interp->call_stack, &frame);
    interp->current_scope = sub_scope;
    interp->exit_sub = 0;
    interp->exit_procedure = 0;

    exec_block(interp, proc_def->data.proc_def.body,
               proc_def->data.proc_def.body_count);

    interp->exit_sub = 0;
    interp->exit_procedure = 0;

    /* Write back by-ref values */
    for (int i = 0; i < frame.param_count; i++) {
        if (!frame.param_bindings[i].is_byval &&
            frame.param_bindings[i].caller_addr) {
            Symbol* sym = scope_lookup_local(sub_scope,
                              frame.param_bindings[i].param_name);
            if (sym) {
                fbval_release(frame.param_bindings[i].caller_addr);
                *frame.param_bindings[i].caller_addr = fbval_copy(&sym->value);
            }
        }
    }

    /* Restore scope and pop frame */
    interp->current_scope = saved_scope;
    callstack_pop(&interp->call_stack);
    scope_free(sub_scope);
    fb_free(frame.param_bindings);
}

/* ---- DEF FN ---- */
static void exec_def_fn(Interpreter* interp, ASTNode* node) {
    /* Register the DEF FN for later use */
    if (interp->def_fn_count >= interp->def_fn_cap) {
        interp->def_fn_cap = interp->def_fn_cap ? interp->def_fn_cap * 2 : 16;
        interp->def_fns = fb_realloc(interp->def_fns,
            interp->def_fn_cap * sizeof(interp->def_fns[0]));
    }
    strncpy(interp->def_fns[interp->def_fn_count].name,
            node->data.def_fn.name, 41);
    interp->def_fns[interp->def_fn_count].node = node;
    interp->def_fn_count++;
}

/* ---- MID$ statement (lvalue) ---- */
static void exec_mid_stmt(Interpreter* interp, ASTNode* node) {
    /* MID$ statement is parsed as: LET with target being special
     * For now, handle via the write_stmt or a custom approach.
     * The parser emits this as AST_LET where the target is an AST_FUNC_CALL to MID$.
     * We handle it specially here. */
    (void)interp; (void)node;
    /* TODO: Implement MID$ lvalue once parser emits it correctly */
}

/* ---- SLEEP ---- */
static void exec_sleep(Interpreter* interp, ASTNode* node) {
    (void)interp;
    int secs = 0;
    if (node->data.sleep_stmt.seconds) {
        FBValue v = eval_expr(interp, node->data.sleep_stmt.seconds);
        secs = (int)fbval_to_long(&v);
        fbval_release(&v);
    }
    if (secs > 0) {
#ifdef _WIN32
        Sleep(secs * 1000);
#else
        sleep(secs);
#endif
    } else {
        /* SLEEP with no argument waits for keypress */
        fgetc(stdin);
    }
}

/* ---- TRON / TROFF ---- */
static void exec_tron(Interpreter* interp) { interp->trace_on = 1; }
static void exec_troff(Interpreter* interp) { interp->trace_on = 0; }

/* ---- ON ERROR ---- */
static void exec_on_error(Interpreter* interp, ASTNode* node) {
    if (node->data.on_error.disable) {
        if (interp->in_error_handler) {
            /* ON ERROR GOTO 0 inside handler: re-raise as fatal */
            interp->in_error_handler = 0;
            interp->on_error_target = -1;
            fprintf(stderr, "Unhandled error %d: %s in line %d\n",
                    interp->err_code,
                    fb_error_message((FBErrorCode)interp->err_code),
                    interp->err_line);
            interp->running = 0;
            return;
        }
        interp->on_error_target = -1;
    } else {
        int target = -1;
        if (node->data.on_error.lineno >= 0) {
            target = program_find_lineno(interp->prog, node->data.on_error.lineno);
        } else if (node->data.on_error.label[0]) {
            target = program_find_label(interp->prog, node->data.on_error.label);
        }
        interp->on_error_target = target;
    }
}

/* ---- RESUME ---- */
static void exec_resume(Interpreter* interp, ASTNode* node) {
    if (!interp->in_error_handler) {
        fb_error(FB_ERR_RESUME_WITHOUT_ERROR, node->line, NULL);
        return;
    }
    interp->in_error_handler = 0;

    switch (node->data.resume.resume_type) {
        case 0: /* RESUME */
            interp->pc = interp->error_pc;
            break;
        case 1: /* RESUME NEXT */
            interp->pc = interp->error_pc + 1;
            break;
        case 2: /* RESUME label */
            if (node->data.resume.lineno >= 0)
                interp->pc = program_find_lineno(interp->prog, node->data.resume.lineno);
            else
                interp->pc = program_find_label(interp->prog, node->data.resume.label);
            break;
    }
}

/* ---- ERROR n statement ---- */
static void exec_error_stmt(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.error_stmt.code);
    int code = (int)fbval_to_long(&val);
    fbval_release(&val);
    fb_error((FBErrorCode)code, node->line, NULL);
}

/* ---- LPRINT ---- */
static void exec_lprint(Interpreter* interp, ASTNode* node) {
    /* Same as PRINT but to stdout (no printer support) */
    exec_print(interp, node);
}

/* ---- OPTION BASE ---- */
static void exec_option_base(Interpreter* interp, ASTNode* node) {
    interp->option_base = node->data.option_base.base;
}

/* ================================================================
 *  Phase 5 — File I/O
 * ================================================================ */

/* Map AST open mode int (0–4) to FBFileMode enum */
static FBFileMode ast_mode_to_filemode(int m) {
    switch (m) {
        case 0: return FMODE_INPUT;
        case 1: return FMODE_OUTPUT;
        case 2: return FMODE_APPEND;
        case 3: return FMODE_RANDOM;
        case 4: return FMODE_BINARY;
        default: return FMODE_RANDOM;
    }
}

static void exec_open(Interpreter* interp, ASTNode* node) {
    FBValue fname_val = eval_expr(interp, node->data.open.filename);
    const char* filename = (fname_val.type == FB_STRING && fname_val.as.str)
                           ? fname_val.as.str->data : "";

    FBValue fnum_val = eval_expr(interp, node->data.open.filenum);
    int filenum = (int)fbval_to_long(&fnum_val);
    fbval_release(&fnum_val);

    int reclen = 128;
    if (node->data.open.reclen) {
        FBValue rv = eval_expr(interp, node->data.open.reclen);
        reclen = (int)fbval_to_long(&rv);
        fbval_release(&rv);
    }

    FBFileMode  mode   = ast_mode_to_filemode(node->data.open.mode);
    FBFileAccess access = (FBFileAccess)node->data.open.access_mode;
    FBFileLock   lock   = (FBFileLock)node->data.open.lock_mode;

    int err = fb_file_open(&interp->file_table, filenum, filename,
                            mode, access, lock, reclen);
    if (err) {
        fb_error(err, node->line, filename);
    }
    fbval_release(&fname_val);
}

static void exec_close(Interpreter* interp, ASTNode* node) {
    if (node->data.close.close_all || node->data.close.filenum_count == 0) {
        fb_filetable_close_all(&interp->file_table);
    } else {
        for (int i = 0; i < node->data.close.filenum_count; i++) {
            int fnum = node->data.close.filenums[i];
            fb_file_close(&interp->file_table, fnum);
        }
    }
}

/* Helper: write a formatted string to a file */
static void file_print_str(FBFileTable* ft, FBFile* f, const char* s) {
    fb_file_write_bytes(ft, f, s, strlen(s));
}

/* PRINT # — same logic as screen exec_print, but directed to file */
static void exec_print_to_file(Interpreter* interp, ASTNode* node, int filenum) {
    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    FBFileTable* ft = &interp->file_table;

    for (int i = 0; i < node->data.print.item_count; i++) {
        ASTNode* item = node->data.print.items[i];

        if (item->kind == AST_FUNC_CALL) {
            if (strcasecmp(item->data.func_call.name, "SPC") == 0 &&
                item->data.func_call.arg_count > 0) {
                FBValue v = eval_expr(interp, item->data.func_call.args[0]);
                int n = (int)fbval_to_long(&v);
                fbval_release(&v);
                for (int j = 0; j < n; j++) fb_file_write_char(ft, f, ' ');
                goto file_next_sep;
            }
            if (strcasecmp(item->data.func_call.name, "TAB") == 0 &&
                item->data.func_call.arg_count > 0) {
                FBValue v = eval_expr(interp, item->data.func_call.args[0]);
                int target = (int)fbval_to_long(&v);
                fbval_release(&v);
                long pos = ft->ops->tell(f->handle);
                while (pos < target - 1) {
                    fb_file_write_char(ft, f, ' ');
                    pos++;
                }
                goto file_next_sep;
            }
        }

        {
            FBValue val = eval_expr(interp, item);
            char* text = fbval_format_print(&val);
            file_print_str(ft, f, text);
            fb_free(text);
            fbval_release(&val);
        }

file_next_sep:
        if (node->data.print.separators) {
            int sep = node->data.print.separators[i];
            if (sep == TOK_COMMA) {
                /* Tab zone of 14 */
                long pos = ft->ops->tell(f->handle);
                int col = (int)(pos % 14);
                int pad = 14 - col;
                for (int j = 0; j < pad; j++) fb_file_write_char(ft, f, ' ');
            }
        }
    }

    if (!node->data.print.trailing_sep) {
        file_print_str(ft, f, "\r\n");
    }
}

/* WRITE # */
static void exec_write_to_file(Interpreter* interp, ASTNode* node, int filenum) {
    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    FBFileTable* ft = &interp->file_table;

    for (int i = 0; i < node->data.write_stmt.item_count; i++) {
        if (i > 0) fb_file_write_char(ft, f, ',');
        FBValue val = eval_expr(interp, node->data.write_stmt.items[i]);
        if (val.type == FB_STRING) {
            fb_file_write_char(ft, f, '"');
            file_print_str(ft, f, val.as.str ? val.as.str->data : "");
            fb_file_write_char(ft, f, '"');
        } else if (val.type == FB_INTEGER || val.type == FB_LONG) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", (int)fbval_to_long(&val));
            file_print_str(ft, f, buf);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", fbval_to_double(&val));
            file_print_str(ft, f, buf);
        }
        fbval_release(&val);
    }
    file_print_str(ft, f, "\r\n");
}

/* INPUT # — read comma-delimited values from file */
static void exec_input_from_file(Interpreter* interp, ASTNode* node, int filenum) {
    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    FBFileTable* ft = &interp->file_table;

    for (int i = 0; i < node->data.input.var_count; i++) {
        char field[4096];
        int pos = 0;
        int ch;

        /* Skip leading whitespace and newlines */
        while ((ch = fb_file_read_char(ft, f)) != -1) {
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        }

        if (ch == -1) {
            fb_error(FB_ERR_INPUT_PAST_END, node->line, NULL);
            return;
        }

        if (ch == '"') {
            /* Quoted string — read until closing quote */
            while ((ch = fb_file_read_char(ft, f)) != -1 && ch != '"') {
                if (pos < (int)sizeof(field) - 1) field[pos++] = (char)ch;
            }
            /* Skip past comma or newline after closing quote */
            ch = fb_file_read_char(ft, f);
            if (ch == '\r') ch = fb_file_read_char(ft, f); /* skip CR */
        } else {
            /* Unquoted value — read until comma or newline */
            if (pos < (int)sizeof(field) - 1) field[pos++] = (char)ch;
            while ((ch = fb_file_read_char(ft, f)) != -1) {
                if (ch == ',' || ch == '\r' || ch == '\n') {
                    if (ch == '\r') {
                        int next = fb_file_read_char(ft, f);
                        if (next != '\n' && next != -1) {
                            /* push back — seek */
                            long p2 = ft->ops->tell(f->handle);
                            ft->ops->seek(f->handle, p2 - 1, 0);
                        }
                    }
                    break;
                }
                if (pos < (int)sizeof(field) - 1) field[pos++] = (char)ch;
            }
        }
        field[pos] = '\0';

        /* Assign to variable via resolve_lvalue if array/udt, else use symbol */
        ASTNode* var_node = node->data.input.vars[i];
        FBValue* target_ptr = NULL;
        FBType target_type = FB_SINGLE;

        if (var_node->kind == AST_ARRAY_ACCESS || var_node->kind == AST_UDT_MEMBER) {
            target_ptr = resolve_lvalue(interp, var_node);
            if (target_ptr) target_type = target_ptr->type;
        }
        if (!target_ptr) {
            Symbol* sym = resolve_or_create_var(interp, var_node);
            if (sym->is_ref && sym->ref_addr) {
                target_ptr = sym->ref_addr;
            } else {
                target_ptr = &sym->value;
            }
            target_type = sym->type;
        }

        if (target_type == FB_STRING) {
            fbval_release(target_ptr);
            *target_ptr = fbval_string_from_cstr(field);
        } else {
            double val = atof(field);
            FBValue numval = fbval_double(val);
            FBValue coerced = fbval_coerce(&numval, target_type);
            fbval_release(target_ptr);
            *target_ptr = coerced;
        }
    }
}

/* LINE INPUT # — read entire line from file */
static void exec_line_input_from_file(Interpreter* interp, ASTNode* node, int filenum) {
    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    FBFileTable* ft = &interp->file_table;

    char buf[32768];
    int pos = 0;
    int ch;
    while ((ch = fb_file_read_char(ft, f)) != -1 && ch != '\n') {
        if (ch == '\r') continue;
        if (pos < (int)sizeof(buf) - 1) buf[pos++] = (char)ch;
    }
    buf[pos] = '\0';

    if (node->data.input.var_count > 0) {
        ASTNode* var_node = node->data.input.vars[0];
        FBValue* target_ptr = NULL;

        if (var_node->kind == AST_ARRAY_ACCESS || var_node->kind == AST_UDT_MEMBER) {
            target_ptr = resolve_lvalue(interp, var_node);
        }
        if (!target_ptr) {
            Symbol* sym = resolve_or_create_var(interp, var_node);
            if (sym->is_ref && sym->ref_addr) target_ptr = sym->ref_addr;
            else target_ptr = &sym->value;
        }
        fbval_release(target_ptr);
        *target_ptr = fbval_string_from_cstr(buf);
    }
}

/* GET # — random-access or binary read */
static void exec_get_file(Interpreter* interp, ASTNode* node) {
    FBValue fnum_val = eval_expr(interp, node->data.file_io.filenum);
    int filenum = (int)fbval_to_long(&fnum_val);
    fbval_release(&fnum_val);

    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    FBFileTable* ft = &interp->file_table;

    long recnum = -1;
    if (node->data.file_io.recnum) {
        FBValue rv = eval_expr(interp, node->data.file_io.recnum);
        recnum = (long)fbval_to_long(&rv);
        fbval_release(&rv);
    }

    if (f->mode == FMODE_RANDOM) {
        fb_file_get_record(ft, f, recnum);

        /* If a variable was specified, copy the record buffer into it */
        if (node->data.file_io.var) {
            Symbol* sym = resolve_or_create_var(interp, node->data.file_io.var);
            if (sym->type == FB_STRING || sym->value.type == FB_STRING) {
                fbval_release(&sym->value);
                sym->value = fbval_string(fbstr_new(f->record_buffer, f->reclen));
            } else if (sym->value.type == FB_UDT) {
                /* Copy buffer into UDT fields */
                int type_id = sym->value.as.udt.type_id;
                UDTDef* def = &interp->udt_registry.types[type_id];
                FBValue* fields = sym->value.as.udt.fields;
                int offset = 0;
                for (int i = 0; i < def->field_count && offset < f->reclen; i++) {
                    FBType ftype = def->fields[i].type;
                    if (ftype == FB_STRING) {
                        int flen = def->fields[i].fixed_str_len;
                        if (flen <= 0) flen = 1;
                        if (offset + flen > f->reclen) flen = f->reclen - offset;
                        fbval_release(&fields[i]);
                        fields[i] = fbval_string(fbstr_new(f->record_buffer + offset, flen));
                        offset += flen;
                    } else if (ftype == FB_INTEGER) {
                        int16_t v = 0;
                        if (offset + 2 <= f->reclen) memcpy(&v, f->record_buffer + offset, 2);
                        fbval_release(&fields[i]);
                        fields[i] = fbval_int(v);
                        offset += 2;
                    } else if (ftype == FB_LONG) {
                        int32_t v = 0;
                        if (offset + 4 <= f->reclen) memcpy(&v, f->record_buffer + offset, 4);
                        fbval_release(&fields[i]);
                        fields[i] = fbval_long(v);
                        offset += 4;
                    } else if (ftype == FB_SINGLE) {
                        float v = 0;
                        if (offset + 4 <= f->reclen) memcpy(&v, f->record_buffer + offset, 4);
                        fbval_release(&fields[i]);
                        fields[i] = fbval_single(v);
                        offset += 4;
                    } else if (ftype == FB_DOUBLE) {
                        double v = 0;
                        if (offset + 8 <= f->reclen) memcpy(&v, f->record_buffer + offset, 8);
                        fbval_release(&fields[i]);
                        fields[i] = fbval_double(v);
                        offset += 8;
                    }
                }
            }
        }

        /* Update FIELD-mapped variables */
        for (int i = 0; i < f->field_count; i++) {
            Symbol* sym = scope_lookup(interp->current_scope, f->field_map[i].var_name);
            if (!sym) sym = scope_lookup(interp->global_scope, f->field_map[i].var_name);
            if (sym && sym->type == FB_STRING) {
                int off = f->field_map[i].offset;
                int wid = f->field_map[i].width;
                if (off + wid <= f->reclen) {
                    fbval_release(&sym->value);
                    sym->value = fbval_string(fbstr_new(f->record_buffer + off, wid));
                }
            }
        }
    } else if (f->mode == FMODE_BINARY) {
        /* Binary GET */
        if (recnum > 0) {
            ft->ops->seek(f->handle, recnum - 1, 0);
        }
        if (node->data.file_io.var) {
            Symbol* sym = resolve_or_create_var(interp, node->data.file_io.var);
            switch (sym->type) {
                case FB_INTEGER: {
                    int16_t v = 0;
                    fb_file_read_bytes(ft, f, &v, 2);
                    fbval_release(&sym->value);
                    sym->value = fbval_int(v);
                    break;
                }
                case FB_LONG: {
                    int32_t v = 0;
                    fb_file_read_bytes(ft, f, &v, 4);
                    fbval_release(&sym->value);
                    sym->value = fbval_long(v);
                    break;
                }
                case FB_SINGLE: {
                    float v = 0;
                    fb_file_read_bytes(ft, f, &v, 4);
                    fbval_release(&sym->value);
                    sym->value = fbval_single(v);
                    break;
                }
                case FB_DOUBLE: {
                    double v = 0;
                    fb_file_read_bytes(ft, f, &v, 8);
                    fbval_release(&sym->value);
                    sym->value = fbval_double(v);
                    break;
                }
                case FB_STRING: {
                    int len = sym->value.as.str ? sym->value.as.str->len : 0;
                    if (len > 0) {
                        char* buf = fb_malloc(len);
                        fb_file_read_bytes(ft, f, buf, len);
                        fbval_release(&sym->value);
                        sym->value = fbval_string(fbstr_new(buf, len));
                        fb_free(buf);
                    }
                    break;
                }
                default: break;
            }
        }
    }
}

/* PUT # — random-access or binary write */
static void exec_put_file(Interpreter* interp, ASTNode* node) {
    FBValue fnum_val = eval_expr(interp, node->data.file_io.filenum);
    int filenum = (int)fbval_to_long(&fnum_val);
    fbval_release(&fnum_val);

    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    FBFileTable* ft = &interp->file_table;

    long recnum = -1;
    if (node->data.file_io.recnum) {
        FBValue rv = eval_expr(interp, node->data.file_io.recnum);
        recnum = (long)fbval_to_long(&rv);
        fbval_release(&rv);
    }

    if (f->mode == FMODE_RANDOM) {
        /* Build record buffer from FIELD variables, then overlay with var if given */
        memset(f->record_buffer, ' ', f->reclen);  /* space-fill per FB convention */

        /* Copy FIELD-mapped variables to buffer */
        for (int i = 0; i < f->field_count; i++) {
            Symbol* sym = scope_lookup(interp->current_scope, f->field_map[i].var_name);
            if (!sym) sym = scope_lookup(interp->global_scope, f->field_map[i].var_name);
            if (sym && sym->type == FB_STRING && sym->value.as.str) {
                int off = f->field_map[i].offset;
                int wid = f->field_map[i].width;
                int slen = sym->value.as.str->len;
                int cplen = slen < wid ? slen : wid;
                if (off + wid <= f->reclen) {
                    memcpy(f->record_buffer + off, sym->value.as.str->data, cplen);
                }
            }
        }

        /* If a variable was specified, copy it into the buffer */
        if (node->data.file_io.var) {
            Symbol* sym = resolve_or_create_var(interp, node->data.file_io.var);
            if (sym->type == FB_STRING || sym->value.type == FB_STRING) {
                if (sym->value.as.str) {
                    int slen = sym->value.as.str->len;
                    int cplen = slen < f->reclen ? slen : f->reclen;
                    memset(f->record_buffer, ' ', f->reclen);
                    memcpy(f->record_buffer, sym->value.as.str->data, cplen);
                }
            } else if (sym->value.type == FB_UDT) {
                /* Serialize UDT fields to buffer */
                int type_id = sym->value.as.udt.type_id;
                UDTDef* def = &interp->udt_registry.types[type_id];
                FBValue* fields = sym->value.as.udt.fields;
                int offset = 0;
                memset(f->record_buffer, 0, f->reclen);
                for (int i = 0; i < def->field_count && offset < f->reclen; i++) {
                    FBType ftype = def->fields[i].type;
                    if (ftype == FB_STRING) {
                        int flen = def->fields[i].fixed_str_len;
                        if (flen <= 0) flen = 1;
                        if (offset + flen > f->reclen) break;
                        /* space-fill then copy */
                        memset(f->record_buffer + offset, ' ', flen);
                        if (fields[i].as.str) {
                            int slen = fields[i].as.str->len;
                            int cplen = slen < flen ? slen : flen;
                            memcpy(f->record_buffer + offset, fields[i].as.str->data, cplen);
                        }
                        offset += flen;
                    } else if (ftype == FB_INTEGER) {
                        int16_t v = (int16_t)fbval_to_long(&fields[i]);
                        if (offset + 2 <= f->reclen) memcpy(f->record_buffer + offset, &v, 2);
                        offset += 2;
                    } else if (ftype == FB_LONG) {
                        int32_t v = (int32_t)fbval_to_long(&fields[i]);
                        if (offset + 4 <= f->reclen) memcpy(f->record_buffer + offset, &v, 4);
                        offset += 4;
                    } else if (ftype == FB_SINGLE) {
                        float v = (float)fbval_to_double(&fields[i]);
                        if (offset + 4 <= f->reclen) memcpy(f->record_buffer + offset, &v, 4);
                        offset += 4;
                    } else if (ftype == FB_DOUBLE) {
                        double v = fbval_to_double(&fields[i]);
                        if (offset + 8 <= f->reclen) memcpy(f->record_buffer + offset, &v, 8);
                        offset += 8;
                    }
                }
            }
        }

        fb_file_put_record(ft, f, recnum);
    } else if (f->mode == FMODE_BINARY) {
        /* Binary PUT */
        if (recnum > 0) {
            ft->ops->seek(f->handle, recnum - 1, 0);
        }
        if (node->data.file_io.var) {
            Symbol* sym = resolve_or_create_var(interp, node->data.file_io.var);
            switch (sym->type) {
                case FB_INTEGER: {
                    int16_t v = (int16_t)fbval_to_long(&sym->value);
                    fb_file_write_bytes(ft, f, &v, 2);
                    break;
                }
                case FB_LONG: {
                    int32_t v = (int32_t)fbval_to_long(&sym->value);
                    fb_file_write_bytes(ft, f, &v, 4);
                    break;
                }
                case FB_SINGLE: {
                    float v = (float)fbval_to_double(&sym->value);
                    fb_file_write_bytes(ft, f, &v, 4);
                    break;
                }
                case FB_DOUBLE: {
                    double v = fbval_to_double(&sym->value);
                    fb_file_write_bytes(ft, f, &v, 8);
                    break;
                }
                case FB_STRING: {
                    if (sym->value.as.str) {
                        fb_file_write_bytes(ft, f, sym->value.as.str->data,
                                            sym->value.as.str->len);
                    }
                    break;
                }
                default: break;
            }
        }
        ft->ops->flush(f->handle);
    }
}

/* SEEK statement */
static void exec_seek_stmt(Interpreter* interp, ASTNode* node) {
    FBValue fnum_val = eval_expr(interp, node->data.seek.filenum);
    int filenum = (int)fbval_to_long(&fnum_val);
    fbval_release(&fnum_val);

    FBValue pos_val = eval_expr(interp, node->data.seek.position);
    long pos = (long)fbval_to_long(&pos_val);
    fbval_release(&pos_val);

    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }
    fb_file_seek_set(&interp->file_table, f, pos);
}

/* FIELD statement */
static void exec_field_stmt(Interpreter* interp, ASTNode* node) {
    FBValue fnum_val = eval_expr(interp, node->data.field.filenum);
    int filenum = (int)fbval_to_long(&fnum_val);
    fbval_release(&fnum_val);

    FBFile* f = fb_file_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_NUMBER, node->line, NULL);
        return;
    }

    /* Free previous FIELD mapping */
    fb_free(f->field_map);
    f->field_count = node->data.field.field_count;
    f->field_map = fb_calloc(f->field_count, sizeof(*f->field_map));

    int offset = 0;
    for (int i = 0; i < f->field_count; i++) {
        f->field_map[i].width  = node->data.field.fields[i].width;
        f->field_map[i].offset = offset;
        strncpy(f->field_map[i].var_name, node->data.field.fields[i].var_name,
                sizeof(f->field_map[i].var_name) - 1);
        offset += f->field_map[i].width;

        /* Ensure the variable exists as a string */
        Symbol* sym = scope_lookup(interp->current_scope, f->field_map[i].var_name);
        if (!sym) {
            sym = scope_insert(interp->current_scope, f->field_map[i].var_name,
                               SYM_VARIABLE, FB_STRING);
        }
    }
}

/* NAME...AS */
static void exec_name_stmt(Interpreter* interp, ASTNode* node) {
    FBValue old_val = eval_expr(interp, node->data.name_stmt.old_name);
    FBValue new_val = eval_expr(interp, node->data.name_stmt.new_name);
    const char* old_path = (old_val.type == FB_STRING && old_val.as.str) ? old_val.as.str->data : "";
    const char* new_path = (new_val.type == FB_STRING && new_val.as.str) ? new_val.as.str->data : "";

    if (interp->file_table.ops->rename(old_path, new_path) != 0) {
        fb_error(FB_ERR_FILE_NOT_FOUND, node->line, old_path);
    }
    fbval_release(&old_val);
    fbval_release(&new_val);
}

/* KILL */
static void exec_kill(Interpreter* interp, ASTNode* node) {
    FBValue fname = eval_expr(interp, node->data.single_arg.arg);
    const char* path = (fname.type == FB_STRING && fname.as.str) ? fname.as.str->data : "";

    if (interp->file_table.ops->remove(path) != 0) {
        fb_error(FB_ERR_FILE_NOT_FOUND, node->line, path);
    }
    fbval_release(&fname);
}

/* LOCK / UNLOCK — no-op for now */
static void exec_lock_stmt(Interpreter* interp, ASTNode* node) {
    (void)interp; (void)node;
}

static void exec_unlock_stmt(Interpreter* interp, ASTNode* node) {
    (void)interp; (void)node;
}

/* RESET — close all files */
static void exec_reset(Interpreter* interp, ASTNode* node) {
    (void)node;
    fb_filetable_close_all(&interp->file_table);
}

/* ======== Block executor ======== */

static void exec_block(Interpreter* interp, ASTNode** stmts, int count) {
    for (int i = 0; i < count && interp->running; i++) {
        if (interp->exit_for || interp->exit_do || interp->exit_while ||
            interp->exit_sub || interp->exit_function || interp->exit_procedure) break;
        exec_statement(interp, stmts[i]);
    }
}

/* ================================================================
 *  Phase 7 — System Interface (via portable FBSysOps abstraction)
 * ================================================================ */

/* ---- SHELL ---- */
static void exec_shell(Interpreter* interp, ASTNode* node) {
    if (node->data.single_arg.arg) {
        FBValue cmd = eval_expr(interp, node->data.single_arg.arg);
        const char* cmdstr = (cmd.type == FB_STRING && cmd.as.str)
                             ? cmd.as.str->data : "";
        interp->sys_ops->shell_exec(cmdstr);
        fbval_release(&cmd);
    } else {
        interp->sys_ops->shell_interactive();
    }
}

/* ---- ENVIRON statement ---- */
static void exec_environ_set(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.environ_stmt.expr);
    const char* str = (val.type == FB_STRING && val.as.str)
                      ? val.as.str->data : "";
    const char* eq = strchr(str, '=');
    if (!eq) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, node->line,
                 "ENVIRON requires NAME=VALUE");
        fbval_release(&val);
        return;
    }
    char name[256];
    int namelen = (int)(eq - str);
    if (namelen >= 256) namelen = 255;
    memcpy(name, str, namelen);
    name[namelen] = '\0';
    interp->sys_ops->environ_set(name, eq + 1);
    fbval_release(&val);
}

/* ---- CHDIR / MKDIR / RMDIR ---- */
static void exec_chdir(Interpreter* interp, ASTNode* node) {
    FBValue path = eval_expr(interp, node->data.single_arg.arg);
    const char* p = (path.type == FB_STRING && path.as.str)
                    ? path.as.str->data : "";
    if (interp->sys_ops->chdir(p) != 0)
        fb_error(FB_ERR_PATH_NOT_FOUND, node->line, p);
    fbval_release(&path);
}

static void exec_mkdir(Interpreter* interp, ASTNode* node) {
    FBValue path = eval_expr(interp, node->data.single_arg.arg);
    const char* p = (path.type == FB_STRING && path.as.str)
                    ? path.as.str->data : "";
    if (interp->sys_ops->mkdir(p) != 0)
        fb_error(FB_ERR_PATH_FILE_ACCESS_ERROR, node->line, p);
    fbval_release(&path);
}

static void exec_rmdir(Interpreter* interp, ASTNode* node) {
    FBValue path = eval_expr(interp, node->data.single_arg.arg);
    const char* p = (path.type == FB_STRING && path.as.str)
                    ? path.as.str->data : "";
    if (interp->sys_ops->rmdir(p) != 0)
        fb_error(FB_ERR_PATH_FILE_ACCESS_ERROR, node->line, p);
    fbval_release(&path);
}

/* ---- CLEAR ---- */
static void exec_clear(Interpreter* interp, ASTNode* node) {
    (void)node;
    scope_clear(interp->global_scope);
    interp->current_scope = interp->global_scope;
    interp->data_ptr = 0;
    interp->err_code = 0;
    interp->err_line = 0;
    interp->on_error_target = -1;
    interp->in_error_handler = 0;
}

/* ---- POKE ---- */
static void exec_poke(Interpreter* interp, ASTNode* node) {
    FBValue addr = eval_expr(interp, node->data.poke_out.addr);
    FBValue val  = eval_expr(interp, node->data.poke_out.val);
    long a = fbval_to_long(&addr);
    int  v = (int)fbval_to_long(&val);
    interp->sys_ops->poke(a, v);
    fbval_release(&addr);
    fbval_release(&val);
}

/* ======== Statement dispatcher ======== */

static void exec_statement(Interpreter* interp, ASTNode* stmt) {
    if (!stmt) return;

    if (interp->trace_on) {
        printf("[%d]", stmt->line);
    }

    switch (stmt->kind) {
        case AST_PRINT:       exec_print(interp, stmt); break;
        case AST_LET:         exec_let(interp, stmt); break;
        case AST_IF:          exec_if(interp, stmt); break;
        case AST_FOR:         exec_for(interp, stmt); break;
        case AST_WHILE:       exec_while(interp, stmt); break;
        case AST_DO_LOOP:     exec_do_loop(interp, stmt); break;
        case AST_GOTO:        exec_goto(interp, stmt); break;
        case AST_GOSUB:       exec_gosub(interp, stmt); break;
        case AST_RETURN:      exec_return(interp, stmt); break;
        case AST_END:
        case AST_STOP:
        case AST_SYSTEM:      exec_end(interp, stmt); break;
        case AST_DIM:         exec_dim(interp, stmt); break;
        case AST_CONST_DECL:  exec_const(interp, stmt); break;
        case AST_DEFTYPE:     exec_deftype(interp, stmt); break;
        case AST_SELECT_CASE: exec_select_case(interp, stmt); break;
        case AST_INPUT:       exec_input(interp, stmt); break;
        case AST_LINE_INPUT:  exec_line_input(interp, stmt); break;
        case AST_WRITE_STMT:  exec_write(interp, stmt); break;
        case AST_CLS:         exec_cls(interp, stmt); break;
        case AST_LOCATE:      exec_locate(interp, stmt); break;
        case AST_COLOR:       exec_color(interp, stmt); break;
        case AST_BEEP:        exec_beep(interp, stmt); break;
        case AST_VIEW_PRINT:  exec_view_print(interp, stmt); break;
        case AST_WIDTH_STMT:  exec_width(interp, stmt); break;
        case AST_DATA:        /* no-op at runtime */ break;
        case AST_READ:        exec_read(interp, stmt); break;
        case AST_RESTORE:     exec_restore(interp, stmt); break;
        case AST_PRINT_USING: exec_print_using_stmt(interp, stmt); break;
        case AST_RANDOMIZE:   exec_randomize(interp, stmt); break;
        case AST_SWAP:        exec_swap(interp, stmt); break;
        case AST_EXIT:        exec_exit(interp, stmt); break;
        case AST_ON_GOTO:
        case AST_ON_GOSUB:    exec_on_branch(interp, stmt); break;
        case AST_CALL:        exec_call(interp, stmt); break;
        case AST_SUB_DEF:
        case AST_FUNCTION_DEF: /* skip at runtime; registered in proc table */ break;
        case AST_DEF_FN:      exec_def_fn(interp, stmt); break;
        case AST_DECLARE:     /* no-op */ break;
        case AST_ON_ERROR:    exec_on_error(interp, stmt); break;
        case AST_RESUME:      exec_resume(interp, stmt); break;
        case AST_ERROR_STMT:  exec_error_stmt(interp, stmt); break;
        case AST_TRON:        exec_tron(interp); break;
        case AST_TROFF:       exec_troff(interp); break;
        case AST_LPRINT:      exec_lprint(interp, stmt); break;
        case AST_OPTION_BASE: exec_option_base(interp, stmt); break;
        case AST_SLEEP:       exec_sleep(interp, stmt); break;
        case AST_REM:
        case AST_LABEL_DEF:   /* no-op */ break;
        case AST_SHARED_STMT: exec_shared_stmt(interp, stmt); break;
        case AST_STATIC_STMT: exec_static_stmt(interp, stmt); break;
        case AST_REDIM:       exec_redim(interp, stmt); break;
        case AST_ERASE:       exec_erase(interp, stmt); break;
        case AST_TYPE_DEF:    exec_type_def(interp, stmt); break;
        case AST_LSET:        exec_lset(interp, stmt); break;
        case AST_RSET:        exec_rset(interp, stmt); break;
        case AST_OPEN:        exec_open(interp, stmt); break;
        case AST_CLOSE:       exec_close(interp, stmt); break;
        case AST_GET_FILE:    exec_get_file(interp, stmt); break;
        case AST_PUT_FILE:    exec_put_file(interp, stmt); break;
        case AST_SEEK_STMT:   exec_seek_stmt(interp, stmt); break;
        case AST_FIELD_STMT:  exec_field_stmt(interp, stmt); break;
        case AST_NAME_STMT:   exec_name_stmt(interp, stmt); break;
        case AST_KILL_STMT:   exec_kill(interp, stmt); break;
        case AST_LOCK_STMT:   exec_lock_stmt(interp, stmt); break;
        case AST_UNLOCK_STMT: exec_unlock_stmt(interp, stmt); break;
        case AST_RESET:       exec_reset(interp, stmt); break;
        /* Phase 7 — System Interface */
        case AST_SHELL:       exec_shell(interp, stmt); break;
        case AST_ENVIRON_STMT: exec_environ_set(interp, stmt); break;
        case AST_CHDIR:       exec_chdir(interp, stmt); break;
        case AST_MKDIR:       exec_mkdir(interp, stmt); break;
        case AST_RMDIR:       exec_rmdir(interp, stmt); break;
        case AST_CLEAR:       exec_clear(interp, stmt); break;
        case AST_POKE:        exec_poke(interp, stmt); break;
        default:
            /* Silently skip unimplemented statements */
            break;
    }
}

/* ======== Public API ======== */

void interp_init(Interpreter* interp, Program* prog) {
    memset(interp, 0, sizeof(Interpreter));
    interp->prog = prog;
    interp->global_scope = scope_new(NULL);
    interp->current_scope = interp->global_scope;
    interp->on_error_target = -1;
    interp->rnd_seed = 327680;
    interp->print_col = 1;
    interp->screen_width = 80;
    interp->screen_height = 25;
    interp->scroll_top = 1;
    interp->scroll_bottom = 25;
    interp->current_fg = 7;
    interp->current_bg = 0;
    callstack_init(&interp->call_stack);
    udt_registry_init(&interp->udt_registry);
    fb_filetable_init(&interp->file_table, NULL);
    interp->sys_ops = fb_sysops_default();
}

void interp_set_command_line(Interpreter* interp, int argc, char** argv) {
    size_t total = 0;
    for (int i = 2; i < argc; i++)   /* skip program name (argv[0]) and .bas file (argv[1]) */
        total += strlen(argv[i]) + 1;
    interp->command_line = fb_malloc(total + 1);
    interp->command_line[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) strcat(interp->command_line, " ");
        strcat(interp->command_line, argv[i]);
    }
    /* FB COMMAND$ is uppercase */
    for (char* p = interp->command_line; *p; p++)
        *p = (char)toupper((unsigned char)*p);
}

void interp_run(Interpreter* interp) {
    interp->pc = 0;
    interp->running = 1;

    /* Install error handler for ON ERROR support */
    extern void fb_set_error_handler(void (*)(FBErrorCode, int, const char*));

    while (interp->running && interp->pc < interp->prog->stmt_count) {
        ASTNode* stmt = interp->prog->statements[interp->pc];
        int old_pc = interp->pc;

        /* Set up longjmp for ON ERROR */
        if (interp->on_error_target >= 0) {
            interp_has_jmp = 1;
            int err = setjmp(interp_error_jmp);
            if (err != 0) {
                /* Error occurred; jump to error handler */
                interp->in_error_handler = 1;
                interp->error_pc = old_pc;
                interp->err_code = err;
                /* ERL returns the BASIC line number, not source file line */
                interp->err_line = find_basic_lineno(interp->prog, old_pc);
                interp->pc = interp->on_error_target;
                continue;
            }
            fb_set_error_handler(interp_error_handler);
        } else {
            interp_has_jmp = 0;
            fb_set_error_handler(NULL);
        }

        exec_statement(interp, stmt);

        /* If pc was not changed by a jump, advance */
        if (interp->pc == old_pc) {
            interp->pc++;
        }
    }

    interp_has_jmp = 0;
    fb_set_error_handler(NULL);
}

void interp_free(Interpreter* interp) {
    fb_filetable_close_all(&interp->file_table);
    scope_free(interp->global_scope);
    fb_free(interp->def_fns);
    fb_free(interp->command_line);
    interp->global_scope = NULL;
    interp->current_scope = NULL;
}

FBValue interp_eval(Interpreter* interp, ASTNode* node) {
    return eval_expr(interp, node);
}
