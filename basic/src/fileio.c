/*
 * fileio.c — Standard-IO-backed implementation of the portable file I/O API
 */
#include "fileio.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  stdio backend — default platform ops
 * ================================================================ */

static void* stdio_open(const char* path, const char* mode) {
    return fopen(path, mode);
}

static int stdio_close(void* handle) {
    return fclose((FILE*)handle);
}

static size_t stdio_read(void* handle, void* buf, size_t size) {
    return fread(buf, 1, size, (FILE*)handle);
}

static size_t stdio_write(void* handle, const void* buf, size_t size) {
    return fwrite(buf, 1, size, (FILE*)handle);
}

static int stdio_seek(void* handle, long offset, int whence) {
    return fseek((FILE*)handle, offset, whence);
}

static long stdio_tell(void* handle) {
    return ftell((FILE*)handle);
}

static int stdio_eof(void* handle) {
    return feof((FILE*)handle);
}

static int stdio_flush(void* handle) {
    return fflush((FILE*)handle);
}

static int stdio_getc(void* handle) {
    return fgetc((FILE*)handle);
}

static int stdio_putc(int ch, void* handle) {
    return fputc(ch, (FILE*)handle);
}

static int stdio_remove(const char* path) {
    return remove(path);
}

static int stdio_rename(const char* oldpath, const char* newpath) {
    return rename(oldpath, newpath);
}

static long stdio_length(void* handle) {
    FILE* fp = (FILE*)handle;
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return len;
}

static const FBFileOps stdio_ops = {
    .open   = stdio_open,
    .close  = stdio_close,
    .read   = stdio_read,
    .write  = stdio_write,
    .seek   = stdio_seek,
    .tell   = stdio_tell,
    .eof    = stdio_eof,
    .flush  = stdio_flush,
    .getc   = stdio_getc,
    .putc   = stdio_putc,
    .remove = stdio_remove,
    .rename = stdio_rename,
    .length = stdio_length,
};

const FBFileOps* fb_fileops_default(void) {
    return &stdio_ops;
}

/* ================================================================
 *  File table management
 * ================================================================ */

void fb_filetable_init(FBFileTable* ft, const FBFileOps* ops) {
    memset(ft, 0, sizeof(*ft));
    ft->ops = ops ? ops : fb_fileops_default();
}

void fb_filetable_close_all(FBFileTable* ft) {
    for (int i = 1; i <= FB_MAX_FILES; i++) {
        if (ft->files[i].is_open) {
            fb_file_close(ft, i);
        }
    }
}

/* ================================================================
 *  High-level operations
 * ================================================================ */

int fb_file_open(FBFileTable* ft, int filenum, const char* filename,
                  FBFileMode mode, FBFileAccess access,
                  FBFileLock lock, int reclen) {
    if (filenum < 1 || filenum > FB_MAX_FILES)
        return FB_ERR_BAD_FILE_NUMBER;

    FBFile* f = &ft->files[filenum];
    if (f->is_open)
        return FB_ERR_FILE_ALREADY_OPEN;

    const char* cmode;
    switch (mode) {
        case FMODE_INPUT:  cmode = "r";   break;
        case FMODE_OUTPUT: cmode = "w";   break;
        case FMODE_APPEND: cmode = "a";   break;
        case FMODE_RANDOM: cmode = "r+b"; break;
        case FMODE_BINARY: cmode = "r+b"; break;
        default: return FB_ERR_BAD_FILE_MODE;
    }

    void* h = ft->ops->open(filename, cmode);

    /* RANDOM/BINARY: create if not found */
    if (!h && (mode == FMODE_RANDOM || mode == FMODE_BINARY)) {
        h = ft->ops->open(filename, "w+b");
    }

    if (!h) return FB_ERR_FILE_NOT_FOUND;

    f->handle   = h;
    f->mode     = mode;
    f->access   = access;
    f->lock     = lock;
    f->reclen   = (reclen > 0) ? reclen : 128;
    f->is_open  = 1;
    strncpy(f->filename, filename, sizeof(f->filename) - 1);
    f->filename[sizeof(f->filename) - 1] = '\0';
    f->field_map     = NULL;
    f->field_count   = 0;
    f->record_buffer = NULL;

    if (mode == FMODE_RANDOM) {
        f->record_buffer = calloc(1, f->reclen);
    }

    return 0;
}

void fb_file_close(FBFileTable* ft, int filenum) {
    if (filenum < 1 || filenum > FB_MAX_FILES) return;
    FBFile* f = &ft->files[filenum];
    if (!f->is_open) return;

    ft->ops->close(f->handle);
    f->handle  = NULL;
    f->is_open = 0;
    f->mode    = FMODE_CLOSED;
    free(f->field_map);
    f->field_map   = NULL;
    f->field_count = 0;
    free(f->record_buffer);
    f->record_buffer = NULL;
}

FBFile* fb_file_get(FBFileTable* ft, int filenum) {
    if (filenum < 1 || filenum > FB_MAX_FILES) return NULL;
    FBFile* f = &ft->files[filenum];
    return f->is_open ? f : NULL;
}

int fb_file_freefile(const FBFileTable* ft) {
    for (int i = 1; i <= FB_MAX_FILES; i++) {
        if (!ft->files[i].is_open) return i;
    }
    return -1;  /* all slots used */
}

/* ================================================================
 *  Query helpers
 * ================================================================ */

int fb_file_eof(FBFileTable* ft, FBFile* f) {
    if (!f || !f->is_open) return 1;

    /* For sequential input, peek ahead to detect EOF */
    if (f->mode == FMODE_INPUT) {
        int ch = ft->ops->getc(f->handle);
        if (ch == EOF) return 1;
        /* push back — re-seek one byte */
        long pos = ft->ops->tell(f->handle);
        ft->ops->seek(f->handle, pos - 1, 0 /* SEEK_SET */);
        return 0;
    }

    /* RANDOM: past last record? */
    if (f->mode == FMODE_RANDOM) {
        long pos = ft->ops->tell(f->handle);
        long len = ft->ops->length(f->handle);
        return pos >= len;
    }

    /* BINARY */
    long pos = ft->ops->tell(f->handle);
    long len = ft->ops->length(f->handle);
    return pos >= len;
}

long fb_file_lof(FBFileTable* ft, FBFile* f) {
    if (!f || !f->is_open) return 0;
    return ft->ops->length(f->handle);
}

long fb_file_loc(FBFileTable* ft, FBFile* f) {
    if (!f || !f->is_open) return 0;
    long pos = ft->ops->tell(f->handle);

    if (f->mode == FMODE_RANDOM) {
        return (pos / f->reclen);  /* last record read/written (0-based internally) */
    }
    /* Sequential / binary: byte pos / 128 for sequential, byte pos for binary */
    if (f->mode == FMODE_BINARY) return pos;
    return (pos > 0) ? ((pos - 1) / 128 + 1) : 0;
}

long fb_file_seek_get(FBFileTable* ft, FBFile* f) {
    if (!f || !f->is_open) return 0;
    long pos = ft->ops->tell(f->handle);

    if (f->mode == FMODE_RANDOM) {
        return (pos / f->reclen) + 1;  /* 1-based record number */
    }
    return pos + 1;  /* 1-based byte offset */
}

void fb_file_seek_set(FBFileTable* ft, FBFile* f, long pos) {
    if (!f || !f->is_open) return;

    if (f->mode == FMODE_RANDOM) {
        ft->ops->seek(f->handle, (pos - 1) * f->reclen, 0 /* SEEK_SET */);
    } else {
        ft->ops->seek(f->handle, pos - 1, 0 /* SEEK_SET */);
    }
}

/* ================================================================
 *  Record I/O (random-access)
 * ================================================================ */

int fb_file_get_record(FBFileTable* ft, FBFile* f, long recnum) {
    if (!f || !f->record_buffer) return -1;

    if (recnum > 0) {
        ft->ops->seek(f->handle, (recnum - 1) * f->reclen, 0 /* SEEK_SET */);
    }

    memset(f->record_buffer, 0, f->reclen);
    ft->ops->read(f->handle, f->record_buffer, f->reclen);
    return 0;
}

int fb_file_put_record(FBFileTable* ft, FBFile* f, long recnum) {
    if (!f || !f->record_buffer) return -1;

    if (recnum > 0) {
        ft->ops->seek(f->handle, (recnum - 1) * f->reclen, 0 /* SEEK_SET */);
    }

    ft->ops->write(f->handle, f->record_buffer, f->reclen);
    ft->ops->flush(f->handle);
    return 0;
}

/* ================================================================
 *  Byte-level I/O helpers
 * ================================================================ */

int fb_file_read_char(FBFileTable* ft, FBFile* f) {
    if (!f || !f->is_open) return -1;
    return ft->ops->getc(f->handle);
}

int fb_file_write_char(FBFileTable* ft, FBFile* f, int ch) {
    if (!f || !f->is_open) return -1;
    return ft->ops->putc(ch, f->handle);
}

size_t fb_file_write_bytes(FBFileTable* ft, FBFile* f,
                            const void* buf, size_t n) {
    if (!f || !f->is_open) return 0;
    return ft->ops->write(f->handle, buf, n);
}

size_t fb_file_read_bytes(FBFileTable* ft, FBFile* f,
                           void* buf, size_t n) {
    if (!f || !f->is_open) return 0;
    return ft->ops->read(f->handle, buf, n);
}
