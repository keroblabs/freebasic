/*
 * main.c — FBasic interpreter entry point
 * Usage: fbasic <file.bas>
 */
#include "fb.h"
#include "system_api.h"
#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: fbasic <file.bas>\n");
        return 1;
    }

    int lex_only = 0;
    int parse_only = 0;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lex") == 0) {
            lex_only = 1;
        } else if (strcmp(argv[i], "--parse") == 0) {
            parse_only = 1;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: fbasic [--lex|--parse] <file.bas>\n");
        return 1;
    }

    /* 1. Load source file */
    FILE* f = fopen(filename, "rb");
    if (!f) {
        perror("Cannot open file");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = fb_malloc(fsize + 1);
    if (!source) {
        fprintf(stderr, "Out of memory\n");
        fclose(f);
        return 1;
    }
    fread(source, 1, fsize, f);
    source[fsize] = '\0';
    fclose(f);

    /* 2. Tokenize */
    Lexer lex;
    lexer_init(&lex, source, (int)fsize);
    if (lexer_tokenize(&lex) != 0) {
        fprintf(stderr, "Lexer failed.\n");
        fb_free(source);
        return 1;
    }

    if (lex_only) {
        lexer_dump(&lex);
        lexer_free(&lex);
        fb_free(source);
        return 0;
    }

    /* 3. Parse */
    Program prog;
    memset(&prog, 0, sizeof(prog));
    if (parser_parse(lex.tokens, lex.token_count, &prog) != 0) {
        fprintf(stderr, "Parser failed.\n");
        lexer_free(&lex);
        fb_free(source);
        return 1;
    }

    if (parse_only) {
        printf("Parsed %d statements, %d labels, %d line mappings, %d data items\n",
               prog.stmt_count, prog.label_count, prog.linemap_count, prog.data_count);
        program_free(&prog);
        lexer_free(&lex);
        fb_free(source);
        return 0;
    }

    /* 4. Interpret */
    Interpreter interp;
    interp_init(&interp, &prog);
    interp_set_command_line(&interp, argc, argv);
    interp_run(&interp);

    /* 5. Clean up */
    interp_free(&interp);
    program_free(&prog);
    lexer_free(&lex);
    fb_free(source);

    return 0;
}
