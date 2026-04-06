/*
 * coerce.c — Type coercion engine
 */
#include "coerce.h"
#include "error.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

FBType fb_promote_type(FBType a, FBType b) {
    if (a == FB_STRING && b == FB_STRING) return FB_STRING;
    if (a == FB_STRING || b == FB_STRING) return (FBType)-1; /* Type mismatch */
    if (a == FB_DOUBLE || b == FB_DOUBLE) return FB_DOUBLE;
    if (a == FB_SINGLE || b == FB_SINGLE) return FB_SINGLE;
    if (a == FB_LONG   || b == FB_LONG)   return FB_LONG;
    return FB_INTEGER;
}

FBValue fbval_coerce(const FBValue* v, FBType target) {
    if (v->type == target) return fbval_copy(v);

    if (v->type == FB_STRING || target == FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, 0, "Cannot coerce between string and numeric");
        return fbval_int(0);
    }

    double d = fbval_to_double(v);

    switch (target) {
        case FB_INTEGER: {
            int32_t l = (int32_t)rint(d);
            if (l < -32768 || l > 32767) {
                fb_error(FB_ERR_OVERFLOW, 0, NULL);
                return fbval_int(0);
            }
            return fbval_int((int16_t)l);
        }
        case FB_LONG: {
            int32_t l = (int32_t)rint(d);
            return fbval_long(l);
        }
        case FB_SINGLE: return fbval_single((float)d);
        case FB_DOUBLE: return fbval_double(d);
        default: return fbval_int(0);
    }
}

FBValue fbval_binary_op(const FBValue* a, const FBValue* b, TokenKind op) {
    /* String concatenation */
    if (a->type == FB_STRING && b->type == FB_STRING && op == TOK_PLUS) {
        FBString* result = fbstr_concat(a->as.str, b->as.str);
        FBValue val;
        val.type = FB_STRING;
        val.as.str = result;
        return val;
    }

    if (a->type == FB_STRING || b->type == FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, 0, "Type mismatch in binary operation");
        return fbval_int(0);
    }

    /* Integer division and MOD: round operands to long first */
    if (op == TOK_BACKSLASH || op == TOK_KW_MOD) {
        int32_t la = fbval_to_long(a);
        int32_t lb = fbval_to_long(b);
        if (lb == 0) {
            fb_error(FB_ERR_DIVISION_BY_ZERO, 0, NULL);
            return fbval_int(0);
        }
        if (op == TOK_BACKSLASH) {
            int32_t result = la / lb;
            if (result >= -32768 && result <= 32767)
                return fbval_int((int16_t)result);
            return fbval_long(result);
        } else {
            int32_t result = la % lb;
            if (result >= -32768 && result <= 32767)
                return fbval_int((int16_t)result);
            return fbval_long(result);
        }
    }

    FBType result_type = fb_promote_type(a->type, b->type);
    double da = fbval_to_double(a);
    double db = fbval_to_double(b);
    double result;

    switch (op) {
        case TOK_PLUS:  result = da + db; break;
        case TOK_MINUS: result = da - db; break;
        case TOK_STAR:  result = da * db; break;
        case TOK_SLASH:
            if (db == 0.0) {
                fb_error(FB_ERR_DIVISION_BY_ZERO, 0, NULL);
                return fbval_int(0);
            }
            result = da / db;
            /* Regular division always produces at least SINGLE */
            if (result_type == FB_INTEGER || result_type == FB_LONG)
                result_type = FB_SINGLE;
            break;
        case TOK_CARET:
            result = pow(da, db);
            break;
        default:
            result = 0.0;
            break;
    }

    switch (result_type) {
        case FB_INTEGER: {
            int32_t l = (int32_t)rint(result);
            if (l >= -32768 && l <= 32767)
                return fbval_int((int16_t)l);
            return fbval_long(l);
        }
        case FB_LONG:   return fbval_long((int32_t)rint(result));
        case FB_SINGLE: return fbval_single((float)result);
        case FB_DOUBLE: return fbval_double(result);
        default:        return fbval_double(result);
    }
}

FBValue fbval_unary_op(const FBValue* v, TokenKind op) {
    if (v->type == FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, 0, "Type mismatch in unary operation");
        return fbval_int(0);
    }

    if (op == TOK_MINUS) {
        switch (v->type) {
            case FB_INTEGER: return fbval_int(-v->as.ival);
            case FB_LONG:    return fbval_long(-v->as.lval);
            case FB_SINGLE:  return fbval_single(-v->as.sval);
            case FB_DOUBLE:  return fbval_double(-v->as.dval);
            default: break;
        }
    } else if (op == TOK_PLUS) {
        return fbval_copy(v);
    } else if (op == TOK_KW_NOT) {
        int32_t l = fbval_to_long(v);
        int32_t result = ~l;
        if (result >= -32768 && result <= 32767)
            return fbval_int((int16_t)result);
        return fbval_long(result);
    }

    return fbval_copy(v);
}

FBValue fbval_compare(const FBValue* a, const FBValue* b, TokenKind op) {
    int cmp;

    if (a->type == FB_STRING && b->type == FB_STRING) {
        cmp = fbstr_compare(a->as.str, b->as.str);
    } else if (a->type == FB_STRING || b->type == FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, 0, "Cannot compare string and number");
        return fbval_int(FB_FALSE);
    } else {
        double da = fbval_to_double(a);
        double db = fbval_to_double(b);
        if (da < db) cmp = -1;
        else if (da > db) cmp = 1;
        else cmp = 0;
    }

    int result;
    switch (op) {
        case TOK_EQ: result = (cmp == 0); break;
        case TOK_NE: result = (cmp != 0); break;
        case TOK_LT: result = (cmp < 0); break;
        case TOK_GT: result = (cmp > 0); break;
        case TOK_LE: result = (cmp <= 0); break;
        case TOK_GE: result = (cmp >= 0); break;
        default:     result = 0; break;
    }

    return fbval_int(result ? FB_TRUE : FB_FALSE);
}

FBValue fbval_logical_op(const FBValue* a, const FBValue* b, TokenKind op) {
    if (a->type == FB_STRING || b->type == FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, 0, "Type mismatch in logical operation");
        return fbval_int(0);
    }

    int32_t la = fbval_to_long(a);
    int32_t lb = fbval_to_long(b);
    int32_t result;

    switch (op) {
        case TOK_KW_AND: result = la & lb; break;
        case TOK_KW_OR:  result = la | lb; break;
        case TOK_KW_XOR: result = la ^ lb; break;
        case TOK_KW_EQV: result = ~(la ^ lb); break;
        case TOK_KW_IMP: result = (~la) | lb; break;
        default:         result = 0; break;
    }

    if (result >= -32768 && result <= 32767)
        return fbval_int((int16_t)result);
    return fbval_long(result);
}
