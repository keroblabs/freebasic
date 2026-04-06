/*
 * builtins_convert.c — MKI$/CVI, MKL$/CVL, MKS$/CVS, MKD$/CVD pack/unpack
 */
#include "builtins_convert.h"
#include "error.h"
#include <string.h>
#include <strings.h>
#include <stdint.h>

int builtin_convert_lookup(const char* name) {
    if (strcasecmp(name, "MKI$") == 0) return 1;
    if (strcasecmp(name, "MKL$") == 0) return 1;
    if (strcasecmp(name, "MKS$") == 0) return 1;
    if (strcasecmp(name, "MKD$") == 0) return 1;
    if (strcasecmp(name, "CVI") == 0) return 1;
    if (strcasecmp(name, "CVL") == 0) return 1;
    if (strcasecmp(name, "CVS") == 0) return 1;
    if (strcasecmp(name, "CVD") == 0) return 1;
    if (strcasecmp(name, "MKSMBF$") == 0) return 1;
    if (strcasecmp(name, "MKDMBF$") == 0) return 1;
    if (strcasecmp(name, "CVSMBF") == 0) return 1;
    if (strcasecmp(name, "CVDMBF") == 0) return 1;
    return 0;
}

FBValue builtin_convert_call(const char* name, FBValue* args, int argc, int line) {
    (void)argc;

    if (strcasecmp(name, "MKI$") == 0) {
        int16_t v = (int16_t)fbval_to_long(&args[0]);
        FBString* s = fbstr_new((char*)&v, 2);
        return fbval_string(s);
    }
    if (strcasecmp(name, "MKL$") == 0) {
        int32_t v = fbval_to_long(&args[0]);
        FBString* s = fbstr_new((char*)&v, 4);
        return fbval_string(s);
    }
    if (strcasecmp(name, "MKS$") == 0) {
        float v = (float)fbval_to_double(&args[0]);
        FBString* s = fbstr_new((char*)&v, 4);
        return fbval_string(s);
    }
    if (strcasecmp(name, "MKD$") == 0) {
        double v = fbval_to_double(&args[0]);
        FBString* s = fbstr_new((char*)&v, 8);
        return fbval_string(s);
    }
    if (strcasecmp(name, "CVI") == 0) {
        if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len < 2) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVI requires 2-byte string");
            return fbval_int(0);
        }
        int16_t v;
        memcpy(&v, args[0].as.str->data, 2);
        return fbval_int(v);
    }
    if (strcasecmp(name, "CVL") == 0) {
        if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len < 4) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVL requires 4-byte string");
            return fbval_long(0);
        }
        int32_t v;
        memcpy(&v, args[0].as.str->data, 4);
        return fbval_long(v);
    }
    if (strcasecmp(name, "CVS") == 0) {
        if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len < 4) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVS requires 4-byte string");
            return fbval_single(0);
        }
        float v;
        memcpy(&v, args[0].as.str->data, 4);
        return fbval_single(v);
    }
    if (strcasecmp(name, "CVD") == 0) {
        if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len < 8) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVD requires 8-byte string");
            return fbval_double(0);
        }
        double v;
        memcpy(&v, args[0].as.str->data, 8);
        return fbval_double(v);
    }
    /* MBF conversions — simplified (assume IEEE = MBF on modern x86) */
    if (strcasecmp(name, "MKSMBF$") == 0) {
        float v = (float)fbval_to_double(&args[0]);
        FBString* s = fbstr_new((char*)&v, 4);
        return fbval_string(s);
    }
    if (strcasecmp(name, "MKDMBF$") == 0) {
        double v = fbval_to_double(&args[0]);
        FBString* s = fbstr_new((char*)&v, 8);
        return fbval_string(s);
    }
    if (strcasecmp(name, "CVSMBF") == 0) {
        if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len < 4) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVSMBF requires 4-byte string");
            return fbval_single(0);
        }
        float v;
        memcpy(&v, args[0].as.str->data, 4);
        return fbval_single(v);
    }
    if (strcasecmp(name, "CVDMBF") == 0) {
        if (args[0].type != FB_STRING || !args[0].as.str || args[0].as.str->len < 8) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVDMBF requires 8-byte string");
            return fbval_double(0);
        }
        double v;
        memcpy(&v, args[0].as.str->data, 8);
        return fbval_double(v);
    }

    fb_error(FB_ERR_UNDEFINED_FUNCTION, line, name);
    return fbval_int(0);
}
