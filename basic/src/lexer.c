/*
 * lexer.c — FBasic tokenizer
 */
#include "lexer.h"
#include "platform.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Keyword table entry */
typedef struct {
    const char* name;
    TokenKind   kind;
} KeywordEntry;

static const KeywordEntry keywords[] = {
    {"ABS", TOK_KW_ABS}, {"ACCESS", TOK_KW_ACCESS}, {"ALL", TOK_KW_ALL},
    {"AND", TOK_KW_AND},
    {"APPEND", TOK_KW_APPEND}, {"AS", TOK_KW_AS}, {"ASC", TOK_KW_ASC},
    {"ATN", TOK_KW_ATN},
    {"BASE", TOK_KW_BASE}, {"BEEP", TOK_KW_BEEP}, {"BINARY", TOK_KW_BINARY},
    {"BYVAL", TOK_KW_BYVAL},
    {"CALL", TOK_KW_CALL}, {"CASE", TOK_KW_CASE},
    {"CDBL", TOK_KW_CDBL}, {"CHAIN", TOK_KW_CHAIN}, {"CHR$", TOK_KW_CHR},
    {"CINT", TOK_KW_CINT}, {"CIRCLE", TOK_KW_CIRCLE},
    {"CLEAR", TOK_KW_CLEAR}, {"CLNG", TOK_KW_CLNG}, {"CLOSE", TOK_KW_CLOSE},
    {"CLS", TOK_KW_CLS}, {"COLOR", TOK_KW_COLOR}, {"COM", TOK_KW_COM},
    {"COMMAND$", TOK_KW_COMMAND}, {"COMMON", TOK_KW_COMMON},
    {"CONST", TOK_KW_CONST}, {"COS", TOK_KW_COS}, {"CSNG", TOK_KW_CSNG},
    {"CSRLIN", TOK_KW_CSRLIN}, {"CVD", TOK_KW_CVD}, {"CVDMBF", TOK_KW_CVDMBF},
    {"CVI", TOK_KW_CVI}, {"CVL", TOK_KW_CVL}, {"CVS", TOK_KW_CVS},
    {"CHDIR", TOK_KW_CHDIR},
    {"CVSMBF", TOK_KW_CVSMBF},
    {"DATA", TOK_KW_DATA}, {"DATE$", TOK_KW_DATE}, {"DECLARE", TOK_KW_DECLARE},
    {"DEF", TOK_KW_DEF}, {"DEFDBL", TOK_KW_DEFDBL}, {"DEFINT", TOK_KW_DEFINT},
    {"DEFLNG", TOK_KW_DEFLNG}, {"DEFSNG", TOK_KW_DEFSNG},
    {"DEFSTR", TOK_KW_DEFSTR}, {"DIM", TOK_KW_DIM}, {"DO", TOK_KW_DO},
    {"DOUBLE", TOK_KW_DOUBLE}, {"DRAW", TOK_KW_DRAW},
    {"ELSE", TOK_KW_ELSE}, {"ELSEIF", TOK_KW_ELSEIF}, {"END", TOK_KW_END},
    {"ENVIRON", TOK_KW_ENVIRON}, {"ENVIRON$", TOK_KW_ENVIRON},
    {"EOF", TOK_KW_EOF}, {"EQV", TOK_KW_EQV}, {"ERASE", TOK_KW_ERASE},
    {"ERDEV", TOK_KW_ERDEV}, {"ERR", TOK_KW_ERR}, {"ERL", TOK_KW_ERL},
    {"ERROR", TOK_KW_ERROR}, {"EXIT", TOK_KW_EXIT}, {"EXP", TOK_KW_EXP},
    {"FIELD", TOK_KW_FIELD}, {"FILEATTR", TOK_KW_FILEATTR},
    {"FILES", TOK_KW_FILES}, {"FIX", TOK_KW_FIX}, {"FN", TOK_KW_FN},
    {"FOR", TOK_KW_FOR}, {"FRE", TOK_KW_FRE}, {"FREEFILE", TOK_KW_FREEFILE},
    {"FUNCTION", TOK_KW_FUNCTION},
    {"GET", TOK_KW_GET}, {"GOSUB", TOK_KW_GOSUB}, {"GOTO", TOK_KW_GOTO},
    {"HEX$", TOK_KW_HEX}, {"IF", TOK_KW_IF}, {"IMP", TOK_KW_IMP},
    {"$INCLUDE", TOK_KW_INCLUDE},
    {"INKEY$", TOK_KW_INKEY}, {"INP", TOK_KW_INP}, {"INPUT", TOK_KW_INPUT},
    {"INPUT$", TOK_KW_INPUT}, {"INSTR", TOK_KW_INSTR}, {"INT", TOK_KW_INT},
    {"INTEGER", TOK_KW_INTEGER}, {"IOCTL", TOK_KW_IOCTL}, {"IS", TOK_KW_IS},
    {"KEY", TOK_KW_KEY}, {"KILL", TOK_KW_KILL},
    {"LBOUND", TOK_KW_LBOUND}, {"LCASE$", TOK_KW_LCASE}, {"LEFT$", TOK_KW_LEFT},
    {"LEN", TOK_KW_LEN}, {"LET", TOK_KW_LET}, {"LIB", TOK_KW_LIB},
    {"LINE", TOK_KW_LINE},
    {"LOC", TOK_KW_LOC}, {"LOCATE", TOK_KW_LOCATE}, {"LOCK", TOK_KW_LOCK},
    {"LOF", TOK_KW_LOF}, {"LOG", TOK_KW_LOG}, {"LONG", TOK_KW_LONG},
    {"LOOP", TOK_KW_LOOP}, {"LPRINT", TOK_KW_LPRINT}, {"LSET", TOK_KW_LSET},
    {"LTRIM$", TOK_KW_LTRIM},
    {"MID$", TOK_KW_MID}, {"MKD$", TOK_KW_MKD}, {"MKDIR", TOK_KW_MKDIR},
    {"MKDMBF$", TOK_KW_MKDMBF}, {"MKI$", TOK_KW_MKI}, {"MKL$", TOK_KW_MKL},
    {"MKS$", TOK_KW_MKS}, {"MKSMBF$", TOK_KW_MKSMBF}, {"MOD", TOK_KW_MOD},
    {"MERGE", TOK_KW_MERGE},
    {"NAME", TOK_KW_NAME}, {"NEXT", TOK_KW_NEXT}, {"NOT", TOK_KW_NOT},
    {"OCT$", TOK_KW_OCT}, {"OFF", TOK_KW_OFF}, {"ON", TOK_KW_ON},
    {"OPEN", TOK_KW_OPEN}, {"OPTION", TOK_KW_OPTION}, {"OR", TOK_KW_OR},
    {"OUT", TOK_KW_OUT}, {"OUTPUT", TOK_KW_OUTPUT},
    {"PAINT", TOK_KW_PAINT}, {"PALETTE", TOK_KW_PALETTE},
    {"PCOPY", TOK_KW_PCOPY}, {"PEEK", TOK_KW_PEEK}, {"PEN", TOK_KW_PEN},
    {"PLAY", TOK_KW_PLAY}, {"PMAP", TOK_KW_PMAP}, {"POINT", TOK_KW_POINT},
    {"POKE", TOK_KW_POKE}, {"POS", TOK_KW_POS}, {"PRESET", TOK_KW_PRESET},
    {"PRINT", TOK_KW_PRINT}, {"PSET", TOK_KW_PSET}, {"PUT", TOK_KW_PUT},
    {"RANDOM", TOK_KW_RANDOM}, {"RANDOMIZE", TOK_KW_RANDOMIZE}, {"READ", TOK_KW_READ},
    {"REDIM", TOK_KW_REDIM}, {"REM", TOK_KW_REM}, {"RESET", TOK_KW_RESET},
    {"RESTORE", TOK_KW_RESTORE}, {"RESUME", TOK_KW_RESUME},
    {"RETURN", TOK_KW_RETURN}, {"RIGHT$", TOK_KW_RIGHT},
    {"RMDIR", TOK_KW_RMDIR}, {"RND", TOK_KW_RND}, {"RSET", TOK_KW_RSET},
    {"RTRIM$", TOK_KW_RTRIM}, {"RUN", TOK_KW_RUN},
    {"SADD", TOK_KW_SADD}, {"SCREEN", TOK_KW_SCREEN}, {"SEEK", TOK_KW_SEEK},
    {"SEG", TOK_KW_SEG}, {"SELECT", TOK_KW_SELECT}, {"SGN", TOK_KW_SGN},
    {"SHARED", TOK_KW_SHARED}, {"SHELL", TOK_KW_SHELL}, {"SIN", TOK_KW_SIN},
    {"SINGLE", TOK_KW_SINGLE}, {"SLEEP", TOK_KW_SLEEP}, {"SOUND", TOK_KW_SOUND},
    {"SPACE$", TOK_KW_SPACE}, {"SPC", TOK_KW_SPC}, {"SQR", TOK_KW_SQR},
    {"STATIC", TOK_KW_STATIC}, {"STEP", TOK_KW_STEP}, {"STICK", TOK_KW_STICK},
    {"STOP", TOK_KW_STOP}, {"STR$", TOK_KW_STR}, {"STRIG", TOK_KW_STRIG},
    {"STRING", TOK_KW_STRING}, {"STRING$", TOK_KW_STRING},
    {"SUB", TOK_KW_SUB}, {"SWAP", TOK_KW_SWAP}, {"SYSTEM", TOK_KW_SYSTEM},
    {"TAB", TOK_KW_TAB}, {"TAN", TOK_KW_TAN}, {"THEN", TOK_KW_THEN},
    {"TIME$", TOK_KW_TIME}, {"TIMER", TOK_KW_TIMER}, {"TO", TOK_KW_TO},
    {"TROFF", TOK_KW_TROFF}, {"TRON", TOK_KW_TRON}, {"TYPE", TOK_KW_TYPE},
    {"UBOUND", TOK_KW_UBOUND}, {"UCASE$", TOK_KW_UCASE},
    {"UEVENT", TOK_KW_UEVENT}, {"UNLOCK", TOK_KW_UNLOCK},
    {"UNTIL", TOK_KW_UNTIL}, {"USING", TOK_KW_USING},
    {"VAL", TOK_KW_VAL}, {"VARPTR", TOK_KW_VARPTR}, {"VARSEG", TOK_KW_VARSEG},
    {"VIEW", TOK_KW_VIEW},
    {"WAIT", TOK_KW_WAIT}, {"WEND", TOK_KW_WEND}, {"WHILE", TOK_KW_WHILE},
    {"WIDTH", TOK_KW_WIDTH}, {"WINDOW", TOK_KW_WINDOW}, {"WRITE", TOK_KW_WRITE},
    {"XOR", TOK_KW_XOR},
};

static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);

static TokenKind lookup_keyword(const char* word) {
    /* Case-insensitive search */
    for (int i = 0; i < keyword_count; i++) {
        if (strcasecmp(word, keywords[i].name) == 0)
            return keywords[i].kind;
    }
    return TOK_IDENT; /* Not a keyword */
}

/* --- Lexer helpers --- */

static char lexer_peek(Lexer* lex) {
    if (lex->pos >= lex->source_len) return '\0';
    return lex->source[lex->pos];
}

static char lexer_peek_ahead(Lexer* lex, int offset) {
    int p = lex->pos + offset;
    if (p >= lex->source_len) return '\0';
    return lex->source[p];
}

static char lexer_advance(Lexer* lex) {
    char c = lex->source[lex->pos];
    lex->pos++;
    lex->col++;
    return c;
}

static void lexer_emit(Lexer* lex, Token tok) {
    if (lex->token_count >= lex->token_cap) {
        lex->token_cap = lex->token_cap ? lex->token_cap * 2 : 256;
        lex->tokens = fb_realloc(lex->tokens, lex->token_cap * sizeof(Token));
    }
    lex->tokens[lex->token_count++] = tok;
}

static Token make_token(Lexer* lex, TokenKind kind) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = kind;
    t.line = lex->line;
    t.col = lex->col;
    return t;
}

static void skip_whitespace(Lexer* lex) {
    while (lex->pos < lex->source_len) {
        char c = lex->source[lex->pos];
        if (c == ' ' || c == '\t') {
            lex->pos++;
            lex->col++;
        } else {
            break;
        }
    }
}

/* Handle line continuation: _ at end of line */
static int check_line_continuation(Lexer* lex) {
    if (lex->pos < lex->source_len && lex->source[lex->pos] == '_') {
        int p = lex->pos + 1;
        /* Skip spaces/tabs after _ */
        while (p < lex->source_len && (lex->source[p] == ' ' || lex->source[p] == '\t'))
            p++;
        /* Check for newline */
        if (p >= lex->source_len || lex->source[p] == '\r' || lex->source[p] == '\n') {
            /* Consume the _, spaces, and newline */
            lex->pos = p;
            if (lex->pos < lex->source_len && lex->source[lex->pos] == '\r') lex->pos++;
            if (lex->pos < lex->source_len && lex->source[lex->pos] == '\n') lex->pos++;
            lex->line++;
            lex->col = 1;
            /* Skip leading whitespace on next line */
            skip_whitespace(lex);
            return 1;
        }
    }
    return 0;
}

/* Scan a string literal */
static void scan_string(Lexer* lex) {
    int start_line = lex->line;
    int start_col = lex->col;
    lex->pos++; lex->col++; /* skip opening quote */

    char buf[32768];
    int len = 0;

    while (lex->pos < lex->source_len) {
        char c = lex->source[lex->pos];
        if (c == '"') {
            lex->pos++; lex->col++;
            /* Check for doubled quote ("") */
            if (lex->pos < lex->source_len && lex->source[lex->pos] == '"') {
                if (len < (int)sizeof(buf) - 1) buf[len++] = '"';
                lex->pos++; lex->col++;
            } else {
                /* End of string */
                break;
            }
        } else {
            if (len < (int)sizeof(buf) - 1) buf[len++] = c;
            lex->pos++; lex->col++;
        }
    }
    buf[len] = '\0';

    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = TOK_STRING_LIT;
    t.line = start_line;
    t.col = start_col;
    t.value.str.text = strdup(buf);
    t.value.str.length = len;
    lexer_emit(lex, t);
}

/* Scan a numeric literal */
static void scan_number(Lexer* lex) {
    int start_col = lex->col;
    char buf[256];
    int len = 0;
    int has_dot = 0;
    int has_exp = 0;
    int is_double_exp = 0; /* D exponent */
    TokenKind forced_type = TOK_COUNT; /* no forced type yet */

    /* Collect digits and optional decimal point */
    while (lex->pos < lex->source_len) {
        char c = lex->source[lex->pos];
        if (isdigit(c)) {
            buf[len++] = c;
            lex->pos++; lex->col++;
        } else if (c == '.' && !has_dot && !has_exp) {
            /* Check it's not a UDT member access (next char must be digit or end something) */
            char next = lexer_peek_ahead(lex, 1);
            if (!isdigit(next) && next != 'E' && next != 'e' && next != 'D' && next != 'd'
                && next != '#' && next != '!') {
                break; /* It's a dot operator, not decimal */
            }
            has_dot = 1;
            buf[len++] = c;
            lex->pos++; lex->col++;
        } else if ((c == 'E' || c == 'e') && !has_exp) {
            has_exp = 1;
            buf[len++] = 'E';
            lex->pos++; lex->col++;
            /* Optional sign */
            if (lex->pos < lex->source_len &&
                (lex->source[lex->pos] == '+' || lex->source[lex->pos] == '-')) {
                buf[len++] = lex->source[lex->pos];
                lex->pos++; lex->col++;
            }
        } else if ((c == 'D' || c == 'd') && !has_exp) {
            has_exp = 1;
            is_double_exp = 1;
            buf[len++] = 'E'; /* Store as E for strtod */
            lex->pos++; lex->col++;
            if (lex->pos < lex->source_len &&
                (lex->source[lex->pos] == '+' || lex->source[lex->pos] == '-')) {
                buf[len++] = lex->source[lex->pos];
                lex->pos++; lex->col++;
            }
        } else {
            break;
        }
    }
    buf[len] = '\0';

    /* Check for type suffix */
    if (lex->pos < lex->source_len) {
        char c = lex->source[lex->pos];
        if (c == '%') { forced_type = TOK_INTEGER_LIT; lex->pos++; lex->col++; }
        else if (c == '&') { forced_type = TOK_LONG_LIT; lex->pos++; lex->col++; }
        else if (c == '!') { forced_type = TOK_SINGLE_LIT; lex->pos++; lex->col++; }
        else if (c == '#') { forced_type = TOK_DOUBLE_LIT; lex->pos++; lex->col++; }
    }

    Token t;
    memset(&t, 0, sizeof(t));
    t.line = lex->line;
    t.col = start_col;

    if (forced_type != TOK_COUNT) {
        t.kind = forced_type;
    } else if (is_double_exp) {
        t.kind = TOK_DOUBLE_LIT;
    } else if (has_dot || has_exp) {
        t.kind = TOK_SINGLE_LIT;
    } else {
        /* Integer: check range */
        long val = strtol(buf, NULL, 10);
        if (val >= -32768 && val <= 32767) {
            t.kind = TOK_INTEGER_LIT;
        } else if (val >= -2147483648L && val <= 2147483647L) {
            t.kind = TOK_LONG_LIT;
        } else {
            t.kind = TOK_SINGLE_LIT;
        }
    }

    switch (t.kind) {
        case TOK_INTEGER_LIT: t.value.int_val = (int16_t)strtol(buf, NULL, 10); break;
        case TOK_LONG_LIT:    t.value.long_val = (int32_t)strtol(buf, NULL, 10); break;
        case TOK_SINGLE_LIT:  t.value.single_val = strtof(buf, NULL); break;
        case TOK_DOUBLE_LIT:  t.value.double_val = strtod(buf, NULL); break;
        default: break;
    }

    lexer_emit(lex, t);
}

/* Scan hex literal &H... */
static void scan_hex(Lexer* lex) {
    int start_col = lex->col;
    lex->pos += 2; lex->col += 2; /* skip &H */

    char buf[32];
    int len = 0;
    while (lex->pos < lex->source_len && isxdigit(lex->source[lex->pos])) {
        buf[len++] = lex->source[lex->pos];
        lex->pos++; lex->col++;
    }
    buf[len] = '\0';

    long val = strtol(buf, NULL, 16);

    Token t;
    memset(&t, 0, sizeof(t));
    t.line = lex->line;
    t.col = start_col;

    /* Check for suffix */
    if (lex->pos < lex->source_len && lex->source[lex->pos] == '&') {
        t.kind = TOK_LONG_LIT;
        t.value.long_val = (int32_t)val;
        lex->pos++; lex->col++;
    } else if (lex->pos < lex->source_len && lex->source[lex->pos] == '%') {
        t.kind = TOK_INTEGER_LIT;
        t.value.int_val = (int16_t)val;
        lex->pos++; lex->col++;
    } else if (val >= -32768 && val <= 65535) {
        /* Treat as integer if fits (FB treats &HFFFF as -1) */
        t.kind = TOK_INTEGER_LIT;
        t.value.int_val = (int16_t)(val > 32767 ? val - 65536 : val);
    } else {
        t.kind = TOK_LONG_LIT;
        t.value.long_val = (int32_t)val;
    }

    lexer_emit(lex, t);
}

/* Scan octal literal &O... or &[0-7] */
static void scan_octal(Lexer* lex) {
    int start_col = lex->col;
    lex->pos++; lex->col++; /* skip & */

    /* Skip optional O */
    if (lex->pos < lex->source_len &&
        (lex->source[lex->pos] == 'O' || lex->source[lex->pos] == 'o')) {
        lex->pos++; lex->col++;
    }

    char buf[32];
    int len = 0;
    while (lex->pos < lex->source_len && lex->source[lex->pos] >= '0' && lex->source[lex->pos] <= '7') {
        buf[len++] = lex->source[lex->pos];
        lex->pos++; lex->col++;
    }
    buf[len] = '\0';

    long val = strtol(buf, NULL, 8);

    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = (val >= -32768 && val <= 32767) ? TOK_INTEGER_LIT : TOK_LONG_LIT;
    t.line = lex->line;
    t.col = start_col;
    if (t.kind == TOK_INTEGER_LIT)
        t.value.int_val = (int16_t)val;
    else
        t.value.long_val = (int32_t)val;

    /* Check for suffix */
    if (lex->pos < lex->source_len && lex->source[lex->pos] == '&') {
        t.kind = TOK_LONG_LIT;
        t.value.long_val = (int32_t)val;
        lex->pos++; lex->col++;
    } else if (lex->pos < lex->source_len && lex->source[lex->pos] == '%') {
        t.kind = TOK_INTEGER_LIT;
        t.value.int_val = (int16_t)val;
        lex->pos++; lex->col++;
    }

    lexer_emit(lex, t);
}

/* Scan identifier or keyword */
static void scan_identifier(Lexer* lex) {
    int start_col = lex->col;
    char buf[256];
    int len = 0;

    /* Collect [A-Za-z0-9._] */
    while (lex->pos < lex->source_len) {
        char c = lex->source[lex->pos];
        if (isalnum(c) || c == '_') {
            if (len < 254) buf[len++] = c;
            lex->pos++; lex->col++;
        } else if (c == '.') {
            /* Only if followed by alphanumeric (could be UDT access) */
            /* Actually, in identifiers like DEF FN variable names can have dots
               but let's not include dots in identifiers for now */
            break;
        } else {
            break;
        }
    }
    buf[len] = '\0';

    /* Check for $ suffix on keywords like CHR$, LEFT$, etc. */
    TokenKind suffix = TOK_IDENT;
    char fullbuf[260];
    if (lex->pos < lex->source_len && lex->source[lex->pos] == '$') {
        snprintf(fullbuf, sizeof(fullbuf), "%s$", buf);
        TokenKind kw = lookup_keyword(fullbuf);
        if (kw != TOK_IDENT) {
            lex->pos++; lex->col++; /* consume $ */
            Token t;
            memset(&t, 0, sizeof(t));
            t.kind = kw;
            t.line = lex->line;
            t.col = start_col;
            t.value.str.text = strdup(fullbuf);
            t.value.str.length = len + 1;
            lexer_emit(lex, t);
            return;
        }
        /* Not a keyword with $, so it's a string-typed identifier */
        lex->pos++; lex->col++;
        suffix = TOK_IDENT_STR;
    } else if (lex->pos < lex->source_len) {
        char c = lex->source[lex->pos];
        if (c == '%') { suffix = TOK_IDENT_INT; lex->pos++; lex->col++; }
        else if (c == '&') { suffix = TOK_IDENT_LONG; lex->pos++; lex->col++; }
        else if (c == '!') { suffix = TOK_IDENT_SINGLE; lex->pos++; lex->col++; }
        else if (c == '#') { suffix = TOK_IDENT_DOUBLE; lex->pos++; lex->col++; }
    }

    Token t;
    memset(&t, 0, sizeof(t));
    t.line = lex->line;
    t.col = start_col;

    if (suffix == TOK_IDENT) {
        /* Try keyword lookup */
        TokenKind kw = lookup_keyword(buf);
        if (kw != TOK_IDENT) {
            t.kind = kw;
            /* For REM, skip rest of line */
            if (kw == TOK_KW_REM) {
                t.value.str.text = strdup(buf);
                t.value.str.length = len;
                lexer_emit(lex, t);
                /* Skip to end of line */
                while (lex->pos < lex->source_len &&
                       lex->source[lex->pos] != '\n' &&
                       lex->source[lex->pos] != '\r') {
                    lex->pos++; lex->col++;
                }
                return;
            }
            t.value.str.text = strdup(buf);
            t.value.str.length = len;
            lexer_emit(lex, t);
            return;
        }
        /* Plain identifier */
        t.kind = TOK_IDENT;
    } else {
        t.kind = suffix;
    }

    t.value.str.text = strdup(buf);
    t.value.str.length = len;

    /* Check if this identifier is followed by : at a statement boundary (label definition) */
    /* We'll handle label detection later in a post-pass or parser */

    lexer_emit(lex, t);
}

/* Skip comment (REM or ') */
static void skip_comment(Lexer* lex) {
    Token t = make_token(lex, TOK_KW_REM);
    t.value.str.text = strdup("");
    t.value.str.length = 0;
    lex->pos++; lex->col++; /* skip ' */
    /* Skip to end of line */
    while (lex->pos < lex->source_len &&
           lex->source[lex->pos] != '\n' &&
           lex->source[lex->pos] != '\r') {
        lex->pos++; lex->col++;
    }
    lexer_emit(lex, t);
}

/* Main tokenizer loop */
static void lexer_scan_token(Lexer* lex) {
    char c = lex->source[lex->pos];

    /* String literal */
    if (c == '"') { scan_string(lex); return; }

    /* Numeric literal or line number at start of line */
    if (isdigit(c) || (c == '.' && lex->pos + 1 < lex->source_len &&
                        isdigit(lex->source[lex->pos + 1]))) {
        /* Check if this is a line number (start of line, pure integer) */
        if (lex->at_line_start && isdigit(c)) {
            int start = lex->pos;
            int start_col = lex->col;
            /* Scan digits */
            while (lex->pos < lex->source_len && isdigit(lex->source[lex->pos])) {
                lex->pos++; lex->col++;
            }
            /* If followed by space/tab/colon/EOL, it's a line number */
            char next = (lex->pos < lex->source_len) ? lex->source[lex->pos] : '\0';
            if (next == ' ' || next == '\t' || next == ':' || next == '\r' ||
                next == '\n' || next == '\0') {
                char numbuf[32];
                int nlen = lex->pos - start;
                if (nlen > 30) nlen = 30;
                memcpy(numbuf, lex->source + start, nlen);
                numbuf[nlen] = '\0';

                Token t;
                memset(&t, 0, sizeof(t));
                t.kind = TOK_LINENO;
                t.line = lex->line;
                t.col = start_col;
                t.value.long_val = (int32_t)strtol(numbuf, NULL, 10);
                lexer_emit(lex, t);
                lex->at_line_start = 0;
                return;
            }
            /* Not a line number, rewind and parse as regular number */
            lex->pos = start;
            lex->col = start_col;
        }
        lex->at_line_start = 0;
        scan_number(lex);
        return;
    }

    /* Hex/Octal literals */
    if (c == '&') {
        char next = (lex->pos + 1 < lex->source_len) ? lex->source[lex->pos + 1] : '\0';
        if (next == 'H' || next == 'h') {
            lex->at_line_start = 0;
            scan_hex(lex);
            return;
        }
        if (next == 'O' || next == 'o' || (next >= '0' && next <= '7')) {
            lex->at_line_start = 0;
            scan_octal(lex);
            return;
        }
        /* Could be a long suffix for a preceding number, but we handle that in scan_number */
        /* Fall through to operator handling - but & alone is not valid */
        /* Actually & at start can also be a type suffix - we emit TOK_IDENT_LONG already */
    }

    lex->at_line_start = 0;

    /* Comment with ' */
    if (c == '\'') { skip_comment(lex); return; }

    /* Identifier or keyword */
    if (isalpha(c) || c == '_') {
        scan_identifier(lex);
        return;
    }

    /* $ prefix for $INCLUDE etc. */
    if (c == '$' && lex->pos + 1 < lex->source_len && isalpha(lex->source[lex->pos + 1])) {
        /* Scan as identifier starting with $ */
        int start_col = lex->col;
        char buf[256];
        int len = 0;
        buf[len++] = c;
        lex->pos++; lex->col++;
        while (lex->pos < lex->source_len && isalpha(lex->source[lex->pos])) {
            buf[len++] = lex->source[lex->pos];
            lex->pos++; lex->col++;
        }
        buf[len] = '\0';
        TokenKind kw = lookup_keyword(buf);
        Token t;
        memset(&t, 0, sizeof(t));
        t.kind = (kw != TOK_IDENT) ? kw : TOK_IDENT;
        t.line = lex->line;
        t.col = start_col;
        t.value.str.text = strdup(buf);
        t.value.str.length = len;
        lexer_emit(lex, t);
        return;
    }

    /* Operators and punctuation */
    switch (c) {
        case '+': lexer_emit(lex, make_token(lex, TOK_PLUS)); lex->pos++; lex->col++; break;
        case '-': lexer_emit(lex, make_token(lex, TOK_MINUS)); lex->pos++; lex->col++; break;
        case '*': lexer_emit(lex, make_token(lex, TOK_STAR)); lex->pos++; lex->col++; break;
        case '/': lexer_emit(lex, make_token(lex, TOK_SLASH)); lex->pos++; lex->col++; break;
        case '\\': lexer_emit(lex, make_token(lex, TOK_BACKSLASH)); lex->pos++; lex->col++; break;
        case '^': lexer_emit(lex, make_token(lex, TOK_CARET)); lex->pos++; lex->col++; break;
        case '(': lexer_emit(lex, make_token(lex, TOK_LPAREN)); lex->pos++; lex->col++; break;
        case ')': lexer_emit(lex, make_token(lex, TOK_RPAREN)); lex->pos++; lex->col++; break;
        case ',': lexer_emit(lex, make_token(lex, TOK_COMMA)); lex->pos++; lex->col++; break;
        case ';': lexer_emit(lex, make_token(lex, TOK_SEMICOLON)); lex->pos++; lex->col++; break;
        case ':': lexer_emit(lex, make_token(lex, TOK_COLON)); lex->pos++; lex->col++; break;
        case '.': lexer_emit(lex, make_token(lex, TOK_DOT)); lex->pos++; lex->col++; break;
        case '#': lexer_emit(lex, make_token(lex, TOK_HASH)); lex->pos++; lex->col++; break;
        case '=': lexer_emit(lex, make_token(lex, TOK_EQ)); lex->pos++; lex->col++; break;
        case '<': {
            lex->pos++; lex->col++;
            if (lex->pos < lex->source_len) {
                if (lex->source[lex->pos] == '>') {
                    Token t = make_token(lex, TOK_NE);
                    t.col--;
                    lexer_emit(lex, t);
                    lex->pos++; lex->col++;
                } else if (lex->source[lex->pos] == '=') {
                    Token t = make_token(lex, TOK_LE);
                    t.col--;
                    lexer_emit(lex, t);
                    lex->pos++; lex->col++;
                } else {
                    Token t = make_token(lex, TOK_LT);
                    t.col--;
                    lexer_emit(lex, t);
                }
            } else {
                Token t = make_token(lex, TOK_LT);
                t.col--;
                lexer_emit(lex, t);
            }
            break;
        }
        case '>': {
            lex->pos++; lex->col++;
            if (lex->pos < lex->source_len && lex->source[lex->pos] == '=') {
                Token t = make_token(lex, TOK_GE);
                t.col--;
                lexer_emit(lex, t);
                lex->pos++; lex->col++;
            } else {
                Token t = make_token(lex, TOK_GT);
                t.col--;
                lexer_emit(lex, t);
            }
            break;
        }
        default:
            /* Unknown character - skip it */
            lex->pos++; lex->col++;
            break;
    }
}

/* --- Public API --- */

int lexer_init(Lexer* lex, const char* source, int source_len) {
    lex->source = source;
    lex->source_len = source_len;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->at_line_start = 1;
    lex->tokens = NULL;
    lex->token_count = 0;
    lex->token_cap = 0;
    return 0;
}

int lexer_tokenize(Lexer* lex) {
    while (lex->pos < lex->source_len) {
        /* Line continuation */
        if (check_line_continuation(lex)) continue;

        char c = lex->source[lex->pos];

        /* Skip whitespace */
        if (c == ' ' || c == '\t') {
            skip_whitespace(lex);
            continue;
        }

        /* Newlines */
        if (c == '\r' || c == '\n') {
            /* Only emit EOL if the last token isn't already EOL */
            if (lex->token_count == 0 ||
                lex->tokens[lex->token_count - 1].kind != TOK_EOL) {
                lexer_emit(lex, make_token(lex, TOK_EOL));
            }
            if (c == '\r') { lex->pos++; }
            if (lex->pos < lex->source_len && lex->source[lex->pos] == '\n') {
                lex->pos++;
            }
            lex->line++;
            lex->col = 1;
            lex->at_line_start = 1;
            continue;
        }

        lexer_scan_token(lex);
    }

    /* Emit final EOL if needed */
    if (lex->token_count > 0 && lex->tokens[lex->token_count - 1].kind != TOK_EOL) {
        lexer_emit(lex, make_token(lex, TOK_EOL));
    }

    /* Emit EOF */
    lexer_emit(lex, make_token(lex, TOK_EOF));

    /* Post-pass: detect labels (identifier followed by colon at statement boundary) */
    for (int i = 0; i < lex->token_count - 1; i++) {
        if (lex->tokens[i].kind == TOK_IDENT &&
            lex->tokens[i + 1].kind == TOK_COLON) {
            /* Check if this is at start of line (preceded by EOL, LINENO, or nothing) */
            int at_start = (i == 0);
            if (!at_start && i > 0) {
                TokenKind prev = lex->tokens[i - 1].kind;
                at_start = (prev == TOK_EOL || prev == TOK_LINENO || prev == TOK_COLON);
            }
            if (at_start) {
                lex->tokens[i].kind = TOK_LABEL;
                /* Remove the colon token by shifting */
                fb_free(lex->tokens[i + 1].value.str.text); /* might be NULL, that's ok */
                for (int j = i + 1; j < lex->token_count - 1; j++) {
                    lex->tokens[j] = lex->tokens[j + 1];
                }
                lex->token_count--;
            }
        }
    }

    return 0;
}

void lexer_free(Lexer* lex) {
    for (int i = 0; i < lex->token_count; i++) {
        TokenKind k = lex->tokens[i].kind;
        if (k == TOK_STRING_LIT || k == TOK_LABEL ||
            (k >= TOK_IDENT && k <= TOK_IDENT_DOUBLE) ||
            (k >= TOK_KW_ABS && k < TOK_LINENO)) {
            fb_free(lex->tokens[i].value.str.text);
        }
    }
    fb_free(lex->tokens);
    lex->tokens = NULL;
    lex->token_count = 0;
    lex->token_cap = 0;
}

void lexer_dump(const Lexer* lex) {
    for (int i = 0; i < lex->token_count; i++) {
        const Token* t = &lex->tokens[i];
        printf("[%3d] %-20s", t->line, token_kind_name(t->kind));
        switch (t->kind) {
            case TOK_INTEGER_LIT: printf(" %d", t->value.int_val); break;
            case TOK_LONG_LIT:
            case TOK_LINENO:      printf(" %d", t->value.long_val); break;
            case TOK_SINGLE_LIT:  printf(" %g", t->value.single_val); break;
            case TOK_DOUBLE_LIT:  printf(" %.15g", t->value.double_val); break;
            case TOK_STRING_LIT:  printf(" \"%s\"", t->value.str.text); break;
            case TOK_LABEL:       printf(" %s:", t->value.str.text); break;
            case TOK_EOL:
            case TOK_EOF:         break;
            default:
                if (t->value.str.text && t->value.str.length > 0)
                    printf(" %s", t->value.str.text);
                break;
        }
        printf("\n");
    }
}

/* Token kind name helper */
const char* token_kind_name(TokenKind kind) {
    static const char* names[] = {
        [TOK_INTEGER_LIT] = "INTEGER_LIT",
        [TOK_LONG_LIT] = "LONG_LIT",
        [TOK_SINGLE_LIT] = "SINGLE_LIT",
        [TOK_DOUBLE_LIT] = "DOUBLE_LIT",
        [TOK_STRING_LIT] = "STRING_LIT",
        [TOK_IDENT] = "IDENT",
        [TOK_IDENT_STR] = "IDENT_STR",
        [TOK_IDENT_INT] = "IDENT_INT",
        [TOK_IDENT_LONG] = "IDENT_LONG",
        [TOK_IDENT_SINGLE] = "IDENT_SINGLE",
        [TOK_IDENT_DOUBLE] = "IDENT_DOUBLE",
        [TOK_PLUS] = "PLUS", [TOK_MINUS] = "MINUS",
        [TOK_STAR] = "STAR", [TOK_SLASH] = "SLASH",
        [TOK_BACKSLASH] = "BACKSLASH", [TOK_CARET] = "CARET",
        [TOK_EQ] = "EQ", [TOK_NE] = "NE",
        [TOK_LT] = "LT", [TOK_GT] = "GT",
        [TOK_LE] = "LE", [TOK_GE] = "GE",
        [TOK_LPAREN] = "LPAREN", [TOK_RPAREN] = "RPAREN",
        [TOK_COMMA] = "COMMA", [TOK_SEMICOLON] = "SEMICOLON",
        [TOK_COLON] = "COLON", [TOK_DOT] = "DOT", [TOK_HASH] = "HASH",
        [TOK_LINENO] = "LINENO", [TOK_LABEL] = "LABEL",
        [TOK_EOL] = "EOL", [TOK_EOF] = "EOF",
        [TOK_KW_ABS] = "KW_ABS", [TOK_KW_AND] = "KW_AND",
        [TOK_KW_AS] = "KW_AS", [TOK_KW_ASC] = "KW_ASC",
        [TOK_KW_ATN] = "KW_ATN", [TOK_KW_BEEP] = "KW_BEEP",
        [TOK_KW_CALL] = "KW_CALL", [TOK_KW_CASE] = "KW_CASE",
        [TOK_KW_CDBL] = "KW_CDBL", [TOK_KW_CHR] = "KW_CHR",
        [TOK_KW_CINT] = "KW_CINT", [TOK_KW_CIRCLE] = "KW_CIRCLE",
        [TOK_KW_CLEAR] = "KW_CLEAR", [TOK_KW_CLNG] = "KW_CLNG",
        [TOK_KW_CLOSE] = "KW_CLOSE", [TOK_KW_CLS] = "KW_CLS",
        [TOK_KW_COLOR] = "KW_COLOR", [TOK_KW_COMMON] = "KW_COMMON",
        [TOK_KW_CONST] = "KW_CONST", [TOK_KW_COS] = "KW_COS",
        [TOK_KW_CSNG] = "KW_CSNG", [TOK_KW_CSRLIN] = "KW_CSRLIN",
        [TOK_KW_DATA] = "KW_DATA", [TOK_KW_DATE] = "KW_DATE",
        [TOK_KW_DECLARE] = "KW_DECLARE", [TOK_KW_DEF] = "KW_DEF",
        [TOK_KW_DEFDBL] = "KW_DEFDBL", [TOK_KW_DEFINT] = "KW_DEFINT",
        [TOK_KW_DEFLNG] = "KW_DEFLNG", [TOK_KW_DEFSNG] = "KW_DEFSNG",
        [TOK_KW_DEFSTR] = "KW_DEFSTR", [TOK_KW_DIM] = "KW_DIM",
        [TOK_KW_DO] = "KW_DO", [TOK_KW_DOUBLE] = "KW_DOUBLE",
        [TOK_KW_DRAW] = "KW_DRAW",
        [TOK_KW_ELSE] = "KW_ELSE", [TOK_KW_ELSEIF] = "KW_ELSEIF",
        [TOK_KW_END] = "KW_END", [TOK_KW_ENVIRON] = "KW_ENVIRON",
        [TOK_KW_EOF] = "KW_EOF", [TOK_KW_EQV] = "KW_EQV",
        [TOK_KW_ERASE] = "KW_ERASE", [TOK_KW_ERR] = "KW_ERR",
        [TOK_KW_ERL] = "KW_ERL", [TOK_KW_ERROR] = "KW_ERROR",
        [TOK_KW_EXIT] = "KW_EXIT", [TOK_KW_EXP] = "KW_EXP",
        [TOK_KW_FIELD] = "KW_FIELD", [TOK_KW_FILES] = "KW_FILES",
        [TOK_KW_FIX] = "KW_FIX", [TOK_KW_FOR] = "KW_FOR",
        [TOK_KW_FRE] = "KW_FRE", [TOK_KW_FREEFILE] = "KW_FREEFILE",
        [TOK_KW_FUNCTION] = "KW_FUNCTION",
        [TOK_KW_GET] = "KW_GET", [TOK_KW_GOSUB] = "KW_GOSUB",
        [TOK_KW_GOTO] = "KW_GOTO",
        [TOK_KW_HEX] = "KW_HEX", [TOK_KW_IF] = "KW_IF",
        [TOK_KW_IMP] = "KW_IMP", [TOK_KW_INKEY] = "KW_INKEY",
        [TOK_KW_INP] = "KW_INP", [TOK_KW_INPUT] = "KW_INPUT",
        [TOK_KW_INSTR] = "KW_INSTR", [TOK_KW_INT] = "KW_INT",
        [TOK_KW_INTEGER] = "KW_INTEGER", [TOK_KW_IS] = "KW_IS",
        [TOK_KW_KEY] = "KW_KEY", [TOK_KW_KILL] = "KW_KILL",
        [TOK_KW_LBOUND] = "KW_LBOUND", [TOK_KW_LCASE] = "KW_LCASE",
        [TOK_KW_LEFT] = "KW_LEFT", [TOK_KW_LEN] = "KW_LEN",
        [TOK_KW_LET] = "KW_LET", [TOK_KW_LINE] = "KW_LINE",
        [TOK_KW_LOC] = "KW_LOC", [TOK_KW_LOCATE] = "KW_LOCATE",
        [TOK_KW_LOCK] = "KW_LOCK", [TOK_KW_LOF] = "KW_LOF",
        [TOK_KW_LOG] = "KW_LOG", [TOK_KW_LONG] = "KW_LONG",
        [TOK_KW_LOOP] = "KW_LOOP", [TOK_KW_LPRINT] = "KW_LPRINT",
        [TOK_KW_LSET] = "KW_LSET", [TOK_KW_LTRIM] = "KW_LTRIM",
        [TOK_KW_MID] = "KW_MID", [TOK_KW_MKDIR] = "KW_MKDIR",
        [TOK_KW_MOD] = "KW_MOD",
        [TOK_KW_NAME] = "KW_NAME", [TOK_KW_NEXT] = "KW_NEXT",
        [TOK_KW_NOT] = "KW_NOT",
        [TOK_KW_OCT] = "KW_OCT", [TOK_KW_ON] = "KW_ON",
        [TOK_KW_OPEN] = "KW_OPEN", [TOK_KW_OPTION] = "KW_OPTION",
        [TOK_KW_OR] = "KW_OR", [TOK_KW_OUT] = "KW_OUT",
        [TOK_KW_OUTPUT] = "KW_OUTPUT",
        [TOK_KW_PAINT] = "KW_PAINT", [TOK_KW_PALETTE] = "KW_PALETTE",
        [TOK_KW_PEEK] = "KW_PEEK", [TOK_KW_PLAY] = "KW_PLAY",
        [TOK_KW_PMAP] = "KW_PMAP", [TOK_KW_POINT] = "KW_POINT",
        [TOK_KW_POKE] = "KW_POKE", [TOK_KW_POS] = "KW_POS",
        [TOK_KW_PRESET] = "KW_PRESET", [TOK_KW_PRINT] = "KW_PRINT",
        [TOK_KW_PSET] = "KW_PSET", [TOK_KW_PUT] = "KW_PUT",
        [TOK_KW_RANDOMIZE] = "KW_RANDOMIZE", [TOK_KW_READ] = "KW_READ",
        [TOK_KW_REDIM] = "KW_REDIM", [TOK_KW_REM] = "KW_REM",
        [TOK_KW_RESET] = "KW_RESET", [TOK_KW_RESTORE] = "KW_RESTORE",
        [TOK_KW_RESUME] = "KW_RESUME", [TOK_KW_RETURN] = "KW_RETURN",
        [TOK_KW_RIGHT] = "KW_RIGHT", [TOK_KW_RMDIR] = "KW_RMDIR",
        [TOK_KW_RND] = "KW_RND", [TOK_KW_RSET] = "KW_RSET",
        [TOK_KW_RTRIM] = "KW_RTRIM", [TOK_KW_RUN] = "KW_RUN",
        [TOK_KW_SCREEN] = "KW_SCREEN", [TOK_KW_SEEK] = "KW_SEEK",
        [TOK_KW_SEG] = "KW_SEG", [TOK_KW_SELECT] = "KW_SELECT",
        [TOK_KW_SGN] = "KW_SGN", [TOK_KW_SHARED] = "KW_SHARED",
        [TOK_KW_SHELL] = "KW_SHELL", [TOK_KW_SIN] = "KW_SIN",
        [TOK_KW_SINGLE] = "KW_SINGLE", [TOK_KW_SLEEP] = "KW_SLEEP",
        [TOK_KW_SOUND] = "KW_SOUND", [TOK_KW_SPACE] = "KW_SPACE",
        [TOK_KW_SPC] = "KW_SPC", [TOK_KW_SQR] = "KW_SQR",
        [TOK_KW_STATIC] = "KW_STATIC", [TOK_KW_STEP] = "KW_STEP",
        [TOK_KW_STICK] = "KW_STICK", [TOK_KW_STOP] = "KW_STOP",
        [TOK_KW_STR] = "KW_STR", [TOK_KW_STRIG] = "KW_STRIG",
        [TOK_KW_STRING] = "KW_STRING", [TOK_KW_SUB] = "KW_SUB",
        [TOK_KW_SWAP] = "KW_SWAP", [TOK_KW_SYSTEM] = "KW_SYSTEM",
        [TOK_KW_TAB] = "KW_TAB", [TOK_KW_TAN] = "KW_TAN",
        [TOK_KW_THEN] = "KW_THEN", [TOK_KW_TIME] = "KW_TIME",
        [TOK_KW_TIMER] = "KW_TIMER", [TOK_KW_TO] = "KW_TO",
        [TOK_KW_TROFF] = "KW_TROFF", [TOK_KW_TRON] = "KW_TRON",
        [TOK_KW_TYPE] = "KW_TYPE",
        [TOK_KW_UBOUND] = "KW_UBOUND", [TOK_KW_UCASE] = "KW_UCASE",
        [TOK_KW_UNLOCK] = "KW_UNLOCK", [TOK_KW_UNTIL] = "KW_UNTIL",
        [TOK_KW_USING] = "KW_USING",
        [TOK_KW_VAL] = "KW_VAL", [TOK_KW_VARPTR] = "KW_VARPTR",
        [TOK_KW_VARSEG] = "KW_VARSEG", [TOK_KW_VIEW] = "KW_VIEW",
        [TOK_KW_WAIT] = "KW_WAIT", [TOK_KW_WEND] = "KW_WEND",
        [TOK_KW_WHILE] = "KW_WHILE", [TOK_KW_WIDTH] = "KW_WIDTH",
        [TOK_KW_WINDOW] = "KW_WINDOW", [TOK_KW_WRITE] = "KW_WRITE",
        [TOK_KW_XOR] = "KW_XOR",
    };
    if (kind >= 0 && kind < TOK_COUNT && names[kind])
        return names[kind];
    return "UNKNOWN";
}
