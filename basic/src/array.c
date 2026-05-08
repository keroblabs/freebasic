/*
 * array.c — FBArray allocation, indexing, resize
 */
#include "array.h"
#include "platform.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>

FBArray* fbarray_new(FBType elem_type, int ndims, ArrayDim* dims,
                     int is_dynamic, int udt_type_id) {
    FBArray* arr = fb_calloc(1, sizeof(FBArray));
    arr->element_type = elem_type;
    arr->ndims = ndims;
    arr->is_dynamic = is_dynamic;
    arr->udt_type_id = udt_type_id;

    arr->total_elements = 1;
    for (int d = 0; d < ndims; d++) {
        arr->dims[d] = dims[d];
        int size = dims[d].upper - dims[d].lower + 1;
        if (size <= 0) {
            fb_free(arr);
            return NULL;
        }
        arr->total_elements *= size;
    }

    arr->data = fb_calloc(arr->total_elements, sizeof(FBValue));

    for (int i = 0; i < arr->total_elements; i++) {
        switch (elem_type) {
            case FB_INTEGER: arr->data[i] = fbval_int(0); break;
            case FB_LONG:    arr->data[i] = fbval_long(0); break;
            case FB_SINGLE:  arr->data[i] = fbval_single(0.0f); break;
            case FB_DOUBLE:  arr->data[i] = fbval_double(0.0); break;
            case FB_STRING:  arr->data[i] = fbval_string_from_cstr(""); break;
            default:         arr->data[i] = fbval_int(0); break;
        }
    }

    return arr;
}

void fbarray_free(FBArray* arr) {
    if (!arr) return;
    for (int i = 0; i < arr->total_elements; i++) {
        fbval_release(&arr->data[i]);
    }
    fb_free(arr->data);
    fb_free(arr);
}

int fbarray_index(const FBArray* arr, const int* subscripts, int nsubs) {
    if (nsubs != arr->ndims) return -1;

    int flat = 0;
    int multiplier = 1;
    for (int d = arr->ndims - 1; d >= 0; d--) {
        int idx = subscripts[d] - arr->dims[d].lower;
        int dim_size = arr->dims[d].upper - arr->dims[d].lower + 1;
        if (idx < 0 || idx >= dim_size) return -1;
        flat += idx * multiplier;
        multiplier *= dim_size;
    }
    return flat;
}

FBValue* fbarray_get(FBArray* arr, const int* subscripts, int nsubs) {
    int idx = fbarray_index(arr, subscripts, nsubs);
    if (idx < 0) return NULL;
    return &arr->data[idx];
}

void fbarray_reinit(FBArray* arr) {
    if (!arr) return;
    for (int i = 0; i < arr->total_elements; i++) {
        fbval_release(&arr->data[i]);
        switch (arr->element_type) {
            case FB_INTEGER: arr->data[i] = fbval_int(0); break;
            case FB_LONG:    arr->data[i] = fbval_long(0); break;
            case FB_SINGLE:  arr->data[i] = fbval_single(0.0f); break;
            case FB_DOUBLE:  arr->data[i] = fbval_double(0.0); break;
            case FB_STRING:  arr->data[i] = fbval_string_from_cstr(""); break;
            default:         arr->data[i] = fbval_int(0); break;
        }
    }
}

int fbarray_redim(FBArray* arr, int ndims, ArrayDim* dims) {
    /* Free old data */
    for (int i = 0; i < arr->total_elements; i++) {
        fbval_release(&arr->data[i]);
    }
    fb_free(arr->data);

    arr->ndims = ndims;
    arr->total_elements = 1;
    for (int d = 0; d < ndims; d++) {
        arr->dims[d] = dims[d];
        int size = dims[d].upper - dims[d].lower + 1;
        if (size <= 0) {
            arr->data = NULL;
            arr->total_elements = 0;
            return -1;
        }
        arr->total_elements *= size;
    }

    arr->data = fb_calloc(arr->total_elements, sizeof(FBValue));
    for (int i = 0; i < arr->total_elements; i++) {
        switch (arr->element_type) {
            case FB_INTEGER: arr->data[i] = fbval_int(0); break;
            case FB_LONG:    arr->data[i] = fbval_long(0); break;
            case FB_SINGLE:  arr->data[i] = fbval_single(0.0f); break;
            case FB_DOUBLE:  arr->data[i] = fbval_double(0.0); break;
            case FB_STRING:  arr->data[i] = fbval_string_from_cstr(""); break;
            default:         arr->data[i] = fbval_int(0); break;
        }
    }
    return 0;
}

int fbarray_lbound(const FBArray* arr, int dim) {
    if (dim < 1 || dim > arr->ndims) return 0;
    return arr->dims[dim - 1].lower;
}

int fbarray_ubound(const FBArray* arr, int dim) {
    if (dim < 1 || dim > arr->ndims) return 0;
    return arr->dims[dim - 1].upper;
}
