# Phase 3 — Arrays & User-Defined Types: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **array support and user-defined types (UDTs)** for the FreeBASIC interpreter. Phase 3 enables multi-dimensional arrays, dynamic resizing, record types, and binary packing — unlocking sorting algorithms, address-book programs, and record-based data structures.

---

## Project File Structure (Phase 3 additions)

Files **added** or **significantly modified** relative to Phase 2 are marked with `[NEW]` or `[MOD]`.

```
fbasic/
├── Makefile                        [MOD] — add new source files
├── include/
│   ├── fb.h                       [MOD]
│   ├── ast.h                      [MOD] — array subscript/UDT field nodes
│   ├── parser.h                   [MOD] — array/UDT parsing
│   ├── interpreter.h              [MOD] — array runtime support
│   ├── array.h                    [NEW] — FBArray data structure + API
│   ├── udt.h                      [NEW] — user-defined type definitions
│   ├── builtins_convert.h         [NEW] — MKI$/CVI etc. pack/unpack functions
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — DIM arrays, TYPE, REDIM, SWAP, etc.
│   ├── interpreter.c              [MOD] — array element access, UDT field access
│   ├── array.c                    [NEW] — array allocation, bounds checking, resize
│   ├── udt.c                      [NEW] — UDT type registry, field access
│   ├── builtins_convert.c         [NEW] — MKI$/CVI etc.
│   └── ...
└── tests/
    ├── test_array.c               [NEW] — unit tests for array operations
    ├── test_udt.c                 [NEW] — unit tests for UDT
    └── verify/
        ├── phase3_arrays.bas      [NEW] — DIM, multi-dim, bounds
        ├── phase3_dynamic.bas     [NEW] — REDIM, ERASE, dynamic arrays
        ├── phase3_udt.bas         [NEW] — TYPE...END TYPE
        ├── phase3_swap.bas        [NEW] — SWAP statement
        ├── phase3_sorting.bas     [NEW] — milestone: bubble sort
        ├── phase3_records.bas     [NEW] — milestone: address book
        └── phase3_expected/       [NEW]
            ├── arrays.txt
            ├── dynamic.txt
            ├── udt.txt
            ├── swap.txt
            ├── sorting.txt
            └── records.txt
```

---

## 1. Array Data Structure (`include/array.h`, `src/array.c`)

### 1.1 FBArray Structure

```c
#ifndef ARRAY_H
#define ARRAY_H

#include "value.h"

#define FB_MAX_DIMENSIONS 60    // FB allows up to 60 dimensions

typedef struct {
    int lower;    // Lower bound (default 0, or OPTION BASE, or explicit)
    int upper;    // Upper bound (inclusive)
} ArrayDim;

typedef struct {
    FBType      element_type;   // Type of each element
    int         ndims;          // Number of dimensions
    ArrayDim    dims[FB_MAX_DIMENSIONS];
    FBValue*    data;           // Flat array of elements (row-major)
    int         total_elements; // Product of all dimension sizes
    int         is_dynamic;     // 0 = static (const bounds), 1 = dynamic (REDIM-able)
    int         udt_type_id;    // If element_type is a UDT, this is the type index (-1 otherwise)
} FBArray;

// Allocate and initialize array. Elements set to default (0 or "").
FBArray* fbarray_new(FBType elem_type, int ndims, ArrayDim* dims,
                     int is_dynamic, int udt_type_id);

// Free array and all elements.
void fbarray_free(FBArray* arr);

// Compute flat index from subscript list. Returns -1 on out-of-bounds.
int fbarray_index(const FBArray* arr, const int* subscripts, int nsubs);

// Get pointer to element at subscripts (for read/write).
FBValue* fbarray_get(FBArray* arr, const int* subscripts, int nsubs);

// Reinitialize a static array (all elements to default).
void fbarray_reinit(FBArray* arr);

// Resize a dynamic array (REDIM). Old data is lost.
int fbarray_redim(FBArray* arr, int ndims, ArrayDim* dims);

// Get LBOUND for a dimension (1-based dimension index).
int fbarray_lbound(const FBArray* arr, int dim);

// Get UBOUND for a dimension (1-based dimension index).
int fbarray_ubound(const FBArray* arr, int dim);

#endif
```

### 1.2 Array Index Calculation

```c
int fbarray_index(const FBArray* arr, const int* subscripts, int nsubs) {
    if (nsubs != arr->ndims) return -1;

    // Row-major order: rightmost subscript varies fastest (FB convention)
    int flat = 0;
    int multiplier = 1;
    for (int d = arr->ndims - 1; d >= 0; d--) {
        int idx = subscripts[d] - arr->dims[d].lower;
        int dim_size = arr->dims[d].upper - arr->dims[d].lower + 1;

        if (idx < 0 || idx >= dim_size) return -1; // Subscript out of range

        flat += idx * multiplier;
        multiplier *= dim_size;
    }
    return flat;
}

FBValue* fbarray_get(FBArray* arr, const int* subscripts, int nsubs) {
    int idx = fbarray_index(arr, subscripts, nsubs);
    if (idx < 0) return NULL; // Caller should raise error 9
    return &arr->data[idx];
}
```

### 1.3 Array Allocation

```c
FBArray* fbarray_new(FBType elem_type, int ndims, ArrayDim* dims,
                     int is_dynamic, int udt_type_id) {
    FBArray* arr = calloc(1, sizeof(FBArray));
    arr->element_type = elem_type;
    arr->ndims = ndims;
    arr->is_dynamic = is_dynamic;
    arr->udt_type_id = udt_type_id;

    arr->total_elements = 1;
    for (int d = 0; d < ndims; d++) {
        arr->dims[d] = dims[d];
        int size = dims[d].upper - dims[d].lower + 1;
        if (size <= 0) {
            fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, 0, "Invalid array bounds");
            free(arr);
            return NULL;
        }
        arr->total_elements *= size;
    }

    arr->data = calloc(arr->total_elements, sizeof(FBValue));

    // Initialize elements to default values
    for (int i = 0; i < arr->total_elements; i++) {
        switch (elem_type) {
            case FB_INTEGER: arr->data[i] = fbval_int(0); break;
            case FB_LONG:    arr->data[i] = fbval_long(0); break;
            case FB_SINGLE:  arr->data[i] = fbval_single(0.0f); break;
            case FB_DOUBLE:  arr->data[i] = fbval_double(0.0); break;
            case FB_STRING:  arr->data[i] = fbval_string_from_cstr(""); break;
        }
    }

    return arr;
}
```

---

## 2. Symbol Table Changes for Arrays

### 2.1 Extended Symbol Entry

```c
typedef struct {
    char      name[42];
    SymKind   kind;          // SYM_ARRAY for arrays
    FBType    type;          // Element type
    FBValue   value;         // For SYM_VARIABLE / SYM_CONST
    FBArray*  array;         // For SYM_ARRAY — pointer to array struct
    int       is_shared;
    int       is_static;
    int       target_line;   // For SYM_LABEL
    int       udt_type_id;   // For UDT variables — type registry index
} Symbol;
```

---

## 3. OPTION BASE

```
OPTION BASE {0 | 1}
```

Sets the default lower bound for arrays. Must appear before any DIM.

```c
// In Interpreter struct:
int option_base; // 0 (default) or 1

static void exec_option_base(Interpreter* interp, ASTNode* node) {
    int base = (int)fbval_to_long(&eval_expr(interp, node->data.option_base.value));
    if (base != 0 && base != 1) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, node->line, "OPTION BASE must be 0 or 1");
        return;
    }
    interp->option_base = base;
}
```

---

## 4. DIM Arrays

### 4.1 Syntax

```
DIM [SHARED] name(bounds) [AS type] [, name(bounds) [AS type]] ...
```

Where bounds can be:
- `upper` → lower = OPTION BASE, upper = value
- `lower TO upper` → explicit bounds per dimension
- Multiple dimensions: `DIM a(5, 10)` or `DIM a(1 TO 5, 1 TO 10)`

### 4.2 Parse DIM with Array Bounds

```c
static void parse_dim(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume DIM

    int is_shared = 0;
    if (current_token(p)->kind == TOK_KW_SHARED) {
        is_shared = 1;
        advance(p);
    }

    do {
        Token* name_tok = current_token(p);
        char name[42];
        strncpy(name, name_tok->value.str.text, 41);
        advance(p);

        // Check for array subscripts: name(...)
        if (current_token(p)->kind == TOK_LPAREN) {
            advance(p); // consume '('
            parse_array_dim(p, line, name, name_tok, is_shared);
        } else {
            // Scalar DIM (Phase 1 behavior)
            parse_scalar_dim(p, line, name, name_tok, is_shared);
        }
    } while (current_token(p)->kind == TOK_COMMA && (advance(p), 1));
}

static void parse_array_dim(Parser* p, int line, const char* name,
                            Token* name_tok, int is_shared) {
    // Parse dimension list
    ASTNode* bounds[FB_MAX_DIMENSIONS * 2]; // low, high pairs
    int ndims = 0;

    do {
        ASTNode* first = parse_expr(p, 1);
        ASTNode* low = NULL;
        ASTNode* high = NULL;

        if (current_token(p)->kind == TOK_KW_TO) {
            advance(p);
            low = first;
            high = parse_expr(p, 1);
        } else {
            // Single value → upper bound; lower = OPTION BASE
            low = NULL; // sentinel: use option_base at runtime
            high = first;
        }

        bounds[ndims * 2] = low;
        bounds[ndims * 2 + 1] = high;
        ndims++;

        if (current_token(p)->kind == TOK_COMMA &&
            peek_token(p, 1)->kind != TOK_RPAREN) {
            advance(p);
        } else {
            break;
        }
    } while (ndims < FB_MAX_DIMENSIONS);

    expect(p, TOK_RPAREN);

    // AS type clause
    FBType elem_type = ident_type_from_token(name_tok);
    if (current_token(p)->kind == TOK_KW_AS) {
        advance(p);
        elem_type = parse_type_name(p);
    }

    // Determine static vs dynamic:
    // Static if ALL bounds are constant literals; dynamic otherwise
    int is_dynamic = 0;
    for (int i = 0; i < ndims * 2; i++) {
        if (bounds[i] && bounds[i]->kind != AST_LITERAL) {
            is_dynamic = 1;
            break;
        }
    }

    ASTNode* node = ast_dim_array(line, name, elem_type, ndims,
                                   bounds, is_shared, is_dynamic);
    program_add_stmt(p->prog, node);
}
```

### 4.3 Execute DIM Array

```c
static void exec_dim_array(Interpreter* interp, ASTNode* node) {
    int ndims = node->data.dim_array.ndims;
    ArrayDim dims[FB_MAX_DIMENSIONS];

    for (int d = 0; d < ndims; d++) {
        if (node->data.dim_array.low_bounds[d]) {
            dims[d].lower = (int)fbval_to_long(
                &eval_expr(interp, node->data.dim_array.low_bounds[d]));
        } else {
            dims[d].lower = interp->option_base;
        }
        dims[d].upper = (int)fbval_to_long(
            &eval_expr(interp, node->data.dim_array.high_bounds[d]));
    }

    FBArray* arr = fbarray_new(node->data.dim_array.elem_type, ndims, dims,
                               node->data.dim_array.is_dynamic, -1);

    Scope* target = node->data.dim_array.is_shared
                    ? interp->global_scope
                    : interp->current_scope;

    Symbol* sym = scope_insert(target, node->data.dim_array.name,
                               SYM_ARRAY, node->data.dim_array.elem_type);
    sym->array = arr;
    sym->is_shared = node->data.dim_array.is_shared;
}
```

### 4.4 Auto-dimensioning

FB automatically dimensions arrays on first use if not explicitly DIM'd: default 0 TO 10 (or `option_base` TO 10).

```c
static FBArray* auto_dim_array(Interpreter* interp, const char* name,
                                FBType elem_type, int ndims) {
    ArrayDim dims[FB_MAX_DIMENSIONS];
    for (int d = 0; d < ndims; d++) {
        dims[d].lower = interp->option_base;
        dims[d].upper = 10;
    }
    FBArray* arr = fbarray_new(elem_type, ndims, dims, 0, -1);

    Symbol* sym = scope_insert(interp->current_scope, name,
                               SYM_ARRAY, elem_type);
    sym->array = arr;
    return arr;
}
```

---

## 5. Array Element Access in Expressions

### 5.1 AST Node for Array Element

```c
// AST_ARRAY_ACCESS
struct {
    char           name[42];
    FBType         type_hint;
    struct ASTNode** subscripts;   // Array of subscript expressions
    int            subscript_count;
} array_access;
```

### 5.2 Parse Array Access

In the expression parser, when an identifier is followed by `(`, it could be a function call or array access. Disambiguate by looking up the name:

```c
static ASTNode* parse_primary(Parser* p) {
    Token* t = current_token(p);
    if (is_identifier(t->kind) && peek_token(p, 1)->kind == TOK_LPAREN) {
        // Check if it's a known built-in function
        if (is_builtin_function(t->value.str.text)) {
            return parse_func_call(p);
        }
        // Otherwise — could be array access or user function (Phase 4)
        // For Phase 3, treat as array access
        return parse_array_access(p);
    }
    // ... rest of parse_primary
}

static ASTNode* parse_array_access(Parser* p) {
    Token* t = current_token(p);
    char name[42];
    strncpy(name, t->value.str.text, 41);
    FBType type_hint = ident_type_from_token(t);
    int line = t->line;
    advance(p); // consume name

    expect(p, TOK_LPAREN);
    ASTNode* subs[FB_MAX_DIMENSIONS];
    int nsubs = 0;
    subs[nsubs++] = parse_expr(p, 1);
    while (current_token(p)->kind == TOK_COMMA) {
        advance(p);
        subs[nsubs++] = parse_expr(p, 1);
    }
    expect(p, TOK_RPAREN);

    return ast_array_access(line, name, type_hint,
                            copy_node_array(subs, nsubs), nsubs);
}
```

### 5.3 Evaluate Array Access

```c
static FBValue eval_array_access(Interpreter* interp, ASTNode* expr) {
    Symbol* sym = scope_lookup(interp->current_scope,
                                expr->data.array_access.name);

    int subs[FB_MAX_DIMENSIONS];
    int nsubs = expr->data.array_access.subscript_count;
    for (int i = 0; i < nsubs; i++) {
        FBValue v = eval_expr(interp, expr->data.array_access.subscripts[i]);
        subs[i] = (int)fbval_to_long(&v);
        fbval_release(&v);
    }

    // Auto-dim if not yet declared
    if (!sym) {
        FBType type = scope_default_type(interp->current_scope,
                                          expr->data.array_access.name);
        auto_dim_array(interp, expr->data.array_access.name, type, nsubs);
        sym = scope_lookup(interp->current_scope,
                           expr->data.array_access.name);
    }

    if (sym->kind != SYM_ARRAY) {
        fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, expr->line,
                 "Variable is not an array");
        return fbval_int(0);
    }

    FBValue* elem = fbarray_get(sym->array, subs, nsubs);
    if (!elem) {
        fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, expr->line, NULL);
        return fbval_int(0);
    }

    return fbval_copy(elem);
}
```

---

## 6. REDIM

### 6.1 Syntax

```
REDIM [SHARED] name(bounds) [AS type]
```

Resizes dynamic arrays. All data is lost (reset to default). Cannot REDIM a static array.

```c
static void exec_redim(Interpreter* interp, ASTNode* node) {
    Symbol* sym = scope_lookup(interp->current_scope,
                                node->data.dim_array.name);
    if (sym && sym->kind == SYM_ARRAY) {
        if (!sym->array->is_dynamic) {
            fb_error(FB_ERR_DUPLICATE_DEFINITION, node->line,
                     "Cannot REDIM static array");
            return;
        }
        // Resize existing dynamic array
        int ndims = node->data.dim_array.ndims;
        ArrayDim dims[FB_MAX_DIMENSIONS];
        for (int d = 0; d < ndims; d++) {
            if (node->data.dim_array.low_bounds[d]) {
                dims[d].lower = (int)fbval_to_long(
                    &eval_expr(interp, node->data.dim_array.low_bounds[d]));
            } else {
                dims[d].lower = interp->option_base;
            }
            dims[d].upper = (int)fbval_to_long(
                &eval_expr(interp, node->data.dim_array.high_bounds[d]));
        }
        fbarray_redim(sym->array, ndims, dims);
    } else {
        // First time — create as dynamic
        exec_dim_array(interp, node);
        Symbol* new_sym = scope_lookup(interp->current_scope,
                                        node->data.dim_array.name);
        if (new_sym && new_sym->array) {
            new_sym->array->is_dynamic = 1;
        }
    }
}
```

---

## 7. ERASE

### 7.1 Syntax

```
ERASE arrayname [, arrayname] ...
```

- Static arrays: reinitialize all elements to default values
- Dynamic arrays: deallocate entirely (must REDIM before using again)

```c
static void exec_erase(Interpreter* interp, ASTNode* node) {
    for (int i = 0; i < node->data.erase.name_count; i++) {
        Symbol* sym = scope_lookup(interp->current_scope,
                                    node->data.erase.names[i]);
        if (!sym || sym->kind != SYM_ARRAY) {
            fb_error(FB_ERR_ILLEGAL_FUNC_CALL, node->line,
                     "ERASE: not an array");
            continue;
        }

        if (sym->array->is_dynamic) {
            // Deallocate entirely
            fbarray_free(sym->array);
            sym->array = NULL;
        } else {
            // Reinitialize elements
            fbarray_reinit(sym->array);
        }
    }
}
```

---

## 8. LBOUND / UBOUND Functions

```c
// LBOUND(array [, dimension])
// UBOUND(array [, dimension])
// dimension is 1-based (default = 1)

if (_stricmp(name, "LBOUND") == 0 || _stricmp(name, "UBOUND") == 0) {
    // args[0] is AST_VARIABLE with the array name
    const char* arr_name = expr->data.func_call.args[0]->data.variable.name;
    Symbol* sym = scope_lookup(interp->current_scope, arr_name);
    if (!sym || sym->kind != SYM_ARRAY || !sym->array) {
        fb_error(FB_ERR_SUBSCRIPT_OUT_OF_RANGE, expr->line, NULL);
        return fbval_int(0);
    }

    int dim = 1;
    if (argc >= 2) dim = (int)fbval_to_long(&argv[1]);

    if (_stricmp(name, "LBOUND") == 0) {
        return fbval_int((int16_t)fbarray_lbound(sym->array, dim));
    } else {
        return fbval_int((int16_t)fbarray_ubound(sym->array, dim));
    }
}
```

---

## 9. SWAP Statement

### 9.1 Syntax

```
SWAP var1, var2
```

Swaps the values of two variables (or array elements) of the same type.

```c
static void exec_swap(Interpreter* interp, ASTNode* node) {
    // Resolve both targets to their storage locations
    FBValue* loc1 = resolve_lvalue(interp, node->data.swap.left);
    FBValue* loc2 = resolve_lvalue(interp, node->data.swap.right);

    if (!loc1 || !loc2) {
        fb_error(FB_ERR_TYPE_MISMATCH, node->line, "SWAP: invalid target");
        return;
    }

    // Check types match
    if (loc1->type != loc2->type) {
        fb_error(FB_ERR_TYPE_MISMATCH, node->line, "SWAP: type mismatch");
        return;
    }

    // Swap values
    FBValue tmp = *loc1;
    *loc1 = *loc2;
    *loc2 = tmp;
    // Note: for strings, this swaps the pointers — no ref-count change needed
}

// resolve_lvalue: return a pointer to the value storage for a variable or array element
static FBValue* resolve_lvalue(Interpreter* interp, ASTNode* node) {
    if (node->kind == AST_VARIABLE) {
        Symbol* sym = resolve_or_create_var(interp, node);
        return &sym->value;
    } else if (node->kind == AST_ARRAY_ACCESS) {
        Symbol* sym = scope_lookup(interp->current_scope,
                                    node->data.array_access.name);
        if (!sym || sym->kind != SYM_ARRAY) return NULL;
        int subs[FB_MAX_DIMENSIONS];
        for (int i = 0; i < node->data.array_access.subscript_count; i++) {
            FBValue v = eval_expr(interp,
                            node->data.array_access.subscripts[i]);
            subs[i] = (int)fbval_to_long(&v);
            fbval_release(&v);
        }
        return fbarray_get(sym->array, subs,
                           node->data.array_access.subscript_count);
    } else if (node->kind == AST_UDT_FIELD) {
        // UDT field access — resolve recursively
        return resolve_udt_lvalue(interp, node);
    }
    return NULL;
}
```

---

## 10. TYPE...END TYPE (User-Defined Types)

### 10.1 UDT Registry (`include/udt.h`)

```c
#ifndef UDT_H
#define UDT_H

#include "value.h"

#define UDT_MAX_FIELDS 256
#define UDT_MAX_TYPES  128

typedef struct {
    char    name[42];       // Field name
    FBType  type;           // Field type
    int     is_fixed_str;   // 1 if STRING * n
    int     fixed_str_len;  // n for STRING * n
    int     udt_type_id;    // Nested UDT type index (-1 if simple)
    int     offset;         // Byte offset within the record (for FIELD/LSET)
} UDTField;

typedef struct {
    char      name[42];           // Type name
    UDTField  fields[UDT_MAX_FIELDS];
    int       field_count;
    int       total_size;         // Total byte size of one record
} UDTDef;

// Global UDT registry
typedef struct {
    UDTDef  types[UDT_MAX_TYPES];
    int     type_count;
} UDTRegistry;

// Initialize registry.
void udt_registry_init(UDTRegistry* reg);

// Register a new type. Returns type index, or -1 on error (duplicate name).
int udt_register(UDTRegistry* reg, const char* name);

// Add a field to the type being defined.
int udt_add_field(UDTRegistry* reg, int type_id, const char* name,
                  FBType type, int fixed_str_len, int nested_udt_id);

// Finalize type definition (calculate offsets and total size).
void udt_finalize(UDTRegistry* reg, int type_id);

// Look up a type by name. Returns type index or -1.
int udt_find(const UDTRegistry* reg, const char* name);

// Look up a field by name within a type. Returns field index or -1.
int udt_find_field(const UDTRegistry* reg, int type_id, const char* name);

// Allocate a FBValue record instance for a given UDT type.
// Returns a FBValue of type FB_UDT (or a block of memory holding field values).
FBValue* udt_alloc_instance(const UDTRegistry* reg, int type_id);

// Free a UDT instance.
void udt_free_instance(FBValue* instance, const UDTRegistry* reg, int type_id);

#endif
```

### 10.2 UDT Storage

Each UDT instance is stored as an array of FBValues (one per field), embedded in a special UDT value wrapper:

```c
// Extend FBType enum:
typedef enum {
    FB_INTEGER, FB_LONG, FB_SINGLE, FB_DOUBLE, FB_STRING,
    FB_UDT      // User-defined type instance
} FBType;

// Extend FBValue union:
typedef struct {
    FBType type;
    union {
        int16_t    ival;
        int32_t    lval;
        float      sval;
        double     dval;
        FBString*  str;
        struct {
            int       type_id;    // UDT type index
            FBValue*  fields;     // Array of field values
        } udt;
    } as;
} FBValue;
```

### 10.3 Parse TYPE...END TYPE

```c
static void parse_type_def(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume TYPE

    char type_name[42];
    strncpy(type_name, current_token(p)->value.str.text, 41);
    advance(p);
    expect_eol(p);

    int type_id = udt_register(&p->prog->udt_registry, type_name);

    // Parse fields until END TYPE
    while (current_token(p)->kind != TOK_KW_END) {
        if (current_token(p)->kind == TOK_EOL) { advance(p); continue; }

        char field_name[42];
        strncpy(field_name, current_token(p)->value.str.text, 41);
        advance(p);

        expect(p, TOK_KW_AS);

        // Type can be: INTEGER, LONG, SINGLE, DOUBLE, STRING * n, or another TYPE name
        FBType field_type;
        int fixed_str_len = 0;
        int nested_udt_id = -1;

        if (current_token(p)->kind == TOK_KW_STRING) {
            advance(p);
            if (current_token(p)->kind == TOK_STAR) {
                advance(p);
                fixed_str_len = (int)current_token(p)->value.int_val;
                advance(p);
                field_type = FB_STRING;
            } else {
                field_type = FB_STRING;
            }
        } else if (is_type_keyword(current_token(p)->kind)) {
            field_type = parse_type_name(p);
        } else {
            // Could be another UDT name
            nested_udt_id = udt_find(&p->prog->udt_registry,
                                      current_token(p)->value.str.text);
            field_type = FB_UDT;
            advance(p);
        }

        udt_add_field(&p->prog->udt_registry, type_id, field_name,
                       field_type, fixed_str_len, nested_udt_id);
        expect_eol(p);
    }

    expect(p, TOK_KW_END);
    expect(p, TOK_KW_TYPE);
    udt_finalize(&p->prog->udt_registry, type_id);
}
```

### 10.4 UDT Field Access (Dot Notation)

```c
// AST_UDT_FIELD
struct {
    struct ASTNode* base;      // Variable or nested field access
    char            field[42]; // Field name
} udt_field;

// In expression parser: after parsing a variable, check for '.'
static ASTNode* parse_postfix(Parser* p, ASTNode* base) {
    while (current_token(p)->kind == TOK_DOT) {
        advance(p); // consume '.'
        char field_name[42];
        strncpy(field_name, current_token(p)->value.str.text, 41);
        advance(p);
        base = ast_udt_field(base->line, base, field_name);
    }
    return base;
}
```

### 10.5 Evaluate UDT Field

```c
static FBValue eval_udt_field(Interpreter* interp, ASTNode* expr) {
    // First resolve the base (could be a variable or nested field)
    FBValue* base_ptr = resolve_lvalue(interp, expr->data.udt_field.base);
    if (!base_ptr || base_ptr->type != FB_UDT) {
        fb_error(FB_ERR_TYPE_MISMATCH, expr->line, "Not a TYPE variable");
        return fbval_int(0);
    }

    int type_id = base_ptr->as.udt.type_id;
    int field_idx = udt_find_field(&interp->prog->udt_registry,
                                    type_id,
                                    expr->data.udt_field.field);
    if (field_idx < 0) {
        fb_error(FB_ERR_SYNTAX, expr->line, "Unknown field");
        return fbval_int(0);
    }

    return fbval_copy(&base_ptr->as.udt.fields[field_idx]);
}
```

---

## 11. FIELD / LSET / RSET

### 11.1 FIELD Statement

```
FIELD #filenum, width AS stringvar [, width AS stringvar] ...
```

Maps portions of a random-access file buffer to string variables. Implementation deferred partly to Phase 5 (File I/O), but the binding mechanism is built here.

```c
// AST_FIELD
struct {
    struct ASTNode* filenum;
    struct {
        struct ASTNode* width;
        struct ASTNode* var;
    }* fields;
    int field_count;
} field_stmt;

static void exec_field(Interpreter* interp, ASTNode* node) {
    int fnum = (int)fbval_to_long(&eval_expr(interp, node->data.field_stmt.filenum));
    // Associate string variables with buffer offsets for the file
    int offset = 0;
    for (int i = 0; i < node->data.field_stmt.field_count; i++) {
        int width = (int)fbval_to_long(
            &eval_expr(interp, node->data.field_stmt.fields[i].width));
        const char* var_name =
            node->data.field_stmt.fields[i].var->data.variable.name;
        // Store mapping: file fnum, offset, width → variable name
        register_field_mapping(interp, fnum, offset, width, var_name);
        offset += width;
    }
}
```

### 11.2 LSET / RSET

```
LSET stringvar = expr
RSET stringvar = expr
```

Left-justify or right-justify a value into a fixed-length string field.

```c
static void exec_lset(Interpreter* interp, ASTNode* node) {
    Symbol* sym = scope_lookup(interp->current_scope,
                                node->data.let.target->data.variable.name);
    FBValue rhs = eval_expr(interp, node->data.let.expr);

    // LSET: copy into existing string length, left-justified, space-padded
    int target_len = sym->value.as.str->len;
    sym->value.as.str = fbstr_cow(sym->value.as.str);
    memset(sym->value.as.str->data, ' ', target_len);
    int copy_len = rhs.as.str->len;
    if (copy_len > target_len) copy_len = target_len;
    memcpy(sym->value.as.str->data, rhs.as.str->data, copy_len);

    fbval_release(&rhs);
}

// RSET is the same but right-justified:
static void exec_rset(Interpreter* interp, ASTNode* node) {
    Symbol* sym = scope_lookup(interp->current_scope,
                                node->data.let.target->data.variable.name);
    FBValue rhs = eval_expr(interp, node->data.let.expr);

    int target_len = sym->value.as.str->len;
    sym->value.as.str = fbstr_cow(sym->value.as.str);
    memset(sym->value.as.str->data, ' ', target_len);
    int copy_len = rhs.as.str->len;
    if (copy_len > target_len) copy_len = target_len;
    int offset = target_len - copy_len;
    memcpy(sym->value.as.str->data + offset, rhs.as.str->data, copy_len);

    fbval_release(&rhs);
}
```

---

## 12. MKI$/CVI and Related Pack/Unpack Functions (`src/builtins_convert.c`)

### 12.1 Function Table

```c
// Pack numeric values into string bytes:
// MKI$(integer%) → 2-byte string
// MKL$(long&)    → 4-byte string
// MKS$(single!)  → 4-byte string
// MKD$(double#)  → 8-byte string

// Unpack string bytes to numeric values:
// CVI(string2$)  → integer%
// CVL(string4$)  → long&
// CVS(string4$)  → single!
// CVD(string8$)  → double#

// Microsoft Binary Format (MBF) conversions:
// MKSMBF$(single!) → 4-byte MBF string
// MKDMBF$(double#) → 8-byte MBF string
// CVSMBF(string4$) → single!
// CVDMBF(string8$) → double#
```

### 12.2 Implementations

```c
static FBValue builtin_mki(FBValue* args, int argc, int line) {
    int16_t v = (int16_t)fbval_to_long(&args[0]);
    FBString* s = fbstr_new((char*)&v, 2);
    return fbval_string(s);
}

static FBValue builtin_mkl(FBValue* args, int argc, int line) {
    int32_t v = fbval_to_long(&args[0]);
    FBString* s = fbstr_new((char*)&v, 4);
    return fbval_string(s);
}

static FBValue builtin_mks(FBValue* args, int argc, int line) {
    float v = (float)fbval_to_double(&args[0]);
    FBString* s = fbstr_new((char*)&v, 4);
    return fbval_string(s);
}

static FBValue builtin_mkd(FBValue* args, int argc, int line) {
    double v = fbval_to_double(&args[0]);
    FBString* s = fbstr_new((char*)&v, 8);
    return fbval_string(s);
}

static FBValue builtin_cvi(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING || args[0].as.str->len < 2) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVI requires 2-byte string");
        return fbval_int(0);
    }
    int16_t v;
    memcpy(&v, args[0].as.str->data, 2);
    return fbval_int(v);
}

static FBValue builtin_cvl(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING || args[0].as.str->len < 4) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVL requires 4-byte string");
        return fbval_int(0);
    }
    int32_t v;
    memcpy(&v, args[0].as.str->data, 4);
    return fbval_long(v);
}

static FBValue builtin_cvs(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING || args[0].as.str->len < 4) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVS requires 4-byte string");
        return fbval_single(0);
    }
    float v;
    memcpy(&v, args[0].as.str->data, 4);
    return fbval_single(v);
}

static FBValue builtin_cvd(FBValue* args, int argc, int line) {
    if (args[0].type != FB_STRING || args[0].as.str->len < 8) {
        fb_error(FB_ERR_ILLEGAL_FUNC_CALL, line, "CVD requires 8-byte string");
        return fbval_double(0);
    }
    double v;
    memcpy(&v, args[0].as.str->data, 8);
    return fbval_double(v);
}
```

### 12.3 MBF Conversions

```c
// Microsoft Binary Format (MBF) uses a different exponent encoding than IEEE 754.
// MBF single: 1 byte exponent (biased by 128), 1 bit sign, 23 bit mantissa
// MBF double: 1 byte exponent (biased by 128), 1 bit sign, 55 bit mantissa

static float mbf_to_ieee_single(const uint8_t* mbf) {
    if (mbf[3] == 0) return 0.0f;
    uint8_t ieee[4];
    uint8_t sign = mbf[2] & 0x80;
    uint8_t exponent = mbf[3] - 2; // MBF bias 128 → IEEE bias 127
    ieee[3] = sign | (exponent >> 1);
    ieee[2] = (exponent << 7) | (mbf[2] & 0x7F);
    ieee[1] = mbf[1];
    ieee[0] = mbf[0];
    float result;
    memcpy(&result, ieee, 4);
    return result;
}

// Similar for double precision (8 bytes)
```

---

## 13. Verification Test Files

### 13.1 `tests/verify/phase3_arrays.bas` — Array Operations

```basic
REM Phase 3 Test: Arrays
DEFINT A-Z
DIM a(5)
FOR i = 0 TO 5
    a(i) = i * 10
NEXT i
FOR i = 0 TO 5
    PRINT a(i);
NEXT i
PRINT

' Multi-dimensional
DIM b(2, 3)
FOR i = 0 TO 2
    FOR j = 0 TO 3
        b(i, j) = i * 10 + j
    NEXT j
NEXT i
PRINT b(1, 2)

' Explicit bounds
DIM c(1 TO 5)
FOR i = 1 TO 5
    c(i) = i
NEXT i
PRINT c(3)

' LBOUND / UBOUND
PRINT LBOUND(c); UBOUND(c)
```

**Expected output (`tests/verify/phase3_expected/arrays.txt`):**
```
 0  10  20  30  40  50
 12
 3
 1  5
```

### 13.2 `tests/verify/phase3_dynamic.bas` — Dynamic Arrays

```basic
REM Phase 3 Test: Dynamic arrays
DEFINT A-Z
DIM n AS INTEGER
n = 5
REDIM a(1 TO n)
FOR i = 1 TO n
    a(i) = i * 2
NEXT i
FOR i = 1 TO n
    PRINT a(i);
NEXT i
PRINT

' Resize
n = 3
REDIM a(1 TO n)
FOR i = 1 TO n
    a(i) = i * 100
NEXT i
FOR i = 1 TO n
    PRINT a(i);
NEXT i
PRINT

' ERASE
ERASE a
PRINT "Erased."
```

**Expected output (`tests/verify/phase3_expected/dynamic.txt`):**
```
 2  4  6  8  10
 100  200  300
Erased.
```

### 13.3 `tests/verify/phase3_udt.bas` — User-Defined Types

```basic
REM Phase 3 Test: TYPE...END TYPE
TYPE PersonType
    firstName AS STRING * 15
    lastName AS STRING * 15
    age AS INTEGER
END TYPE

DIM p AS PersonType
p.firstName = "John"
p.lastName = "Doe"
p.age = 30

PRINT p.firstName; " "; p.lastName; ", age"; p.age

' Array of TYPE
DIM people(1 TO 3) AS PersonType
people(1).firstName = "Alice"
people(1).age = 25
people(2).firstName = "Bob"
people(2).age = 30
people(3).firstName = "Charlie"
people(3).age = 35

FOR i% = 1 TO 3
    PRINT people(i%).firstName; ":"; people(i%).age
NEXT i%
```

**Expected output (`tests/verify/phase3_expected/udt.txt`):**
```
John Doe, age 30
Alice: 25
Bob: 30
Charlie: 35
```

### 13.4 `tests/verify/phase3_sorting.bas` — Milestone: Bubble Sort

```basic
REM Bubble Sort - Phase 3 Milestone
DEFINT A-Z
DIM a(1 TO 10)
' Initialize with unsorted data
DATA 64, 34, 25, 12, 22, 11, 90, 1, 45, 78
FOR i = 1 TO 10
    READ a(i)
NEXT i

' Bubble sort
FOR i = 1 TO 9
    FOR j = 1 TO 10 - i
        IF a(j) > a(j + 1) THEN
            SWAP a(j), a(j + 1)
        END IF
    NEXT j
NEXT i

' Print sorted
PRINT "Sorted:";
FOR i = 1 TO 10
    PRINT a(i);
NEXT i
PRINT
```

**Expected output (`tests/verify/phase3_expected/sorting.txt`):**
```
Sorted: 1  11  12  22  25  34  45  64  78  90
```

---

## 14. Updated Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -g -fsanitize=address
LDFLAGS = -fsanitize=address -lm

SRC = src/main.c src/lexer.c src/value.c src/symtable.c src/ast.c \
      src/parser.c src/interpreter.c src/coerce.c src/error.c \
      src/console.c src/builtins_str.c src/builtins_math.c \
      src/print_using.c src/array.c src/udt.c src/builtins_convert.c
OBJ = $(SRC:.c=.o)

fbasic: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

test-phase3: fbasic
	@echo "Running Phase 3 tests..."
	@for f in arrays dynamic udt swap sorting records; do \
		./fbasic tests/verify/phase3_$$f.bas > /tmp/p3_$$f.txt && \
		diff /tmp/p3_$$f.txt tests/verify/phase3_expected/$$f.txt && \
		echo "  PASS: $$f" || echo "  FAIL: $$f"; \
	done

test: test-phase0 test-phase1 test-phase2 test-phase3
	@echo "All tests passed."

clean:
	rm -f $(OBJ) fbasic

.PHONY: test test-phase0 test-phase1 test-phase2 test-phase3 clean
```

---

## 15. Phase 3 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **DIM arrays** | Single and multi-dimensional arrays allocate correctly. Bounds checking works — subscript out of range → error 9. `lower TO upper` explicit bounds work. Default lower bound follows `OPTION BASE`. |
| 2 | **Auto-dimensioning** | Using undeclared `a(5)` auto-creates array with bounds `0 TO 10` (or `1 TO 10` with OPTION BASE 1). |
| 3 | **REDIM** | Resizes dynamic arrays. Old data discarded. Static array REDIM → error. |
| 4 | **ERASE** | Static arrays reinitialize to defaults. Dynamic arrays deallocate. |
| 5 | **LBOUND / UBOUND** | Correct bounds returned for each dimension (1-based dimension index). |
| 6 | **SWAP** | Swaps scalars, array elements, and UDT fields. Type mismatch → error. |
| 7 | **Static vs Dynamic** | `DIM a(10)` is static (constant bounds). `DIM a(N)` or `REDIM a(N)` is dynamic. REDIM on static → error. |
| 8 | **TYPE...END TYPE** | UDT definitions registered. Field types include INTEGER, LONG, SINGLE, DOUBLE, STRING * n, and nested UDTs. |
| 9 | **UDT instances** | DIM var AS TypeName allocates instance. Dot-notation field access for read and write. Arrays of UDTs work. |
| 10 | **FIELD / LSET / RSET** | FIELD maps buffer offsets. LSET left-justifies into fixed-length string. RSET right-justifies. |
| 11 | **MKI$/CVI etc.** | All 12 pack/unpack functions (MKI$, MKL$, MKS$, MKD$, CVI, CVL, CVS, CVD, MKSMBF$, MKDMBF$, CVSMBF, CVDMBF) correctly convert between numeric values and string byte representations. |
| 12 | **OPTION BASE** | `OPTION BASE 0` and `OPTION BASE 1` affect default lower bound. Must appear before any DIM. |
| 13 | **Milestone Programs** | Bubble sort and address-book program produce correct output. |
| 14 | **No Leaks** | Array allocation/deallocation, UDT instance lifecycle, and string fields all pass ASan with zero leaks. |

---

## 16. Key Implementation Warnings

1. **Array identity vs function call:** When the parser sees `name(expr)`, it must disambiguate array access from function call. Priority: (1) built-in function → function call, (2) declared SYM_ARRAY → array access, (3) declared SYM_FUNCTION (Phase 4) → function call, (4) unknown → array access (auto-dim). This heuristic breaks if a user names a variable the same as a function — FB handles this by context.

2. **Row-major order:** FB stores arrays in "row-major" order (same as C). `DIM a(2, 3)` stores `a(0,0), a(0,1), a(0,2), a(0,3), a(1,0), ...`. The rightmost subscript varies fastest.

3. **OPTION BASE must appear first:** FB requires OPTION BASE before any array DIM. If a DIM has already occurred, OPTION BASE is a compile error. Track whether any arrays have been DIMmed.

4. **STRING * n in TYPE:** Fixed-length strings in TYPEs are space-padded, NOT null-terminated. A `STRING * 10` field always occupies exactly 10 bytes. Initialize to 10 spaces. LSET/RSET respect this fixed length.

5. **UDT assignment:** Assigning one UDT variable to another (`p1 = p2`) copies ALL fields, including fixed-length strings and nested UDTs. This is a deep copy, not pointer assignment. For strings, this means creating new FBString copies.

6. **MKI$/CVI endianness:** FB uses little-endian byte order (x86). On little-endian platforms (Windows, Linux x86), memcpy works directly. On big-endian platforms, byte-swap would be needed. Since we target x86, direct memcpy is correct.

7. **Auto-dim vs explicit DIM interaction:** If `a(5)` is used before `DIM a(20)`, FB auto-dims a(0 TO 10). A subsequent `DIM a(20)` would then be "Duplicate definition" error. The program must DIM arrays before first use to get non-default bounds.
