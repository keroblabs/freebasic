#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef struct {
    const char* source;      /* Entire source text */
    int         source_len;
    int         pos;         /* Current byte position */
    int         line;        /* Current line (1-based) */
    int         col;         /* Current column (1-based) */
    int         at_line_start; /* Track start-of-line for line numbers */
    Token*      tokens;      /* Dynamic array of produced tokens */
    int         token_count;
    int         token_cap;
} Lexer;

int  lexer_init(Lexer* lex, const char* source, int source_len);
int  lexer_tokenize(Lexer* lex);
void lexer_free(Lexer* lex);
void lexer_dump(const Lexer* lex);

#endif
