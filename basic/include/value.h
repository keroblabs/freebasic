#ifndef VALUE_H
#define VALUE_H

#include <stdint.h>

typedef enum {
    FB_INTEGER,    /* int16_t */
    FB_LONG,       /* int32_t */
    FB_SINGLE,     /* float (32-bit IEEE) */
    FB_DOUBLE,     /* double (64-bit IEEE) */
    FB_STRING,     /* FBString* */
    FB_UDT         /* User-defined type instance */
} FBType;

typedef struct {
    char*    data;       /* Null-terminated for convenience */
    int32_t  len;        /* Actual length */
    int32_t  capacity;   /* Allocated size of data buffer */
    int32_t  refcount;   /* Reference count (starts at 1) */
} FBString;

typedef struct FBValue {
    FBType type;
    union {
        int16_t    ival;     /* FB_INTEGER */
        int32_t    lval;     /* FB_LONG */
        float      sval;     /* FB_SINGLE */
        double     dval;     /* FB_DOUBLE */
        FBString*  str;      /* FB_STRING */
        struct {
            int              type_id;
            struct FBValue*  fields;
        } udt;                   /* FB_UDT */
    } as;
} FBValue;

FBValue fbval_udt(int type_id, FBValue* fields);

#define FB_TRUE  ((int16_t)-1)
#define FB_FALSE ((int16_t)0)

/* FBString functions */
FBString* fbstr_new(const char* text, int32_t len);
FBString* fbstr_empty(void);
void fbstr_ref(FBString* s);
void fbstr_unref(FBString* s);
FBString* fbstr_cow(FBString* s);
FBString* fbstr_concat(const FBString* a, const FBString* b);
FBString* fbstr_mid(const FBString* s, int32_t start, int32_t len);
int fbstr_compare(const FBString* a, const FBString* b);

/* FBValue functions */
FBValue fbval_int(int16_t v);
FBValue fbval_long(int32_t v);
FBValue fbval_single(float v);
FBValue fbval_double(double v);
FBValue fbval_string(FBString* s);
FBValue fbval_string_from_cstr(const char* text);
void fbval_release(FBValue* v);
FBValue fbval_copy(const FBValue* v);
double fbval_to_double(const FBValue* v);
int32_t fbval_to_long(const FBValue* v);
int fbval_is_true(const FBValue* v);
char* fbval_format_print(const FBValue* v);
const char* fbtype_name(FBType t);

#endif
