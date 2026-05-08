/*
 * symtable.c — Symbol table with scope chaining
 */
#include "symtable.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint32_t sym_hash(const char* name) {
    uint32_t h = 2166136261u;
    for (const char* p = name; *p; p++) {
        h ^= (uint8_t)toupper(*p);
        h *= 16777619u;
    }
    return h;
}

Scope* scope_new(Scope* parent) {
    Scope* scope = fb_calloc(1, sizeof(Scope));
    scope->parent = parent;
    /* FB default: all letters map to SINGLE */
    for (int i = 0; i < 26; i++) {
        scope->deftype[i] = FB_SINGLE;
    }
    /* If parent exists, inherit deftype settings */
    if (parent) {
        memcpy(scope->deftype, parent->deftype, sizeof(scope->deftype));
    }
    return scope;
}

void scope_free(Scope* scope) {
    if (!scope) return;
    for (int i = 0; i < SYMTAB_BUCKETS; i++) {
        Symbol* sym = scope->buckets[i];
        while (sym) {
            Symbol* next = sym->next;
            fbval_release(&sym->value);
            fb_free(sym);
            sym = next;
        }
    }
    fb_free(scope);
}

void scope_clear(Scope* scope) {
    if (!scope) return;
    for (int i = 0; i < SYMTAB_BUCKETS; i++) {
        Symbol* sym = scope->buckets[i];
        while (sym) {
            Symbol* next = sym->next;
            fbval_release(&sym->value);
            fb_free(sym);
            sym = next;
        }
        scope->buckets[i] = NULL;
    }
}

Symbol* scope_lookup(Scope* scope, const char* name) {
    while (scope) {
        Symbol* sym = scope_lookup_local(scope, name);
        if (sym) return sym;
        scope = scope->parent;
    }
    return NULL;
}

Symbol* scope_lookup_local(Scope* scope, const char* name) {
    uint32_t idx = sym_hash(name) % SYMTAB_BUCKETS;
    Symbol* sym = scope->buckets[idx];
    while (sym) {
        if (strcasecmp(sym->name, name) == 0)
            return sym;
        sym = sym->next;
    }
    return NULL;
}

Symbol* scope_insert(Scope* scope, const char* name, SymKind kind, FBType type) {
    /* Check for duplicate */
    if (scope_lookup_local(scope, name))
        return NULL;

    Symbol* sym = fb_calloc(1, sizeof(Symbol));
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    sym->kind = kind;
    sym->type = type;
    sym->target_line = -1;

    /* Initialize default value */
    switch (type) {
        case FB_INTEGER: sym->value = fbval_int(0); break;
        case FB_LONG:    sym->value = fbval_long(0); break;
        case FB_SINGLE:  sym->value = fbval_single(0.0f); break;
        case FB_DOUBLE:  sym->value = fbval_double(0.0); break;
        case FB_STRING:  sym->value = fbval_string_from_cstr(""); break;
        case FB_UDT:     sym->value = fbval_int(0); break; /* UDT handled separately */
    }

    uint32_t idx = sym_hash(name) % SYMTAB_BUCKETS;
    sym->next = scope->buckets[idx];
    scope->buckets[idx] = sym;
    return sym;
}

FBType scope_default_type(Scope* scope, const char* name) {
    if (!name || !name[0]) return FB_SINGLE;
    int idx = toupper(name[0]) - 'A';
    if (idx < 0 || idx >= 26) return FB_SINGLE;
    return scope->deftype[idx];
}
