/*
 * system_api.c — Linux/POSIX implementation of the system abstraction layer
 *
 * Provides SHELL, ENVIRON, directory, date/time, and low-level stub
 * operations.  Replace fb_sysops_default() for other platforms.
 */
#include "system_api.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---- SHELL ---- */

static int linux_shell_exec(const char* command) {
    return system(command);
}

static int linux_shell_interactive(void) {
    const char* sh = getenv("SHELL");
    if (!sh) sh = "/bin/sh";
    return system(sh);
}

/* ---- Environment ---- */

extern char** environ;

static const char* linux_environ_get(const char* name) {
    return getenv(name);
}

static const char* linux_environ_get_nth(int n) {
    if (!environ || n < 1) return NULL;
    int i = 0;
    while (environ[i]) {
        if (i + 1 == n) return environ[i];
        i++;
    }
    return NULL;
}

static int linux_environ_set(const char* name, const char* value) {
    return setenv(name, value, 1);
}

/* ---- Directory operations ---- */

static int linux_chdir(const char* path) {
    return chdir(path);
}

static int linux_mkdir(const char* path) {
    return mkdir(path, 0755);
}

static int linux_rmdir(const char* path) {
    return rmdir(path);
}

/* ---- Date / Time ---- */

static void linux_get_date(int* year, int* month, int* day) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    *year  = t->tm_year + 1900;
    *month = t->tm_mon + 1;
    *day   = t->tm_mday;
}

static void linux_get_time(int* hour, int* minute, int* second) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    *hour   = t->tm_hour;
    *minute = t->tm_min;
    *second = t->tm_sec;
}

static double linux_timer(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm* t = localtime(&ts.tv_sec);
    return t->tm_hour * 3600.0 + t->tm_min * 60.0
         + t->tm_sec + ts.tv_nsec / 1e9;
}

/* ---- Low-level stubs ---- */

static int linux_peek(long address) {
    (void)address;
    return 0;
}

static void linux_poke(long address, int value) {
    (void)address;
    (void)value;
}

/* ---- Vtable ---- */

static const FBSysOps linux_sysops = {
    .shell_exec        = linux_shell_exec,
    .shell_interactive = linux_shell_interactive,
    .environ_get       = linux_environ_get,
    .environ_get_nth   = linux_environ_get_nth,
    .environ_set       = linux_environ_set,
    .chdir             = linux_chdir,
    .mkdir             = linux_mkdir,
    .rmdir             = linux_rmdir,
    .get_date          = linux_get_date,
    .get_time          = linux_get_time,
    .timer             = linux_timer,
    .peek              = linux_peek,
    .poke              = linux_poke,
};

const FBSysOps* fb_sysops_default(void) {
    return &linux_sysops;
}
