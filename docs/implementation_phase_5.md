# Phase 5 — File I/O: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **file input/output** for the FreeBASIC interpreter. Phase 5 adds sequential, random-access, and binary file operations — enabling file copy utilities, CSV processors, and simple database applications.

---

## Project File Structure (Phase 5 additions)

```
fbasic/
├── Makefile                        [MOD]
├── include/
│   ├── ast.h                      [MOD] — AST nodes for OPEN, CLOSE, file I/O
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD] — file handle table
│   ├── fileio.h                   [NEW] — file handle management + I/O API
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse OPEN, CLOSE, PRINT #, INPUT #, etc.
│   ├── interpreter.c              [MOD] — execute file statements
│   ├── fileio.c                   [NEW] — file handle table, all I/O operations
│   └── ...
└── tests/
    └── verify/
        ├── phase5_sequential.bas  [NEW] — sequential file I/O tests
        ├── phase5_random.bas      [NEW] — random-access file I/O tests
        ├── phase5_binary.bas      [NEW] — binary file I/O tests
        ├── phase5_functions.bas   [NEW] — EOF, LOF, LOC, SEEK, FREEFILE
        ├── phase5_management.bas  [NEW] — NAME, KILL, FILES
        ├── phase5_csv.bas         [NEW] — milestone: CSV processor
        └── phase5_expected/       [NEW]
            ├── sequential.txt
            ├── random.txt
            ├── binary.txt
            ├── functions.txt
            └── csv.txt
```

---

## 1. File Handle Table (`include/fileio.h`, `src/fileio.c`)

### 1.1 File Handle Structure

```c
#ifndef FILEIO_H
#define FILEIO_H

#include "value.h"
#include <stdio.h>

#define FB_MAX_FILES 255

typedef enum {
    FMODE_CLOSED = 0,
    FMODE_INPUT,       // Sequential input
    FMODE_OUTPUT,      // Sequential output
    FMODE_APPEND,      // Sequential append
    FMODE_RANDOM,      // Random access
    FMODE_BINARY       // Binary access
} FileMode;

typedef enum {
    FACCESS_READ_WRITE = 0,
    FACCESS_READ,
    FACCESS_WRITE
} FileAccess;

typedef enum {
    FLOCK_DEFAULT = 0,
    FLOCK_SHARED,
    FLOCK_READ,
    FLOCK_WRITE,
    FLOCK_READ_WRITE
} FileLock;

typedef struct {
    FILE*       fp;             // C file pointer
    FileMode    mode;           // Current mode
    FileAccess  access;         // Access mode
    FileLock    lock;           // Lock type
    int         reclen;         // Record length (RANDOM mode, default 128)
    int         is_open;        // 0 = closed, 1 = open
    char        filename[260];  // Original filename
    long        file_pos;       // Current position for LOC()

    // FIELD mappings (Phase 3 FIELD statement)
    struct {
        char   var_name[42];
        int    offset;
        int    width;
    }* field_map;
    int         field_count;

    // Random-access record buffer
    char*       record_buffer;  // Buffer of size reclen
} FBFile;

// Global file table
typedef struct {
    FBFile  files[FB_MAX_FILES + 1]; // files[1..255], files[0] unused
} FileTable;

void filetable_init(FileTable* ft);
void filetable_close_all(FileTable* ft);

// Open a file. Returns 0 on success, error code on failure.
int qbfile_open(FileTable* ft, int filenum, const char* filename,
                FileMode mode, FileAccess access, FileLock lock, int reclen);

// Close a file.
void qbfile_close(FileTable* ft, int filenum);

// Get file handle (NULL if not open).
FBFile* qbfile_get(FileTable* ft, int filenum);

// Find next free file number (FREEFILE).
int qbfile_freefile(const FileTable* ft);

// EOF check.
int qbfile_eof(FBFile* f);

// LOF — length of file.
long qbfile_lof(FBFile* f);

// LOC — current record number (RANDOM) or byte position / 128 (OTHER).
long qbfile_loc(FBFile* f);

// SEEK — get or set position.
long qbfile_seek_get(FBFile* f);
void qbfile_seek_set(FBFile* f, long pos);

#endif
```

### 1.2 File Table in Interpreter

```c
// Add to Interpreter struct:
typedef struct Interpreter {
    // ... previous fields ...
    FileTable    file_table;
} Interpreter;
```

---

## 2. OPEN Statement

### 2.1 Syntax (Two Forms)

**Modern syntax:**
```basic
OPEN filename FOR mode [ACCESS access] [lock] AS #filenum [LEN = reclen]
```

**Legacy syntax:**
```basic
OPEN "mode", #filenum, filename [, reclen]
```

Where mode is: `INPUT`, `OUTPUT`, `APPEND`, `RANDOM`, `BINARY`.

### 2.2 AST Node

```c
// AST_OPEN
struct {
    struct ASTNode* filename;    // String expression
    FileMode        mode;
    FileAccess      access;
    FileLock        lock;
    struct ASTNode* filenum;     // Expression → integer
    struct ASTNode* reclen;      // Optional LEN= expression (NULL → default 128)
} open_stmt;
```

### 2.3 Parse OPEN

```c
static void parse_open(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume OPEN

    ASTNode* filename = parse_expr(p, 1);

    // Detect legacy vs modern syntax:
    // Legacy: OPEN "O", #1, "file.txt"
    // Modern: OPEN "file.txt" FOR OUTPUT AS #1
    if (current_token(p)->kind == TOK_COMMA) {
        // Legacy syntax
        parse_open_legacy(p, line, filename);
        return;
    }

    expect(p, TOK_KW_FOR);

    // Parse mode
    FileMode mode;
    Token* mode_tok = current_token(p);
    if (mode_tok->kind == TOK_KW_INPUT)       { mode = FMODE_INPUT; advance(p); }
    else if (mode_tok->kind == TOK_KW_OUTPUT)  { mode = FMODE_OUTPUT; advance(p); }
    else if (_stricmp(mode_tok->value.str.text, "APPEND") == 0)
        { mode = FMODE_APPEND; advance(p); }
    else if (_stricmp(mode_tok->value.str.text, "RANDOM") == 0)
        { mode = FMODE_RANDOM; advance(p); }
    else if (_stricmp(mode_tok->value.str.text, "BINARY") == 0)
        { mode = FMODE_BINARY; advance(p); }
    else {
        fb_syntax_error(line, mode_tok->col, "Expected file mode");
        return;
    }

    // Optional ACCESS clause
    FileAccess access = FACCESS_READ_WRITE;
    if (current_token(p)->kind == TOK_KW_AS &&
        peek_is_access_keyword(p)) {
        // Actually parse ACCESS keyword
        // ...
    }

    // Optional LOCK clause
    FileLock lock = FLOCK_DEFAULT;
    if (current_token(p)->kind == TOK_KW_LOCK) {
        advance(p);
        // Parse SHARED, READ, WRITE, READ WRITE
    }

    expect(p, TOK_KW_AS);

    // File number: #expr or expr
    if (current_token(p)->kind == TOK_HASH) advance(p);
    ASTNode* filenum = parse_expr(p, 1);

    // Optional LEN = n
    ASTNode* reclen = NULL;
    if (current_token(p)->kind == TOK_KW_LEN ||
        (current_token(p)->kind == TOK_IDENT &&
         _stricmp(current_token(p)->value.str.text, "LEN") == 0)) {
        advance(p);
        expect(p, TOK_EQ);
        reclen = parse_expr(p, 1);
    }

    program_add_stmt(p->prog,
        ast_open(line, filename, mode, access, lock, filenum, reclen));
}
```

### 2.4 Execute OPEN

```c
static void exec_open(Interpreter* interp, ASTNode* node) {
    FBValue fname_val = eval_expr(interp, node->data.open_stmt.filename);
    const char* filename = fname_val.as.str->data;

    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.open_stmt.filenum));
    if (filenum < 1 || filenum > FB_MAX_FILES) {
        fb_error(FB_ERR_BAD_FILE_NAME, node->line, "Invalid file number");
        fbval_release(&fname_val);
        return;
    }

    int reclen = 128; // Default
    if (node->data.open_stmt.reclen) {
        reclen = (int)fbval_to_long(&eval_expr(interp,
                     node->data.open_stmt.reclen));
    }

    int err = qbfile_open(&interp->file_table, filenum, filename,
                          node->data.open_stmt.mode,
                          node->data.open_stmt.access,
                          node->data.open_stmt.lock, reclen);
    if (err) {
        fb_error(err, node->line, filename);
    }

    fbval_release(&fname_val);
}

int qbfile_open(FileTable* ft, int filenum, const char* filename,
                FileMode mode, FileAccess access, FileLock lock, int reclen) {
    FBFile* f = &ft->files[filenum];

    if (f->is_open) return FB_ERR_FILE_ALREADY_OPEN;

    const char* fmode;
    switch (mode) {
        case FMODE_INPUT:  fmode = "r";  break;
        case FMODE_OUTPUT: fmode = "w";  break;
        case FMODE_APPEND: fmode = "a";  break;
        case FMODE_RANDOM: fmode = "r+b"; break;
        case FMODE_BINARY: fmode = "r+b"; break;
        default: return FB_ERR_BAD_FILE_MODE;
    }

    f->fp = fopen(filename, fmode);

    // For RANDOM/BINARY: if file doesn't exist, create it
    if (!f->fp && (mode == FMODE_RANDOM || mode == FMODE_BINARY)) {
        f->fp = fopen(filename, "w+b");
    }

    if (!f->fp) return FB_ERR_FILE_NOT_FOUND;

    f->mode = mode;
    f->access = access;
    f->lock = lock;
    f->reclen = reclen;
    f->is_open = 1;
    strncpy(f->filename, filename, 259);
    f->file_pos = 0;
    f->field_map = NULL;
    f->field_count = 0;

    if (mode == FMODE_RANDOM) {
        f->record_buffer = calloc(1, reclen);
    } else {
        f->record_buffer = NULL;
    }

    return 0;
}
```

---

## 3. Sequential I/O

### 3.1 PRINT #

```
PRINT #filenum, [USING format;] exprlist
```

Same as screen PRINT but output goes to file.

```c
static void exec_print_file(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.print_file.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);
    if (!f) {
        fb_error(FB_ERR_BAD_FILE_MODE, node->line, NULL);
        return;
    }

    // Format output the same as screen PRINT, but write to file
    for (int i = 0; i < node->data.print_file.item_count; i++) {
        FBValue val = eval_expr(interp, node->data.print_file.items[i]);
        char* text = fbval_format_print(&val);
        fprintf(f->fp, "%s", text);
        free(text);
        fbval_release(&val);

        int sep = node->data.print_file.separators[i];
        if (sep == TOK_COMMA) {
            // Tab to next 14-char zone
            int col = (int)ftell(f->fp) % 14;
            int pad = 14 - col;
            for (int j = 0; j < pad; j++) fputc(' ', f->fp);
        }
    }

    if (!node->data.print_file.trailing_sep) {
        fprintf(f->fp, "\r\n"); // FB uses CR+LF in files
    }
}
```

### 3.2 WRITE #

```c
static void exec_write_file(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.print_file.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);

    for (int i = 0; i < node->data.print_file.item_count; i++) {
        if (i > 0) fputc(',', f->fp);
        FBValue val = eval_expr(interp, node->data.print_file.items[i]);
        if (val.type == FB_STRING) {
            fprintf(f->fp, "\"%s\"", val.as.str->data);
        } else {
            double d = fbval_to_double(&val);
            if (val.type == FB_INTEGER || val.type == FB_LONG)
                fprintf(f->fp, "%d", (int)fbval_to_long(&val));
            else
                fprintf(f->fp, "%g", d);
        }
        fbval_release(&val);
    }
    fprintf(f->fp, "\r\n");
}
```

### 3.3 INPUT #

```
INPUT #filenum, varlist
```

Reads comma-delimited values from a sequential file.

```c
static void exec_input_file(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.input_file.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);

    for (int i = 0; i < node->data.input_file.var_count; i++) {
        // Read one field: skip whitespace, handle quoted strings
        char field[4096];
        int pos = 0;
        int in_quotes = 0;
        int ch;

        // Skip leading whitespace and newlines
        while ((ch = fgetc(f->fp)) != EOF) {
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        }

        if (ch == '"') {
            // Quoted string
            in_quotes = 1;
            while ((ch = fgetc(f->fp)) != EOF && ch != '"') {
                field[pos++] = (char)ch;
            }
            // Skip closing quote and comma
            ch = fgetc(f->fp); // comma or newline
        } else {
            // Unquoted value
            if (ch != EOF) field[pos++] = (char)ch;
            while ((ch = fgetc(f->fp)) != EOF) {
                if (ch == ',' || ch == '\r' || ch == '\n') break;
                field[pos++] = (char)ch;
            }
        }
        field[pos] = '\0';

        // Assign to variable
        Symbol* sym = resolve_or_create_var(interp,
                          node->data.input_file.vars[i]);
        if (sym->type == FB_STRING) {
            fbval_release(&sym->value);
            sym->value = fbval_string_from_cstr(field);
        } else {
            double val = atof(field);
            FBValue v = fbval_double(val);
            FBValue coerced = fbval_coerce(&v, sym->type);
            fbval_release(&sym->value);
            sym->value = coerced;
        }
    }
}
```

### 3.4 LINE INPUT #

```c
static void exec_line_input_file(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.line_input_file.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);

    char buf[32768];
    int pos = 0;
    int ch;
    while ((ch = fgetc(f->fp)) != EOF && ch != '\n') {
        if (ch == '\r') continue;
        buf[pos++] = (char)ch;
    }
    buf[pos] = '\0';

    Symbol* sym = resolve_or_create_var(interp,
                      node->data.line_input_file.var);
    fbval_release(&sym->value);
    sym->value = fbval_string_from_cstr(buf);
}
```

---

## 4. Random-Access I/O

### 4.1 GET # / PUT # (Random)

```
GET #filenum [, recnum] [, variable]
PUT #filenum [, recnum] [, variable]
```

```c
static void exec_get_random(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.file_io.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);

    // Optional record number
    long recnum = -1;
    if (node->data.file_io.recnum) {
        recnum = (long)fbval_to_long(&eval_expr(interp,
                     node->data.file_io.recnum));
    }

    if (f->mode == FMODE_RANDOM) {
        if (recnum > 0) {
            fseek(f->fp, (recnum - 1) * f->reclen, SEEK_SET);
        }
        // Read one record into buffer
        memset(f->record_buffer, 0, f->reclen);
        fread(f->record_buffer, 1, f->reclen, f->fp);

        // If variable specified, copy buffer into UDT/string
        if (node->data.file_io.var) {
            copy_buffer_to_var(interp, node->data.file_io.var,
                               f->record_buffer, f->reclen);
        }
        // Also update FIELD-mapped variables
        update_field_vars(interp, f);
    }
}

static void exec_put_random(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.file_io.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);

    long recnum = -1;
    if (node->data.file_io.recnum) {
        recnum = (long)fbval_to_long(&eval_expr(interp,
                     node->data.file_io.recnum));
    }

    if (f->mode == FMODE_RANDOM) {
        if (recnum > 0) {
            fseek(f->fp, (recnum - 1) * f->reclen, SEEK_SET);
        }

        // Copy variable data into record buffer
        if (node->data.file_io.var) {
            copy_var_to_buffer(interp, node->data.file_io.var,
                               f->record_buffer, f->reclen);
        }
        // Write buffer to file
        fwrite(f->record_buffer, 1, f->reclen, f->fp);
        fflush(f->fp);
    }
}
```

---

## 5. Binary I/O

### 5.1 GET / PUT (Binary)

Binary mode reads/writes raw bytes at arbitrary positions.

```c
static void exec_get_binary(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.file_io.filenum));
    FBFile* f = qbfile_get(&interp->file_table, filenum);

    // Optional byte position
    if (node->data.file_io.recnum) {
        long pos = (long)fbval_to_long(&eval_expr(interp,
                       node->data.file_io.recnum));
        fseek(f->fp, pos - 1, SEEK_SET); // FB positions are 1-based
    }

    // Read into variable based on its type size
    Symbol* sym = resolve_or_create_var(interp, node->data.file_io.var);
    switch (sym->type) {
        case FB_INTEGER: {
            int16_t v; fread(&v, 2, 1, f->fp);
            sym->value = fbval_int(v); break;
        }
        case FB_LONG: {
            int32_t v; fread(&v, 4, 1, f->fp);
            sym->value = fbval_long(v); break;
        }
        case FB_SINGLE: {
            float v; fread(&v, 4, 1, f->fp);
            sym->value = fbval_single(v); break;
        }
        case FB_DOUBLE: {
            double v; fread(&v, 8, 1, f->fp);
            sym->value = fbval_double(v); break;
        }
        case FB_STRING: {
            int len = sym->value.as.str->len;
            char* buf = malloc(len);
            fread(buf, 1, len, f->fp);
            fbval_release(&sym->value);
            sym->value = fbval_string(fbstr_new(buf, len));
            free(buf);
            break;
        }
    }
}
```

---

## 6. File Functions

### 6.1 EOF, LOF, LOC, SEEK, FREEFILE

```c
// In eval_builtin_func:
if (_stricmp(name, "EOF") == 0) {
    int fnum = (int)fbval_to_long(&argv[0]);
    FBFile* f = qbfile_get(&interp->file_table, fnum);
    return fbval_int(qbfile_eof(f) ? FB_TRUE : FB_FALSE);
}

if (_stricmp(name, "LOF") == 0) {
    int fnum = (int)fbval_to_long(&argv[0]);
    FBFile* f = qbfile_get(&interp->file_table, fnum);
    return fbval_long((int32_t)qbfile_lof(f));
}

if (_stricmp(name, "LOC") == 0) {
    int fnum = (int)fbval_to_long(&argv[0]);
    FBFile* f = qbfile_get(&interp->file_table, fnum);
    return fbval_long((int32_t)qbfile_loc(f));
}

if (_stricmp(name, "SEEK") == 0) {
    int fnum = (int)fbval_to_long(&argv[0]);
    FBFile* f = qbfile_get(&interp->file_table, fnum);
    return fbval_long((int32_t)qbfile_seek_get(f));
}

if (_stricmp(name, "FREEFILE") == 0) {
    return fbval_int((int16_t)qbfile_freefile(&interp->file_table));
}

// FILEATTR(filenum, attribute)
if (_stricmp(name, "FILEATTR") == 0) {
    int fnum = (int)fbval_to_long(&argv[0]);
    int attr = (int)fbval_to_long(&argv[1]);
    FBFile* f = qbfile_get(&interp->file_table, fnum);
    if (attr == 1) {
        // Return file mode: 1=INPUT, 2=OUTPUT, 4=RANDOM, 8=APPEND, 32=BINARY
        int mode_code = 0;
        switch (f->mode) {
            case FMODE_INPUT:  mode_code = 1; break;
            case FMODE_OUTPUT: mode_code = 2; break;
            case FMODE_RANDOM: mode_code = 4; break;
            case FMODE_APPEND: mode_code = 8; break;
            case FMODE_BINARY: mode_code = 32; break;
        }
        return fbval_int((int16_t)mode_code);
    }
    return fbval_int(0);
}

// INPUT$(n, #filenum)
if (_stricmp(name, "INPUT$") == 0 && argc >= 2) {
    int n = (int)fbval_to_long(&argv[0]);
    int fnum = (int)fbval_to_long(&argv[1]);
    FBFile* f = qbfile_get(&interp->file_table, fnum);
    char* buf = malloc(n + 1);
    int read_count = (int)fread(buf, 1, n, f->fp);
    buf[read_count] = '\0';
    FBString* s = fbstr_new(buf, read_count);
    free(buf);
    return fbval_string(s);
}
```

### 6.2 SEEK Statement

```
SEEK #filenum, position
```

```c
static void exec_seek_stmt(Interpreter* interp, ASTNode* node) {
    int filenum = (int)fbval_to_long(&eval_expr(interp,
                      node->data.seek_stmt.filenum));
    long pos = (long)fbval_to_long(&eval_expr(interp,
                    node->data.seek_stmt.position));
    FBFile* f = qbfile_get(&interp->file_table, filenum);
    qbfile_seek_set(f, pos);
}
```

---

## 7. File Management

### 7.1 CLOSE / RESET

```c
static void exec_close(Interpreter* interp, ASTNode* node) {
    if (node->data.close.file_count == 0) {
        // CLOSE with no arguments → close all files
        filetable_close_all(&interp->file_table);
    } else {
        for (int i = 0; i < node->data.close.file_count; i++) {
            int fnum = (int)fbval_to_long(
                &eval_expr(interp, node->data.close.filenums[i]));
            qbfile_close(&interp->file_table, fnum);
        }
    }
}

// RESET = close all files
static void exec_reset(Interpreter* interp, ASTNode* node) {
    filetable_close_all(&interp->file_table);
}
```

### 7.2 NAME...AS

```
NAME oldname AS newname
```

```c
static void exec_name(Interpreter* interp, ASTNode* node) {
    FBValue old_val = eval_expr(interp, node->data.name_as.old_name);
    FBValue new_val = eval_expr(interp, node->data.name_as.new_name);
    if (rename(old_val.as.str->data, new_val.as.str->data) != 0) {
        fb_error(FB_ERR_FILE_NOT_FOUND, node->line, old_val.as.str->data);
    }
    fbval_release(&old_val);
    fbval_release(&new_val);
}
```

### 7.3 KILL

```c
static void exec_kill(Interpreter* interp, ASTNode* node) {
    FBValue fname = eval_expr(interp, node->data.kill.filename);
    if (remove(fname.as.str->data) != 0) {
        fb_error(FB_ERR_FILE_NOT_FOUND, node->line, fname.as.str->data);
    }
    fbval_release(&fname);
}
```

### 7.4 FILES

```c
static void exec_files(Interpreter* interp, ASTNode* node) {
    // List files matching pattern (or all files in current directory)
    const char* pattern = "*.*";
    if (node->data.files.pattern) {
        FBValue p = eval_expr(interp, node->data.files.pattern);
        pattern = p.as.str->data;
    }
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            printf("%-12s", fd.cFileName);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    // POSIX: use glob() or opendir()/readdir()
#endif
    printf("\n");
}
```

### 7.5 LOCK / UNLOCK

```c
// LOCK #filenum [, {record | start TO end}]
// UNLOCK #filenum [, {record | start TO end}]
// These map to file locking APIs (flock on POSIX, LockFile on Windows)
// For Phase 5: implement as no-op with warning, or use platform locking.
```

---

## 8. Verification Test Files

### 8.1 `tests/verify/phase5_sequential.bas`

```basic
REM Phase 5 Test: Sequential file I/O
' Write
OPEN "test_seq.txt" FOR OUTPUT AS #1
PRINT #1, "Hello, World!"
PRINT #1, 42
PRINT #1, 3.14
WRITE #1, "Alice", 30
CLOSE #1

' Read back
OPEN "test_seq.txt" FOR INPUT AS #1
DIM line1$, line2$, line3$
LINE INPUT #1, line1$
LINE INPUT #1, line2$
LINE INPUT #1, line3$

DIM name$, age%
INPUT #1, name$, age%
CLOSE #1

PRINT line1$
PRINT line2$
PRINT line3$
PRINT name$; age%

KILL "test_seq.txt"
```

**Expected output (`tests/verify/phase5_expected/sequential.txt`):**
```
Hello, World!
 42
 3.14
Alice 30
```

### 8.2 `tests/verify/phase5_random.bas`

```basic
REM Phase 5 Test: Random-access file I/O
TYPE RecordType
    name AS STRING * 20
    age AS INTEGER
END TYPE

DIM rec AS RecordType

OPEN "test_rnd.dat" FOR RANDOM AS #1 LEN = 22

' Write records
rec.name = "Alice"
rec.age = 25
PUT #1, 1, rec

rec.name = "Bob"
rec.age = 30
PUT #1, 2, rec

rec.name = "Charlie"
rec.age = 35
PUT #1, 3, rec

' Read back record 2
GET #1, 2, rec
PRINT RTRIM$(rec.name); rec.age

' Read record 1
GET #1, 1, rec
PRINT RTRIM$(rec.name); rec.age

CLOSE #1
KILL "test_rnd.dat"
```

**Expected output (`tests/verify/phase5_expected/random.txt`):**
```
Bob 30
Alice 25
```

---

## 9. Phase 5 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **OPEN** | Both modern and legacy syntax. All 5 modes work. ACCESS/LOCK clauses parsed. LEN= for RANDOM. File not found → error 53. Already open → error 55. |
| 2 | **PRINT #** | Formats output same as screen PRINT. CR+LF line endings. |
| 3 | **WRITE #** | Comma-delimited, quoted strings. Correct output for numeric types. |
| 4 | **INPUT #** | Reads comma-delimited fields. Handles quoted strings. Type coercion. |
| 5 | **LINE INPUT #** | Reads entire line into string variable. |
| 6 | **GET # / PUT # (RANDOM)** | Reads/writes fixed-length records at specified record numbers. FIELD variables updated on GET. UDT variables supported. |
| 7 | **GET # / PUT # (BINARY)** | Reads/writes raw bytes at specified positions. Variable type determines byte count. |
| 8 | **EOF** | Returns TRUE at end of sequential file. Works for BINARY and RANDOM. |
| 9 | **LOF** | Returns file length in bytes. |
| 10 | **LOC** | Returns current record number (RANDOM) or byte position / 128 (sequential). |
| 11 | **SEEK** | Statement sets position. Function returns current position. 1-based for RANDOM record numbers. |
| 12 | **FREEFILE** | Returns lowest available file number. |
| 13 | **CLOSE / RESET** | Close specific files or all files. Buffers flushed. |
| 14 | **NAME...AS** | Renames files. Error on failure. |
| 15 | **KILL** | Deletes files. Error on failure. |
| 16 | **FILES** | Lists directory contents matching pattern. |
| 17 | **File numbers** | Support #1 through #255. |
| 18 | **Milestone Programs** | File copy and CSV processor produce correct output. |
| 19 | **No Leaks** | All file handles closed on program exit. Temporary files cleaned up. |

---

## 10. Key Implementation Warnings

1. **CR+LF in files:** FB uses CR+LF (`\r\n`) for line endings in text files. Open files in binary mode ("rb"/"wb") and handle CR+LF manually, or use platform text mode carefully.

2. **RANDOM mode record alignment:** Records are fixed-width. GET/PUT always read/write exactly `reclen` bytes. Unused bytes in the record are typically spaces for strings and zeros for numerics.

3. **FIELD vs TYPE'd variables:** FIELD maps portions of the record buffer to string variables (legacy FB approach). TYPE'd variables can be used directly with GET/PUT (modern approach). Both must be supported.

4. **INPUT # comma parsing:** Inside quoted strings, commas are literal and don't split fields. Outside quotes, commas and newlines are delimiters. Whitespace around commas is trimmed for numeric fields but NOT for string fields.

5. **LOC for different modes:** INPUT mode: number of records read. RANDOM mode: last record read/written. BINARY mode: byte position. OUTPUT mode: byte position / 128.

6. **SEEK positions:** For RANDOM mode, SEEK position is 1-based record number. For BINARY mode, SEEK position is 1-based byte offset. For sequential, SEEK is 1-based byte offset.

7. **Close all files on program exit:** The interpreter must close all files when execution ends (END, STOP, or reaching the end of the program). Implement in `interp_free()`.
