#ifndef TOKEN_H
#define TOKEN_H

#include <stdint.h>

typedef enum {
    /* === Literals === */
    TOK_INTEGER_LIT,      /* 42, &H1F, &O77 */
    TOK_LONG_LIT,         /* 42& */
    TOK_SINGLE_LIT,       /* 3.14, 3.14!, 1E10 */
    TOK_DOUBLE_LIT,       /* 3.14#, 1D10 */
    TOK_STRING_LIT,       /* "hello" */

    /* === Identifiers === */
    TOK_IDENT,            /* myVar (no suffix) */
    TOK_IDENT_STR,        /* myVar$ */
    TOK_IDENT_INT,        /* myVar% */
    TOK_IDENT_LONG,       /* myVar& */
    TOK_IDENT_SINGLE,     /* myVar! */
    TOK_IDENT_DOUBLE,     /* myVar# */

    /* === Operators === */
    TOK_PLUS,             /* + */
    TOK_MINUS,            /* - */
    TOK_STAR,             /* * */
    TOK_SLASH,            /* / */
    TOK_BACKSLASH,        /* \ (integer division) */
    TOK_CARET,            /* ^ */
    TOK_EQ,               /* = */
    TOK_NE,               /* <> */
    TOK_LT,               /* < */
    TOK_GT,               /* > */
    TOK_LE,               /* <= */
    TOK_GE,               /* >= */

    /* === Punctuation === */
    TOK_LPAREN,           /* ( */
    TOK_RPAREN,           /* ) */
    TOK_COMMA,            /* , */
    TOK_SEMICOLON,        /* ; */
    TOK_COLON,            /* : (statement separator) */
    TOK_DOT,              /* . (UDT field access) */
    TOK_HASH,             /* # (file number prefix) */

    /* === Keywords (alphabetical) === */
    TOK_KW_ABS, TOK_KW_ACCESS, TOK_KW_ALL, TOK_KW_AND, TOK_KW_APPEND, TOK_KW_AS,
    TOK_KW_ASC, TOK_KW_ATN,
    TOK_KW_BASE, TOK_KW_BEEP, TOK_KW_BINARY, TOK_KW_BYVAL,
    TOK_KW_CALL, TOK_KW_CASE,
    TOK_KW_CDBL, TOK_KW_CHAIN, TOK_KW_CHR, TOK_KW_CINT, TOK_KW_CIRCLE,
    TOK_KW_CLEAR, TOK_KW_CLNG, TOK_KW_CLOSE, TOK_KW_CLS,
    TOK_KW_COLOR, TOK_KW_COM, TOK_KW_COMMAND, TOK_KW_COMMON,
    TOK_KW_CONST, TOK_KW_COS, TOK_KW_CSNG, TOK_KW_CSRLIN, TOK_KW_CVD,
    TOK_KW_CVDMBF, TOK_KW_CVI, TOK_KW_CVL, TOK_KW_CVS, TOK_KW_CVSMBF,
    TOK_KW_CHDIR, TOK_KW_DATA, TOK_KW_DATE, TOK_KW_DECLARE, TOK_KW_DEF,
    TOK_KW_DEFDBL, TOK_KW_DEFINT, TOK_KW_DEFLNG, TOK_KW_DEFSNG,
    TOK_KW_DEFSTR, TOK_KW_DIM, TOK_KW_DO, TOK_KW_DOUBLE, TOK_KW_DRAW,
    TOK_KW_ELSE, TOK_KW_ELSEIF, TOK_KW_END, TOK_KW_ENVIRON,
    TOK_KW_EOF, TOK_KW_EQV, TOK_KW_ERASE, TOK_KW_ERDEV, TOK_KW_ERR,
    TOK_KW_ERL, TOK_KW_ERROR, TOK_KW_EXIT, TOK_KW_EXP,
    TOK_KW_FIELD, TOK_KW_FILEATTR, TOK_KW_FILES, TOK_KW_FIX, TOK_KW_FN,
    TOK_KW_FOR, TOK_KW_FRE, TOK_KW_FREEFILE, TOK_KW_FUNCTION,
    TOK_KW_GET, TOK_KW_GOSUB, TOK_KW_GOTO,
    TOK_KW_HEX, TOK_KW_IF, TOK_KW_IMP, TOK_KW_INCLUDE, TOK_KW_INKEY,
    TOK_KW_INP, TOK_KW_INPUT, TOK_KW_INSTR, TOK_KW_INT, TOK_KW_INTEGER,
    TOK_KW_IOCTL, TOK_KW_IS,
    TOK_KW_KEY, TOK_KW_KILL,
    TOK_KW_LBOUND, TOK_KW_LCASE, TOK_KW_LEFT, TOK_KW_LEN, TOK_KW_LIB,
    TOK_KW_LET, TOK_KW_LINE, TOK_KW_LOC, TOK_KW_LOCATE,
    TOK_KW_LOCK, TOK_KW_LOF, TOK_KW_LOG, TOK_KW_LONG, TOK_KW_LOOP,
    TOK_KW_LPRINT, TOK_KW_LSET, TOK_KW_LTRIM,
    TOK_KW_MID, TOK_KW_MKD, TOK_KW_MKDIR, TOK_KW_MKDMBF, TOK_KW_MKI,
    TOK_KW_MKL, TOK_KW_MKS, TOK_KW_MKSMBF, TOK_KW_MOD,
    TOK_KW_MERGE, TOK_KW_NAME, TOK_KW_NEXT, TOK_KW_NOT,
    TOK_KW_OCT, TOK_KW_OFF, TOK_KW_ON, TOK_KW_OPEN, TOK_KW_OPTION,
    TOK_KW_OR, TOK_KW_OUT, TOK_KW_OUTPUT,
    TOK_KW_PAINT, TOK_KW_PALETTE, TOK_KW_PCOPY, TOK_KW_PEEK,
    TOK_KW_PEN, TOK_KW_PLAY, TOK_KW_PMAP, TOK_KW_POINT, TOK_KW_POKE,
    TOK_KW_POS, TOK_KW_PRESET, TOK_KW_PRINT, TOK_KW_PSET, TOK_KW_PUT,
    TOK_KW_RANDOM, TOK_KW_RANDOMIZE, TOK_KW_READ, TOK_KW_REDIM, TOK_KW_REM,
    TOK_KW_RESET, TOK_KW_RESTORE, TOK_KW_RESUME, TOK_KW_RETURN,
    TOK_KW_RIGHT, TOK_KW_RMDIR, TOK_KW_RND, TOK_KW_RSET,
    TOK_KW_RTRIM, TOK_KW_RUN,
    TOK_KW_SADD, TOK_KW_SCREEN, TOK_KW_SEEK, TOK_KW_SEG,
    TOK_KW_SELECT, TOK_KW_SGN, TOK_KW_SHARED, TOK_KW_SHELL,
    TOK_KW_SIN, TOK_KW_SINGLE, TOK_KW_SLEEP, TOK_KW_SOUND,
    TOK_KW_SPACE, TOK_KW_SPC, TOK_KW_SQR, TOK_KW_STATIC,
    TOK_KW_STEP, TOK_KW_STICK, TOK_KW_STOP, TOK_KW_STR,
    TOK_KW_STRIG, TOK_KW_STRING, TOK_KW_SUB, TOK_KW_SWAP,
    TOK_KW_SYSTEM,
    TOK_KW_TAB, TOK_KW_TAN, TOK_KW_THEN, TOK_KW_TIME,
    TOK_KW_TIMER, TOK_KW_TO, TOK_KW_TROFF, TOK_KW_TRON,
    TOK_KW_TYPE,
    TOK_KW_UBOUND, TOK_KW_UCASE, TOK_KW_UEVENT, TOK_KW_UNLOCK,
    TOK_KW_UNTIL, TOK_KW_USING,
    TOK_KW_VAL, TOK_KW_VARPTR, TOK_KW_VARSEG, TOK_KW_VIEW,
    TOK_KW_WAIT, TOK_KW_WEND, TOK_KW_WHILE, TOK_KW_WIDTH, TOK_KW_WINDOW,
    TOK_KW_WRITE,
    TOK_KW_XOR,

    /* === Special === */
    TOK_LINENO,           /* Line number at start of line */
    TOK_LABEL,            /* Label followed by colon */
    TOK_EOL,              /* End of logical line */
    TOK_EOF,              /* End of file */

    TOK_COUNT             /* Total number of token types */
} TokenKind;

typedef struct {
    TokenKind kind;
    int       line;          /* Source line number (1-based) */
    int       col;           /* Source column (1-based) */
    union {
        int16_t  int_val;    /* TOK_INTEGER_LIT */
        int32_t  long_val;   /* TOK_LONG_LIT, TOK_LINENO */
        float    single_val; /* TOK_SINGLE_LIT */
        double   double_val; /* TOK_DOUBLE_LIT */
        struct {
            char*   text;    /* Heap-allocated, null-terminated */
            int     length;
        } str;               /* TOK_STRING_LIT, TOK_IDENT*, TOK_LABEL */
    } value;
} Token;

const char* token_kind_name(TokenKind kind);

#endif
