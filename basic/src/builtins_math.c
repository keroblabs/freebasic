/*
 * builtins_math.c — Math built-in functions for FBasic interpreter
 */
#include "builtins_math.h"
#include "interpreter.h"
#include "coerce.h"
#include "error.h"
#include <math.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- RND state (per-interpreter via interp pointer) ---- */

static double do_rnd(Interpreter* interp, float n) {
    if (n < 0.0f) {
        /* Reseed with the argument */
        unsigned int seed;
        memcpy(&seed, &n, sizeof(unsigned int));
        interp->rnd_seed = seed;
    }

    if (n != 0.0f) {
        /* Advance the LCG */
        interp->rnd_seed = (interp->rnd_seed * 16598013u + 12820163u) & 0xFFFFFF;
    }

    double result = (double)interp->rnd_seed / 16777216.0;
    interp->last_rnd = result;
    return result;
}

/* ---- Math function implementations ---- */

static FBValue fn_abs(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)interp;
    double v = fbval_to_double(&args[0]);
    FBValue r = fbval_double(fabs(v));
    return fbval_coerce(&r, args[0].type == FB_STRING ? FB_DOUBLE : args[0].type);
}

static FBValue fn_int(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    double v = fbval_to_double(&args[0]);
    return fbval_double(floor(v));
}

static FBValue fn_fix(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    double v = fbval_to_double(&args[0]);
    return fbval_double(v >= 0 ? floor(v) : ceil(v));
}

static FBValue fn_sqr(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)interp;
    double v = fbval_to_double(&args[0]);
    if (v < 0) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "SQR of negative");
        return fbval_double(0);
    }
    return fbval_double(sqrt(v));
}

static FBValue fn_sin(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_double(sin(fbval_to_double(&args[0])));
}

static FBValue fn_cos(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_double(cos(fbval_to_double(&args[0])));
}

static FBValue fn_tan(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_double(tan(fbval_to_double(&args[0])));
}

static FBValue fn_atn(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_double(atan(fbval_to_double(&args[0])));
}

static FBValue fn_log(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)interp;
    double v = fbval_to_double(&args[0]);
    if (v <= 0) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "LOG of non-positive");
        return fbval_double(0);
    }
    return fbval_double(log(v));
}

static FBValue fn_exp(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_double(exp(fbval_to_double(&args[0])));
}

static FBValue fn_sgn(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    double v = fbval_to_double(&args[0]);
    int16_t s = (v > 0) ? 1 : (v < 0) ? -1 : 0;
    return fbval_int(s);
}

static FBValue fn_rnd(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)line;
    float n = (argc > 0) ? (float)fbval_to_double(&args[0]) : 1.0f;
    double r = do_rnd(interp, n);
    return fbval_single((float)r);
}

static FBValue fn_cint(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_coerce(&args[0], FB_INTEGER);
}

static FBValue fn_clng(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_coerce(&args[0], FB_LONG);
}

static FBValue fn_csng(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_coerce(&args[0], FB_SINGLE);
}

static FBValue fn_cdbl(FBValue* args, int argc, int line, Interpreter* interp) {
    (void)argc; (void)line; (void)interp;
    return fbval_coerce(&args[0], FB_DOUBLE);
}

/* ---- Lookup table ---- */

typedef FBValue (*MathFuncFn)(FBValue* args, int argc, int line, Interpreter* interp);

typedef struct {
    const char* name;
    int         min_args;
    int         max_args;
    MathFuncFn  func;
} MathFuncEntry;

static const MathFuncEntry math_funcs[] = {
    { "ABS",   1, 1, fn_abs  },
    { "INT",   1, 1, fn_int  },
    { "FIX",   1, 1, fn_fix  },
    { "SQR",   1, 1, fn_sqr  },
    { "SIN",   1, 1, fn_sin  },
    { "COS",   1, 1, fn_cos  },
    { "TAN",   1, 1, fn_tan  },
    { "ATN",   1, 1, fn_atn  },
    { "LOG",   1, 1, fn_log  },
    { "EXP",   1, 1, fn_exp  },
    { "SGN",   1, 1, fn_sgn  },
    { "RND",   0, 1, fn_rnd  },
    { "CINT",  1, 1, fn_cint },
    { "CLNG",  1, 1, fn_clng },
    { "CSNG",  1, 1, fn_csng },
    { "CDBL",  1, 1, fn_cdbl },
    { NULL,    0, 0, NULL    }
};

int builtin_math_lookup(const char* name) {
    for (int i = 0; math_funcs[i].name; i++) {
        if (strcasecmp(name, math_funcs[i].name) == 0) return 1;
    }
    return 0;
}

FBValue builtin_math_call(const char* name, FBValue* args, int argc,
                          int line, Interpreter* interp) {
    for (int i = 0; math_funcs[i].name; i++) {
        if (strcasecmp(name, math_funcs[i].name) == 0) {
            if (argc < math_funcs[i].min_args) {
                fb_error(FB_ERR_ARGUMENT_COUNT_MISMATCH, line, name);
                return fbval_int(0);
            }
            return math_funcs[i].func(args, argc, line, interp);
        }
    }
    fb_error(FB_ERR_UNDEFINED_FUNCTION, line, name);
    return fbval_int(0);
}
