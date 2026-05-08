#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

/* ---- Platform abstraction ---- */

typedef struct FBFileOps {
    void*  (*open)(const char* path, const char* mode);
    int    (*close)(void* handle);
    size_t (*read)(void* handle, void* buf, size_t size);
    size_t (*write)(void* handle, const void* buf, size_t size);
    int    (*seek)(void* handle, long offset, int whence);
    long   (*tell)(void* handle);
    int    (*eof)(void* handle);
    int    (*flush)(void* handle);
    int    (*getc)(void* handle);
    int    (*putc)(int ch, void* handle);
    int    (*remove)(const char* path);
    int    (*rename)(const char* oldpath, const char* newpath);
    long   (*length)(void* handle);
} FBFileOps;

typedef struct FBSysOps {
    /* SHELL support */
    int    (*shell_exec)(const char* command);   /* Run command, return exit code */
    int    (*shell_interactive)(void);           /* Open interactive shell */

    /* Environment variables */
    const char* (*environ_get)(const char* name);               /* Get env var by name */
    const char* (*environ_get_nth)(int n);                      /* Get nth env string (1-based) */
    int         (*environ_set)(const char* name, const char* value); /* Set env var */

    /* Directory operations */
    int    (*chdir)(const char* path);
    int    (*mkdir)(const char* path);
    int    (*rmdir)(const char* path);

    /* Date / time */
    void   (*get_date)(int* year, int* month, int* day);
    void   (*get_time)(int* hour, int* minute, int* second);
    double (*timer)(void);                       /* Seconds since midnight */

    /* Console I/O */
    void   (*console_init)(void);               /* Enter raw/semi-raw mode */
    void   (*console_shutdown)(void);           /* Restore normal mode */
    int    (*console_inkey)(void);              /* Non-blocking key read (0 = none) */
    void   (*console_cls)(void);               /* Clear screen */
    void   (*console_locate)(int row, int col); /* Move cursor (1-based) */
    void   (*console_color)(int fg, int bg);   /* Set text color */
    int    (*console_csrlin)(void);            /* Current cursor row (1-based) */
    int    (*console_pos)(void);               /* Current cursor col (1-based) */
    void   (*console_width)(int cols, int rows);/* Set screen dimensions */
    void   (*console_beep)(void);              /* Emit BEL */

    /* Text output */
    void   (*print_str)(const char* text);     /* Print string to stdout */
    void   (*print_char)(char ch);             /* Print single char to stdout */
    void   (*flush_output)(void);              /* Flush stdout */
    void   (*error_str)(const char* text);     /* Print string to stderr */
    void   (*error_flush)(void);               /* Flush stderr */

    /* Heap allocation */
    void*  (*heap_alloc)(size_t size);           /* malloc equivalent */
    void*  (*heap_calloc)(size_t count, size_t size); /* calloc equivalent */
    void*  (*heap_realloc)(void* ptr, size_t size);   /* realloc equivalent */
    void   (*heap_free)(void* ptr);             /* free equivalent */

    /* Low-level stubs (DOS compat) */
    int    (*peek)(long address);                /* PEEK(addr) → byte value */
    void   (*poke)(long address, int value);     /* POKE addr, value */
} FBSysOps;

/* Returns pointer to the default stdio-backed ops table. */
const FBFileOps* fb_fileops_platform(void);

/* Returns pointer to the default platform-specific ops table. */
const FBSysOps* fb_sysops_platform(void);

/* ---- Convenience macros for heap allocation via system API ---- */
#define fb_malloc(size)          fb_sysops_platform()->heap_alloc((size))
#define fb_calloc(count, size)   fb_sysops_platform()->heap_calloc((count), (size))
#define fb_realloc(ptr, size)    fb_sysops_platform()->heap_realloc((ptr), (size))
#define fb_free(ptr)             fb_sysops_platform()->heap_free((ptr))

#endif
