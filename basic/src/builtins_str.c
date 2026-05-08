/*
 * builtins_str.c — String built-in functions for FBasic interpreter
 */
#include "builtins_str.h"
#include "platform.h"
#include "error.h"
#include "value.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ---- String function implementations ---- */

static FBValue fn_left(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "LEFT$");
    int n = (int)fbval_to_long(&args[1]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "LEFT$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    if (n > args[0].as.str->len) n = args[0].as.str->len;
    FBString* s = fbstr_mid(args[0].as.str, 0, n);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_right(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "RIGHT$");
    int n = (int)fbval_to_long(&args[1]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "RIGHT$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    int slen = args[0].as.str->len;
    if (n > slen) n = slen;
    FBString* s = fbstr_mid(args[0].as.str, slen - n, n);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_mid(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "MID$");
    int start = (int)fbval_to_long(&args[1]) - 1; /* 1-based to 0-based */
    if (start < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "MID$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    int slen = args[0].as.str->len;
    int n = (argc >= 3) ? (int)fbval_to_long(&args[2]) : slen - start;
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "MID$");
    if (start >= slen) return fbval_string_from_cstr("");
    if (start + n > slen) n = slen - start;
    FBString* s = fbstr_mid(args[0].as.str, start, n);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_len(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "LEN");
    return fbval_long(args[0].as.str ? args[0].as.str->len : 0);
}

static FBValue fn_instr(FBValue* args, int argc, int line) {
    int start_pos;
    const char* haystack;
    const char* needle;

    if (argc == 2) {
        start_pos = 0;
        if (args[0].type != FB_STRING || args[1].type != FB_STRING)
            fb_error(FB_ERR_TYPE_MISMATCH, line, "INSTR");
        haystack = args[0].as.str ? args[0].as.str->data : "";
        needle = args[1].as.str ? args[1].as.str->data : "";
    } else {
        start_pos = (int)fbval_to_long(&args[0]) - 1;
        if (args[1].type != FB_STRING || args[2].type != FB_STRING)
            fb_error(FB_ERR_TYPE_MISMATCH, line, "INSTR");
        haystack = args[1].as.str ? args[1].as.str->data : "";
        needle = args[2].as.str ? args[2].as.str->data : "";
    }

    if (start_pos < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "INSTR");
    if (needle[0] == '\0') return fbval_int((int16_t)(start_pos + 1));
    if (start_pos >= (int)strlen(haystack)) return fbval_int(0);

    const char* found = strstr(haystack + start_pos, needle);
    if (!found) return fbval_int(0);
    return fbval_int((int16_t)(found - haystack + 1));
}

static FBValue fn_chr(FBValue* args, int argc, int line) {
    (void)argc;
    int32_t code = fbval_to_long(&args[0]);
    if (code < 0 || code > 255)
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CHR$");
    char buf[2] = { (char)code, '\0' };
    return fbval_string_from_cstr(buf);
}

static FBValue fn_asc(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len == 0)
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "ASC of empty string");
    return fbval_int((int16_t)(unsigned char)args[0].as.str->data[0]);
}

static FBValue fn_str(FBValue* args, int argc, int line) {
    (void)argc; (void)line;
    char* fmt = fbval_format_print(&args[0]);
    /* Remove trailing space */
    int len = (int)strlen(fmt);
    if (len > 0 && fmt[len - 1] == ' ') fmt[len - 1] = '\0';
    FBValue result = fbval_string_from_cstr(fmt);
    fb_free(fmt);
    return result;
}

static FBValue fn_val(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING) {
        fb_error(FB_ERR_TYPE_MISMATCH, line, "VAL");
        return fbval_double(0);
    }
    const char* s = args[0].as.str ? args[0].as.str->data : "";
    /* Skip leading whitespace */
    while (*s == ' ') s++;
    double v = atof(s);
    return fbval_double(v);
}

static FBValue fn_ucase(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "UCASE$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    FBString* src = args[0].as.str;
    FBString* s = fbstr_new(src->data, src->len);
    for (int i = 0; i < s->len; i++) s->data[i] = (char)toupper(s->data[i]);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_lcase(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "LCASE$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    FBString* src = args[0].as.str;
    FBString* s = fbstr_new(src->data, src->len);
    for (int i = 0; i < s->len; i++) s->data[i] = (char)tolower(s->data[i]);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_ltrim(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "LTRIM$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    const char* p = args[0].as.str->data;
    while (*p == ' ') p++;
    return fbval_string_from_cstr(p);
}

static FBValue fn_rtrim(FBValue* args, int argc, int line) {
    (void)argc;
    if (args[0].type != FB_STRING)
        fb_error(FB_ERR_TYPE_MISMATCH, line, "RTRIM$");
    if (!args[0].as.str) return fbval_string_from_cstr("");
    int len = args[0].as.str->len;
    while (len > 0 && args[0].as.str->data[len - 1] == ' ') len--;
    FBString* s = fbstr_mid(args[0].as.str, 0, len);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_string_fill(FBValue* args, int argc, int line) {
    (void)argc;
    int n = (int)fbval_to_long(&args[0]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "STRING$");
    char fill_char;
    if (args[1].type == FB_STRING) {
        fill_char = (args[1].as.str && args[1].as.str->len > 0)
                    ? args[1].as.str->data[0] : ' ';
    } else {
        fill_char = (char)fbval_to_long(&args[1]);
    }
    char* buf = fb_malloc(n + 1);
    memset(buf, fill_char, n);
    buf[n] = '\0';
    FBString* s = fbstr_new(buf, n);
    fb_free(buf);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_space(FBValue* args, int argc, int line) {
    (void)argc;
    int n = (int)fbval_to_long(&args[0]);
    if (n < 0) fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "SPACE$");
    char* buf = fb_malloc(n + 1);
    memset(buf, ' ', n);
    buf[n] = '\0';
    FBString* s = fbstr_new(buf, n);
    fb_free(buf);
    FBValue val;
    val.type = FB_STRING;
    val.as.str = s;
    return val;
}

static FBValue fn_hex(FBValue* args, int argc, int line) {
    (void)argc; (void)line;
    int32_t v = fbval_to_long(&args[0]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%X", (unsigned int)v);
    return fbval_string_from_cstr(buf);
}

static FBValue fn_oct(FBValue* args, int argc, int line) {
    (void)argc; (void)line;
    int32_t v = fbval_to_long(&args[0]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%o", (unsigned int)v);
    return fbval_string_from_cstr(buf);
}

/* ---- Lookup table ---- */

typedef FBValue (*StrFuncFn)(FBValue* args, int argc, int line);

typedef struct {
    const char* name;
    int         min_args;
    int         max_args;
    StrFuncFn   func;
} StrFuncEntry;

static const StrFuncEntry str_funcs[] = {
    { "LEFT$",    2, 2, fn_left        },
    { "RIGHT$",   2, 2, fn_right       },
    { "MID$",     2, 3, fn_mid         },
    { "LEN",      1, 1, fn_len         },
    { "INSTR",    2, 3, fn_instr       },
    { "CHR$",     1, 1, fn_chr         },
    { "ASC",      1, 1, fn_asc         },
    { "STR$",     1, 1, fn_str         },
    { "VAL",      1, 1, fn_val         },
    { "UCASE$",   1, 1, fn_ucase       },
    { "LCASE$",   1, 1, fn_lcase       },
    { "LTRIM$",   1, 1, fn_ltrim       },
    { "RTRIM$",   1, 1, fn_rtrim       },
    { "STRING$",  2, 2, fn_string_fill },
    { "SPACE$",   1, 1, fn_space       },
    { "HEX$",     1, 1, fn_hex         },
    { "OCT$",     1, 1, fn_oct         },
    { NULL,       0, 0, NULL           }
};

int builtin_str_lookup(const char* name) {
    for (int i = 0; str_funcs[i].name; i++) {
        if (strcasecmp(name, str_funcs[i].name) == 0) return 1;
    }
    return 0;
}

FBValue builtin_str_call(const char* name, FBValue* args, int argc, int line) {
    for (int i = 0; str_funcs[i].name; i++) {
        if (strcasecmp(name, str_funcs[i].name) == 0) {
            if (argc < str_funcs[i].min_args) {
                fb_error(FB_ERR_ARGUMENT_COUNT_MISMATCH, line, name);
                return fbval_int(0);
            }
            return str_funcs[i].func(args, argc, line);
        }
    }
    fb_error(FB_ERR_UNDEFINED_FUNCTION, line, name);
    return fbval_int(0);
}
