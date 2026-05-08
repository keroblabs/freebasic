/*
 * system_api.c — Linux/POSIX implementation of the system abstraction layer
 *
 * Provides SHELL, ENVIRON, directory, date/time, console I/O, text output,
 * and low-level stub operations.  Replace fb_sysops_default() for other platforms.
 */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>

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

/* ---- Console I/O ---- */

static struct termios orig_termios;
static int            termios_saved = 0;

static void linux_console_init(void) {
    if (!isatty(STDIN_FILENO)) return;
    if (termios_saved) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios_saved = 1;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void linux_console_shutdown(void) {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_saved = 0;
    }
}

static int linux_console_inkey(void) {
    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) return ch;
    return 0;
}

static void linux_console_cls(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void linux_console_locate(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

static int fb_fg_to_ansi(int fg) {
    static const int map[16] = {
        30, 34, 32, 36, 31, 35, 33, 37,
        90, 94, 92, 96, 91, 95, 93, 97
    };
    if (fg < 0) fg = 0;
    if (fg > 15) fg = 15;
    return map[fg];
}

static int fb_bg_to_ansi(int bg) {
    static const int map[8] = {
        40, 44, 42, 46, 41, 45, 43, 47
    };
    if (bg < 0) bg = 0;
    if (bg > 7) bg = 7;
    return map[bg];
}

static void linux_console_color(int fg, int bg) {
    printf("\033[%d;%dm", fb_fg_to_ansi(fg), fb_bg_to_ansi(bg));
    fflush(stdout);
}

static int linux_console_csrlin(void) {
    printf("\033[6n");
    fflush(stdout);
    char buf[32];
    int i = 0;
    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n != 1) break;
        buf[i++] = c;
        if (c == 'R' || i >= 30) break;
    }
    buf[i] = '\0';
    int row = 1, col = 1;
    sscanf(buf, "\033[%d;%dR", &row, &col);
    return row;
}

static int linux_console_pos(void) {
    printf("\033[6n");
    fflush(stdout);
    char buf[32];
    int i = 0;
    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n != 1) break;
        buf[i++] = c;
        if (c == 'R' || i >= 30) break;
    }
    buf[i] = '\0';
    int row = 1, col = 1;
    sscanf(buf, "\033[%d;%dR", &row, &col);
    return col;
}

static void linux_console_width(int cols, int rows) {
    printf("\033[8;%d;%dt", rows, cols);
    fflush(stdout);
}

static void linux_console_beep(void) {
    printf("\007");
    fflush(stdout);
}

/* ---- Heap allocation ---- */

static void* linux_heap_alloc(size_t size) {
    return malloc(size);
}

static void* linux_heap_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

static void* linux_heap_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

static void linux_heap_free(void* ptr) {
    free(ptr);
}

/* ---- Text output ---- */

static void linux_print_str(const char* text) {
    fputs(text, stdout);
}

static void linux_print_char(char ch) {
    fputc(ch, stdout);
}

static void linux_flush_output(void) {
    fflush(stdout);
}

static void linux_error_str(const char* text) {
    fputs(text, stderr);
}

static void linux_error_flush(void) {
    fflush(stderr);
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
    .console_init      = linux_console_init,
    .console_shutdown  = linux_console_shutdown,
    .console_inkey     = linux_console_inkey,
    .console_cls       = linux_console_cls,
    .console_locate    = linux_console_locate,
    .console_color     = linux_console_color,
    .console_csrlin    = linux_console_csrlin,
    .console_pos       = linux_console_pos,
    .console_width     = linux_console_width,
    .console_beep      = linux_console_beep,
    .print_str         = linux_print_str,
    .print_char        = linux_print_char,
    .flush_output      = linux_flush_output,
    .error_str         = linux_error_str,
    .error_flush       = linux_error_flush,
    .heap_alloc        = linux_heap_alloc,
    .heap_calloc       = linux_heap_calloc,
    .heap_realloc      = linux_heap_realloc,
    .heap_free         = linux_heap_free,
    .peek              = linux_peek,
    .poke              = linux_poke,
};

const FBSysOps* fb_sysops_platform(void) {
    return &linux_sysops;
}
