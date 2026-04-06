#ifndef BUILTINS_MATH_H
#define BUILTINS_MATH_H

#include "value.h"

/* Forward declare */
struct Interpreter;

/* Check if a function name is a math builtin */
int builtin_math_lookup(const char* name);

/* Call a math builtin by name */
FBValue builtin_math_call(const char* name, FBValue* args, int argc,
                          int line, struct Interpreter* interp);

#endif
