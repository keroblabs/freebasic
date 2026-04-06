#ifndef ARRAY_H
#define ARRAY_H

#include "value.h"

#define FB_MAX_DIMENSIONS 60

typedef struct {
    int lower;
    int upper;
} ArrayDim;

typedef struct FBArray {
    FBType      element_type;
    int         ndims;
    ArrayDim    dims[FB_MAX_DIMENSIONS];
    FBValue*    data;
    int         total_elements;
    int         is_dynamic;
    int         udt_type_id;    /* -1 if not a UDT array */
} FBArray;

FBArray* fbarray_new(FBType elem_type, int ndims, ArrayDim* dims,
                     int is_dynamic, int udt_type_id);
void     fbarray_free(FBArray* arr);
int      fbarray_index(const FBArray* arr, const int* subscripts, int nsubs);
FBValue* fbarray_get(FBArray* arr, const int* subscripts, int nsubs);
void     fbarray_reinit(FBArray* arr);
int      fbarray_redim(FBArray* arr, int ndims, ArrayDim* dims);
int      fbarray_lbound(const FBArray* arr, int dim);
int      fbarray_ubound(const FBArray* arr, int dim);

#endif
