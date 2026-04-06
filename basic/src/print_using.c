/*
 * print_using.c — PRINT USING format engine for FBasic interpreter
 *
 * Supported format codes:
 *   Numeric:  # . , + - $$ ** **$ ^^^^
 *   String:   ! \..\ &
 *   Escape:   _ (literal next char)
 */
#include "print_using.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Internal helpers ---- */

/* Parse a numeric format spec starting at fmt[pos].
 * Returns the length of the spec consumed (0 if not numeric). */
static int scan_numeric_spec(const char* fmt, int pos, int len,
                             int* out_before_dot, int* out_after_dot,
                             int* out_has_dot, int* out_has_comma,
                             int* out_plus_start, int* out_plus_end,
                             int* out_minus_end, int* out_dollar,
                             int* out_star_fill, int* out_exponent) {
    int p = pos;
    *out_before_dot = *out_after_dot = 0;
    *out_has_dot = *out_has_comma = 0;
    *out_plus_start = *out_plus_end = 0;
    *out_minus_end = *out_dollar = *out_star_fill = *out_exponent = 0;

    /* Leading + */
    if (p < len && fmt[p] == '+') { *out_plus_start = 1; p++; }

    /* $$ */
    if (p + 1 < len && fmt[p] == '$' && fmt[p + 1] == '$') {
        *out_dollar = 1; p += 2;
    }
    /* ** or **$ */
    if (p + 1 < len && fmt[p] == '*' && fmt[p + 1] == '*') {
        *out_star_fill = 1; p += 2;
        if (p < len && fmt[p] == '$') { *out_dollar = 1; p++; }
    }

    /* Count '#' before dot */
    int start_digits = p;
    while (p < len && fmt[p] == '#') { (*out_before_dot)++; p++; }
    while (p < len && fmt[p] == ',') { *out_has_comma = 1; p++; }
    while (p < len && fmt[p] == '#') { (*out_before_dot)++; p++; }

    if (*out_before_dot == 0 && !*out_dollar && !*out_star_fill && !*out_plus_start)
        return 0; /* not a numeric spec */

    /* Decimal point */
    if (p < len && fmt[p] == '.') {
        *out_has_dot = 1; p++;
        while (p < len && fmt[p] == '#') { (*out_after_dot)++; p++; }
    }

    /* Exponent ^^^^ */
    if (p + 3 < len && fmt[p] == '^' && fmt[p+1] == '^' &&
        fmt[p+2] == '^' && fmt[p+3] == '^') {
        *out_exponent = 1; p += 4;
    }

    /* Trailing + or - */
    if (p < len && fmt[p] == '+') { *out_plus_end = 1; p++; }
    else if (p < len && fmt[p] == '-') { *out_minus_end = 1; p++; }

    (void)start_digits;
    return p - pos;
}

/* Format a number given the parsed spec */
static char* format_number(int before_dot, int after_dot,
                           int has_dot, int has_comma,
                           int plus_start, int plus_end,
                           int minus_end, int dollar,
                           int star_fill, int exponent,
                           double value) {
    char buf[128];
    int is_neg = (value < 0.0);
    double absval = fabs(value);

    if (exponent) {
        /* Scientific notation */
        int total_digits = before_dot + (has_dot ? after_dot : 0);
        if (total_digits < 1) total_digits = 1;
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%.*E", total_digits - 1 + after_dot, absval);
        /* Fit into field */
        int field_width = before_dot + (has_dot ? 1 + after_dot : 0) + 5; /* E+xx */
        char sign = is_neg ? '-' : (plus_start || plus_end ? '+' : ' ');
        snprintf(buf, sizeof(buf), "%c%*s", sign, field_width, tmp);
        return strdup(buf);
    }

    /* Regular formatting */
    char num_str[64];
    snprintf(num_str, sizeof(num_str), "%.*f", after_dot, absval);

    /* Split integer and fraction parts */
    char* dot_ptr = strchr(num_str, '.');
    char int_part[64] = "";
    char frac_part[64] = "";
    if (dot_ptr) {
        int ilen = (int)(dot_ptr - num_str);
        memcpy(int_part, num_str, ilen);
        int_part[ilen] = '\0';
        strcpy(frac_part, dot_ptr + 1);
    } else {
        strcpy(int_part, num_str);
    }

    /* Insert commas */
    char with_commas[128];
    int int_len = (int)strlen(int_part);
    if (has_comma && int_len > 3) {
        int pos_out = 0;
        int first_group = int_len % 3;
        if (first_group == 0) first_group = 3;
        for (int i = 0; i < int_len; i++) {
            if (i > 0 && (int_len - i) % 3 == 0)
                with_commas[pos_out++] = ',';
            with_commas[pos_out++] = int_part[i];
        }
        with_commas[pos_out] = '\0';
    } else {
        strcpy(with_commas, int_part);
    }

    /* Build the numeric field */
    char field[128];
    int fi = 0;

    /* Determine field width */
    int int_width = before_dot;
    if (dollar) int_width++; /* extra space for $ */
    if (plus_start) int_width++; /* extra space for sign */

    int padded_len = (int)strlen(with_commas);
    char fill = star_fill ? '*' : ' ';

    /* Build: [fill/pad] [sign/dollar] [digits] [.] [frac] [sign] */
    int total_int_space = int_width;
    int num_pad = total_int_space - padded_len;
    if (is_neg || plus_start) num_pad--;
    if (dollar) num_pad--;

    int overflow = 0;
    if (num_pad < 0) {
        overflow = 1;
        num_pad = 0;
    }

    /* Fill */
    for (int i = 0; i < num_pad; i++) field[fi++] = fill;

    /* Sign */
    if (plus_start) {
        field[fi++] = is_neg ? '-' : '+';
    } else if (is_neg) {
        field[fi++] = '-';
    }

    /* Dollar */
    if (dollar) field[fi++] = '$';

    /* Integer digits */
    for (int i = 0; i < padded_len; i++) field[fi++] = with_commas[i];

    /* Decimal portion */
    if (has_dot) {
        field[fi++] = '.';
        int flen = (int)strlen(frac_part);
        for (int i = 0; i < after_dot; i++) {
            field[fi++] = (i < flen) ? frac_part[i] : '0';
        }
    }

    /* Trailing sign */
    if (plus_end) field[fi++] = is_neg ? '-' : '+';
    else if (minus_end) field[fi++] = is_neg ? '-' : ' ';

    field[fi] = '\0';

    /* Overflow: prefix with % */
    if (overflow) {
        char result[132];
        result[0] = '%';
        strcpy(result + 1, field);
        return strdup(result);
    }

    return strdup(field);
}

static int scan_string_spec(const char* fmt, int pos, int len, int* out_width) {
    if (pos >= len) return 0;

    if (fmt[pos] == '!') {
        *out_width = 1;
        return 1;
    }
    if (fmt[pos] == '&') {
        *out_width = -1; /* variable width */
        return 1;
    }
    if (fmt[pos] == '\\') {
        /* Count spaces between backslashes */
        int p = pos + 1;
        int spaces = 0;
        while (p < len && fmt[p] != '\\') { spaces++; p++; }
        if (p < len && fmt[p] == '\\') {
            *out_width = spaces + 2; /* including both backslashes */
            return p - pos + 1;
        }
    }
    return 0;
}

static char* format_string_val(int width, const char* str_value) {
    int slen = str_value ? (int)strlen(str_value) : 0;

    if (width == 1) {
        /* ! — first char only */
        char buf[2] = { slen > 0 ? str_value[0] : ' ', '\0' };
        return strdup(buf);
    }
    if (width == -1) {
        /* & — entire string */
        return strdup(str_value ? str_value : "");
    }

    /* Fixed width: left-justify, pad with spaces */
    char* buf = malloc(width + 1);
    int i;
    for (i = 0; i < width && i < slen; i++) buf[i] = str_value[i];
    for (; i < width; i++) buf[i] = ' ';
    buf[width] = '\0';
    return buf;
}

/* ---- Public API ---- */

void exec_print_using(const char* fmt, FBValue* values, int value_count) {
    int flen = fmt ? (int)strlen(fmt) : 0;
    int fpos = 0;
    int vi = 0;
    int first_pass = 1;

    while (vi < value_count) {
        /* If we've consumed the format string, recycle */
        if (fpos >= flen) {
            fpos = 0;
            first_pass = 0;
        }

        /* While there are literal characters, print them */
        while (fpos < flen) {
            /* Escape: _ means next char is literal */
            if (fmt[fpos] == '_' && fpos + 1 < flen) {
                putchar(fmt[fpos + 1]);
                fpos += 2;
                continue;
            }

            /* Try numeric spec */
            int bd, ad, hd, hc, ps, pe, me, dl, sf, ex;
            int nlen = scan_numeric_spec(fmt, fpos, flen,
                                         &bd, &ad, &hd, &hc,
                                         &ps, &pe, &me, &dl, &sf, &ex);
            if (nlen > 0) {
                /* Format current value as number */
                if (vi < value_count) {
                    double num = fbval_to_double(&values[vi]);
                    char* result = format_number(bd, ad, hd, hc,
                                                 ps, pe, me, dl, sf, ex, num);
                    fputs(result, stdout);
                    free(result);
                    vi++;
                }
                fpos += nlen;
                break; /* get next value */
            }

            /* Try string spec */
            int sw;
            int slen = scan_string_spec(fmt, fpos, flen, &sw);
            if (slen > 0) {
                if (vi < value_count) {
                    const char* sv = "";
                    if (values[vi].type == FB_STRING && values[vi].as.str)
                        sv = values[vi].as.str->data;
                    char* result = format_string_val(sw, sv);
                    fputs(result, stdout);
                    free(result);
                    vi++;
                }
                fpos += slen;
                break;
            }

            /* Literal character */
            putchar(fmt[fpos]);
            fpos++;
        }

        (void)first_pass;
    }

    /* Print any remaining literal chars in format string */
    while (fpos < flen) {
        if (fmt[fpos] == '_' && fpos + 1 < flen) {
            putchar(fmt[fpos + 1]);
            fpos += 2;
        } else {
            putchar(fmt[fpos]);
            fpos++;
        }
    }

    putchar('\n');
    fflush(stdout);
}

/* format_print_using — same as exec_print_using but returns a heap-allocated string */
char* format_print_using(const char* fmt, FBValue* values, int value_count) {
    int flen = fmt ? (int)strlen(fmt) : 0;
    int fpos = 0;
    int vi = 0;

    /* Dynamic buffer */
    int cap = 256, len = 0;
    char* buf = malloc(cap);
    buf[0] = '\0';

#define BUF_APPEND(s) do { \
    int _sl = (int)strlen(s); \
    while (len + _sl + 1 > cap) { cap *= 2; buf = realloc(buf, cap); } \
    memcpy(buf + len, s, _sl); len += _sl; buf[len] = '\0'; \
} while(0)

#define BUF_PUTCHAR(c) do { \
    if (len + 2 > cap) { cap *= 2; buf = realloc(buf, cap); } \
    buf[len++] = (c); buf[len] = '\0'; \
} while(0)

    while (vi < value_count) {
        if (fpos >= flen) fpos = 0;

        while (fpos < flen) {
            if (fmt[fpos] == '_' && fpos + 1 < flen) {
                BUF_PUTCHAR(fmt[fpos + 1]);
                fpos += 2;
                continue;
            }

            int bd, ad, hd, hc, ps, pe, me, dl, sf, ex;
            int nlen = scan_numeric_spec(fmt, fpos, flen,
                                         &bd, &ad, &hd, &hc,
                                         &ps, &pe, &me, &dl, &sf, &ex);
            if (nlen > 0) {
                if (vi < value_count) {
                    double num = fbval_to_double(&values[vi]);
                    char* result = format_number(bd, ad, hd, hc,
                                                 ps, pe, me, dl, sf, ex, num);
                    BUF_APPEND(result);
                    free(result);
                    vi++;
                }
                fpos += nlen;
                break;
            }

            int sw;
            int slen = scan_string_spec(fmt, fpos, flen, &sw);
            if (slen > 0) {
                if (vi < value_count) {
                    const char* sv = "";
                    if (values[vi].type == FB_STRING && values[vi].as.str)
                        sv = values[vi].as.str->data;
                    char* result = format_string_val(sw, sv);
                    BUF_APPEND(result);
                    free(result);
                    vi++;
                }
                fpos += slen;
                break;
            }

            BUF_PUTCHAR(fmt[fpos]);
            fpos++;
        }
    }

    while (fpos < flen) {
        if (fmt[fpos] == '_' && fpos + 1 < flen) {
            BUF_PUTCHAR(fmt[fpos + 1]);
            fpos += 2;
        } else {
            BUF_PUTCHAR(fmt[fpos]);
            fpos++;
        }
    }

#undef BUF_APPEND
#undef BUF_PUTCHAR
    return buf;
}
