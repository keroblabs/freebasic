/*
 * console.c — Console I/O delegating to the system API vtable
 */
#include "console.h"
#include "system_api.h"

void console_init(void) {
    fb_sysops_default()->console_init();
}

void console_shutdown(void) {
    fb_sysops_default()->console_shutdown();
}

int console_inkey(void) {
    return fb_sysops_default()->console_inkey();
}

void console_cls(void) {
    fb_sysops_default()->console_cls();
}

void console_locate(int row, int col) {
    fb_sysops_default()->console_locate(row, col);
}

void console_color(int fg, int bg) {
    fb_sysops_default()->console_color(fg, bg);
}

int console_csrlin(void) {
    return fb_sysops_default()->console_csrlin();
}

int console_pos(void) {
    return fb_sysops_default()->console_pos();
}

void console_width(int cols, int rows) {
    fb_sysops_default()->console_width(cols, rows);
}

void console_beep(void) {
    fb_sysops_default()->console_beep();
}
