/*
 * value.c — FBValue tagged union + FBString ref-counted strings
 */
#include "value.h"
#include "system_api.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* --- FBString --- */

FBString* fbstr_new(const char* text, int32_t len) {
    FBString* s = fb_malloc(sizeof(FBString));
    s->len = len;
    s->capacity = len + 1;
    s->data = fb_malloc(s->capacity);
    if (text && len > 0)
        memcpy(s->data, text, len);
    s->data[len] = '\0';
    s->refcount = 1;
    return s;
}

FBString* fbstr_empty(void) {
    return fbstr_new("", 0);
}

void fbstr_ref(FBString* s) {
    if (s) s->refcount++;
}

void fbstr_unref(FBString* s) {
    if (!s) return;
    s->refcount--;
    if (s->refcount <= 0) {
        fb_free(s->data);
        fb_free(s);
    }
}

FBString* fbstr_cow(FBString* s) {
    if (!s) return fbstr_empty();
    if (s->refcount == 1) return s;
    /* Make a copy */
    FBString* copy = fbstr_new(s->data, s->len);
    fbstr_unref(s);
    return copy;
}

FBString* fbstr_concat(const FBString* a, const FBString* b) {
    int32_t new_len = a->len + b->len;
    FBString* s = fb_malloc(sizeof(FBString));
    s->len = new_len;
    s->capacity = new_len + 1;
    s->data = fb_malloc(s->capacity);
    if (a->len > 0) memcpy(s->data, a->data, a->len);
    if (b->len > 0) memcpy(s->data + a->len, b->data, b->len);
    s->data[new_len] = '\0';
    s->refcount = 1;
    return s;
}

FBString* fbstr_mid(const FBString* s, int32_t start, int32_t len) {
    if (start < 0) start = 0;
    if (start >= s->len) return fbstr_empty();
    if (start + len > s->len) len = s->len - start;
    if (len <= 0) return fbstr_empty();
    return fbstr_new(s->data + start, len);
}

int fbstr_compare(const FBString* a, const FBString* b) {
    int32_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->data, b->data, min_len);
    if (cmp != 0) return cmp;
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    return 0;
}

/* --- FBValue --- */

FBValue fbval_int(int16_t v) {
    FBValue val;
    val.type = FB_INTEGER;
    val.as.ival = v;
    return val;
}

FBValue fbval_long(int32_t v) {
    FBValue val;
    val.type = FB_LONG;
    val.as.lval = v;
    return val;
}

FBValue fbval_single(float v) {
    FBValue val;
    val.type = FB_SINGLE;
    val.as.sval = v;
    return val;
}

FBValue fbval_double(double v) {
    FBValue val;
    val.type = FB_DOUBLE;
    val.as.dval = v;
    return val;
}

FBValue fbval_string(FBString* s) {
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    if (s) fbstr_ref(s);
    return val;
}

FBValue fbval_string_from_cstr(const char* text) {
    FBString* s = fbstr_new(text, text ? (int32_t)strlen(text) : 0);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

void fbval_release(FBValue* v) {
    if (v->type == FB_STRING) {
        fbstr_unref(v->as.str);
        v->as.str = NULL;
    }
    /* Note: FB_UDT fields are NOT released here — the UDT instance
       lifecycle is managed by the symbol table or the caller explicitly. */
}

FBValue fbval_copy(const FBValue* v) {
    FBValue copy = *v;
    if (copy.type == FB_STRING && copy.as.str) {
        fbstr_ref(copy.as.str);
    }
    /* For FB_UDT: shallow copy — the fields pointer is shared.
       Deep copy must be done explicitly when needed (e.g. UDT assignment). */
    return copy;
}

double fbval_to_double(const FBValue* v) {
    switch (v->type) {
        case FB_INTEGER: return (double)v->as.ival;
        case FB_LONG:    return (double)v->as.lval;
        case FB_SINGLE:  return (double)v->as.sval;
        case FB_DOUBLE:  return v->as.dval;
        case FB_STRING:  return 0.0; /* Type mismatch would be caught earlier */
        default:         return 0.0;
    }
    return 0.0;
}

int32_t fbval_to_long(const FBValue* v) {
    switch (v->type) {
        case FB_INTEGER: return (int32_t)v->as.ival;
        case FB_LONG:    return v->as.lval;
        case FB_SINGLE: {
            /* Banker's rounding */
            double d = (double)v->as.sval;
            double rounded = rint(d); /* rint uses current rounding mode (round-to-even by default) */
            return (int32_t)rounded;
        }
        case FB_DOUBLE: {
            double rounded = rint(v->as.dval);
            return (int32_t)rounded;
        }
        case FB_STRING:  return 0;
        default:         return 0;
    }
    return 0;
}

int fbval_is_true(const FBValue* v) {
    switch (v->type) {
        case FB_INTEGER: return v->as.ival != 0;
        case FB_LONG:    return v->as.lval != 0;
        case FB_SINGLE:  return v->as.sval != 0.0f;
        case FB_DOUBLE:  return v->as.dval != 0.0;
        case FB_STRING:  return 0; /* Strings are not boolean in FB */
        default:         return 0;
    }
    return 0;
}

char* fbval_format_print(const FBValue* v) {
    char buf[64];
    switch (v->type) {
        case FB_INTEGER:
            if (v->as.ival >= 0)
                snprintf(buf, sizeof(buf), " %d ", v->as.ival);
            else
                snprintf(buf, sizeof(buf), "-%d ", -v->as.ival);
            break;
        case FB_LONG:
            if (v->as.lval >= 0)
                snprintf(buf, sizeof(buf), " %d ", v->as.lval);
            else
                snprintf(buf, sizeof(buf), "-%d ", -v->as.lval);
            break;
        case FB_SINGLE: {
            float val = v->as.sval;
            if (val >= 0)
                snprintf(buf, sizeof(buf), " %g ", val);
            else
                snprintf(buf, sizeof(buf), "-%g ", -val);
            break;
        }
        case FB_DOUBLE: {
            double val = v->as.dval;
            if (val >= 0)
                snprintf(buf, sizeof(buf), " %.14g ", val);
            else
                snprintf(buf, sizeof(buf), "-%.14g ", -val);
            break;
        }
        case FB_STRING:
            if (v->as.str)
                return strdup(v->as.str->data);
            return strdup("");
        default:
            return strdup("");
    }
    return strdup(buf);
}

FBValue fbval_udt(int type_id, FBValue* fields) {
    FBValue val;
    val.type = FB_UDT;
    val.as.udt.type_id = type_id;
    val.as.udt.fields = fields;
    return val;
}

const char* fbtype_name(FBType t) {
    switch (t) {
        case FB_INTEGER: return "INTEGER";
        case FB_LONG:    return "LONG";
        case FB_SINGLE:  return "SINGLE";
        case FB_DOUBLE:  return "DOUBLE";
        case FB_STRING:  return "STRING";
        case FB_UDT:     return "TYPE";
    }
    return "UNKNOWN";
}
