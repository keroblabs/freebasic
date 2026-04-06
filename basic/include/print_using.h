#ifndef PRINT_USING_H
#define PRINT_USING_H

#include "value.h"

/* Execute PRINT USING: format values according to format string and print. */
void exec_print_using(const char* fmt, FBValue* values, int value_count);
char* format_print_using(const char* fmt, FBValue* values, int value_count);

#endif
