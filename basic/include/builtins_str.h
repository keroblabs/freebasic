#ifndef BUILTINS_STR_H
#define BUILTINS_STR_H

#include "value.h"

/* Check if a function name is a string builtin */
int builtin_str_lookup(const char* name);

/* Call a string builtin by name */
FBValue builtin_str_call(const char* name, FBValue* args, int argc, int line);

#endif
