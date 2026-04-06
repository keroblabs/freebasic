#ifndef BUILTINS_CONVERT_H
#define BUILTINS_CONVERT_H

#include "value.h"

int     builtin_convert_lookup(const char* name);
FBValue builtin_convert_call(const char* name, FBValue* args, int argc, int line);

#endif
