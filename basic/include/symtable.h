#ifndef SYMTABLE_H
#define SYMTABLE_H

#include "value.h"

/* Forward declare FBArray to avoid circular include */
struct FBArray;

typedef enum {
    SYM_VARIABLE,
    SYM_CONST,
    SYM_ARRAY,
    SYM_SUB,
    SYM_FUNCTION,
    SYM_TYPE_DEF,
    SYM_LABEL
} SymKind;

typedef struct Symbol {
    char      name[42];
    SymKind   kind;
    FBType    type;
    FBValue   value;
    struct FBArray* array;        /* For SYM_ARRAY */
    int       udt_type_id;        /* For UDT variables: type registry index (-1 if none) */
    int       is_shared;
    int       is_static;
    int       is_ref;             /* 1 if this is a by-reference alias */
    FBValue*  ref_addr;           /* Pointer to caller's storage for by-ref */
    int       target_line;    /* For SYM_LABEL: index into statement array */
    struct Symbol* next;      /* Hash chain */
} Symbol;

#define SYMTAB_BUCKETS 256

typedef struct Scope {
    Symbol*        buckets[SYMTAB_BUCKETS];
    struct Scope*  parent;
    FBType         deftype[26]; /* Default type for A-Z, init to FB_SINGLE */
} Scope;

Scope*  scope_new(Scope* parent);
void    scope_free(Scope* scope);
void    scope_clear(Scope* scope);   /* Reset all variables to zero/empty */
Symbol* scope_lookup(Scope* scope, const char* name);
Symbol* scope_lookup_local(Scope* scope, const char* name);
Symbol* scope_insert(Scope* scope, const char* name, SymKind kind, FBType type);
FBType  scope_default_type(Scope* scope, const char* name);

#endif
