/*
 * console.c — Console I/O using ANSI escape sequences (POSIX)
 */
#include "console.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/ioctl.h>
#endif

/* ---- Terminal raw mode (POSIX) ---- */

#ifndef _WIN32
static struct termios orig_termios;
static int            termios_saved = 0;
#endif

void console_init(void) {
#ifndef _WIN32
    if (!isatty(STDIN_FILENO)) return;
    if (termios_saved) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios_saved = 1;
    /* Enter a semi-raw mode so INKEY$ works */
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
}

void console_shutdown(void) {
#ifndef _WIN32
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_saved = 0;
    }
#endif
}

int console_inkey(void) {
#ifdef _WIN32
    if (_kbhit()) return _getch();
    return 0;
#else
    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) return ch;
    return 0;
#endif
}

void console_cls(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void console_locate(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

/* FB color index to ANSI color code */
static int fb_fg_to_ansi(int fg) {
    /* FB: 0=black,1=blue,2=green,3=cyan,4=red,5=magenta,6=brown,7=white
       8-15 = bright versions */
    static const int map[16] = {
        30, 34, 32, 36, 31, 35, 33, 37,  /* 0-7 normal */
        90, 94, 92, 96, 91, 95, 93, 97   /* 8-15 bright */
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

void console_color(int fg, int bg) {
    printf("\033[%d;%dm", fb_fg_to_ansi(fg), fb_bg_to_ansi(bg));
    fflush(stdout);
}

int console_csrlin(void) {
#ifndef _WIN32
    /* Query cursor position via ANSI DSR */
    printf("\033[6n");
    fflush(stdout);

    /* Need canonical off to read the response */
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
#else
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
    return info.dwCursorPosition.Y + 1;
#endif
}

int console_pos(void) {
#ifndef _WIN32
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
#else
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
    return info.dwCursorPosition.X + 1;
#endif
}

void console_width(int cols, int rows) {
    printf("\033[8;%d;%dt", rows, cols);
    fflush(stdout);
}

void console_beep(void) {
    printf("\007");
    fflush(stdout);
}
