/*
 * fileio.h — Portable file I/O abstraction for FBasic interpreter
 *
 * This module provides a platform-independent file I/O API.  The default
 * implementation uses standard C stdio; swap fb_fileops_platform() for an
 * alternative backend to port to other platforms.
 */
#ifndef FILEIO_H
#define FILEIO_H

#include "value.h"
#include "platform.h"
#include <stddef.h>

#define FB_MAX_FILES 255

/* ---- File mode / access enums ---- */

typedef enum {
    FMODE_CLOSED = 0,
    FMODE_INPUT,       /* Sequential input  */
    FMODE_OUTPUT,      /* Sequential output  */
    FMODE_APPEND,      /* Sequential append  */
    FMODE_RANDOM,      /* Random access      */
    FMODE_BINARY       /* Binary access      */
} FBFileMode;

typedef enum {
    FACCESS_READ_WRITE = 0,
    FACCESS_READ,
    FACCESS_WRITE
} FBFileAccess;

typedef enum {
    FLOCK_DEFAULT = 0,
    FLOCK_SHARED,
    FLOCK_READ,
    FLOCK_WRITE,
    FLOCK_READ_WRITE
} FBFileLock;

/* ---- Per-handle state ---- */

typedef struct FBFile {
    void*         handle;         /* Opaque platform handle              */
    FBFileMode    mode;
    FBFileAccess  access;
    FBFileLock    lock;
    int           reclen;         /* Record length (RANDOM, default 128) */
    int           is_open;
    char          filename[260];

    /* FIELD mappings */
    struct {
        char  var_name[42];
        int   offset;
        int   width;
    }*            field_map;
    int           field_count;

    /* Random-access record buffer */
    char*         record_buffer;
} FBFile;

/* ---- File table ---- */

typedef struct {
    FBFile            files[FB_MAX_FILES + 1]; /* [1..255]; [0] unused */
    const FBFileOps*  ops;                     /* Platform backend     */
} FBFileTable;

void fb_filetable_init(FBFileTable* ft, const FBFileOps* ops);
void fb_filetable_close_all(FBFileTable* ft);

/* ---- High-level operations ---- */

int      fb_file_open(FBFileTable* ft, int filenum, const char* filename,
                       FBFileMode mode, FBFileAccess access,
                       FBFileLock lock, int reclen);
void     fb_file_close(FBFileTable* ft, int filenum);
FBFile*  fb_file_get(FBFileTable* ft, int filenum);
int      fb_file_freefile(const FBFileTable* ft);

/* Query helpers */
int      fb_file_eof(FBFileTable* ft, FBFile* f);
long     fb_file_lof(FBFileTable* ft, FBFile* f);
long     fb_file_loc(FBFileTable* ft, FBFile* f);
long     fb_file_seek_get(FBFileTable* ft, FBFile* f);
void     fb_file_seek_set(FBFileTable* ft, FBFile* f, long pos);

/* Random / binary record I/O */
int      fb_file_get_record(FBFileTable* ft, FBFile* f, long recnum);
int      fb_file_put_record(FBFileTable* ft, FBFile* f, long recnum);

/* Character-level I/O for sequential parsing */
int      fb_file_read_char(FBFileTable* ft, FBFile* f);
int      fb_file_write_char(FBFileTable* ft, FBFile* f, int ch);
size_t   fb_file_write_bytes(FBFileTable* ft, FBFile* f,
                              const void* buf, size_t n);
size_t   fb_file_read_bytes(FBFileTable* ft, FBFile* f,
                             void* buf, size_t n);

#endif
