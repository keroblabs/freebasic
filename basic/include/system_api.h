/*
 * system_api.h — Portable system-level abstraction for FBasic interpreter
 *
 * This module provides a platform-independent API for OS-level operations:
 * SHELL, ENVIRON, directory operations, date/time, and low-level stubs.
 * The default implementation uses POSIX/Linux syscalls; swap
 * fb_sysops_default() for an alternative backend to port to other platforms.
 */
#ifndef SYSTEM_API_H
#define SYSTEM_API_H

#include <stddef.h>

/* ---- Platform abstraction vtable ---- */

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

    /* Low-level stubs (DOS compat) */
    int    (*peek)(long address);                /* PEEK(addr) → byte value */
    void   (*poke)(long address, int value);     /* POKE addr, value */
} FBSysOps;

/* Returns pointer to the default platform-specific ops table. */
const FBSysOps* fb_sysops_default(void);

#endif /* SYSTEM_API_H */
