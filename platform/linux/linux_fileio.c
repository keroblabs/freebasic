/*
 * fileio.c — Standard-IO-backed implementation of the portable file I/O API
 */
#include "fileio.h"
#include "platform.h"
#include "platform.h"
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

const FBFileOps* fb_fileops_platform(void) {
    return &stdio_ops;
}
