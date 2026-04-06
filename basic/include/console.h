#ifndef CONSOLE_H
#define CONSOLE_H

/* Initialize console for raw input (INKEY$ support) */
void console_init(void);

/* Restore console to normal mode */
void console_shutdown(void);

/* Non-blocking key read. Returns 0 if no key available. */
int console_inkey(void);

/* Clear screen */
void console_cls(void);

/* Position cursor (1-based row, col) */
void console_locate(int row, int col);

/* Set text color (foreground 0-15, background 0-7) */
void console_color(int fg, int bg);

/* Get current cursor row (1-based) */
int console_csrlin(void);

/* Get current cursor column (1-based) */
int console_pos(void);

/* Set screen width/height */
void console_width(int cols, int rows);

/* Emit BEL character */
void console_beep(void);

#endif
