/*
 * console.c — Console I/O delegating to the system API vtable
 */
#include "console.h"
#include "platform.h"
#include "platform.h"

void console_init(void) {
    fb_sysops_platform()->console_init();
}

void console_shutdown(void) {
    fb_sysops_platform()->console_shutdown();
}

int console_inkey(void) {
    return fb_sysops_platform()->console_inkey();
}

void console_cls(void) {
    fb_sysops_platform()->console_cls();
}

void console_locate(int row, int col) {
    fb_sysops_platform()->console_locate(row, col);
}

void console_color(int fg, int bg) {
    fb_sysops_platform()->console_color(fg, bg);
}

int console_csrlin(void) {
    return fb_sysops_platform()->console_csrlin();
}

int console_pos(void) {
    return fb_sysops_platform()->console_pos();
}

void console_width(int cols, int rows) {
    fb_sysops_platform()->console_width(cols, rows);
}

void console_beep(void) {
    fb_sysops_platform()->console_beep();
}
