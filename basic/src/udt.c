/*
 * udt.c — User-Defined Type registry and instance management
 */
#include "udt.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void udt_registry_init(UDTRegistry* reg) {
    memset(reg, 0, sizeof(UDTRegistry));
}

int udt_register(UDTRegistry* reg, const char* name) {
    /* Check for duplicate */
    for (int i = 0; i < reg->type_count; i++) {
        if (strcasecmp(reg->types[i].name, name) == 0) return -1;
    }
    if (reg->type_count >= UDT_MAX_TYPES) return -1;

    int id = reg->type_count++;
    strncpy(reg->types[id].name, name, 41);
    reg->types[id].name[41] = '\0';
    reg->types[id].field_count = 0;
    reg->types[id].total_size = 0;
    return id;
}

int udt_add_field(UDTRegistry* reg, int type_id, const char* name,
                  FBType type, int fixed_str_len, int nested_udt_id) {
    if (type_id < 0 || type_id >= reg->type_count) return -1;
    UDTDef* def = &reg->types[type_id];
    if (def->field_count >= UDT_MAX_FIELDS) return -1;

    int idx = def->field_count++;
    strncpy(def->fields[idx].name, name, 41);
    def->fields[idx].name[41] = '\0';
    def->fields[idx].type = type;
    def->fields[idx].is_fixed_str = (fixed_str_len > 0) ? 1 : 0;
    def->fields[idx].fixed_str_len = fixed_str_len;
    def->fields[idx].udt_type_id = nested_udt_id;
    def->fields[idx].offset = 0;
    return idx;
}

void udt_finalize(UDTRegistry* reg, int type_id) {
    if (type_id < 0 || type_id >= reg->type_count) return;
    UDTDef* def = &reg->types[type_id];
    int offset = 0;
    for (int i = 0; i < def->field_count; i++) {
        def->fields[i].offset = offset;
        switch (def->fields[i].type) {
            case FB_INTEGER: offset += 2; break;
            case FB_LONG:    offset += 4; break;
            case FB_SINGLE:  offset += 4; break;
            case FB_DOUBLE:  offset += 8; break;
            case FB_STRING:
                if (def->fields[i].is_fixed_str)
                    offset += def->fields[i].fixed_str_len;
                else
                    offset += 4; /* pointer size placeholder */
                break;
            default: offset += 4; break;
        }
    }
    def->total_size = offset;
}

int udt_find(const UDTRegistry* reg, const char* name) {
    for (int i = 0; i < reg->type_count; i++) {
        if (strcasecmp(reg->types[i].name, name) == 0) return i;
    }
    return -1;
}

int udt_find_field(const UDTRegistry* reg, int type_id, const char* name) {
    if (type_id < 0 || type_id >= reg->type_count) return -1;
    const UDTDef* def = &reg->types[type_id];
    for (int i = 0; i < def->field_count; i++) {
        if (strcasecmp(def->fields[i].name, name) == 0) return i;
    }
    return -1;
}

FBValue* udt_alloc_instance(const UDTRegistry* reg, int type_id) {
    if (type_id < 0 || type_id >= reg->type_count) return NULL;
    const UDTDef* def = &reg->types[type_id];
    FBValue* fields = calloc(def->field_count, sizeof(FBValue));

    for (int i = 0; i < def->field_count; i++) {
        switch (def->fields[i].type) {
            case FB_INTEGER: fields[i] = fbval_int(0); break;
            case FB_LONG:    fields[i] = fbval_long(0); break;
            case FB_SINGLE:  fields[i] = fbval_single(0.0f); break;
            case FB_DOUBLE:  fields[i] = fbval_double(0.0); break;
            case FB_STRING:
                if (def->fields[i].is_fixed_str) {
                    /* Space-padded fixed string */
                    int len = def->fields[i].fixed_str_len;
                    char* buf = malloc(len + 1);
                    memset(buf, ' ', len);
                    buf[len] = '\0';
                    FBString* s = fbstr_new(buf, len);
                    free(buf);
                    fields[i] = fbval_string(s);
                } else {
                    fields[i] = fbval_string_from_cstr("");
                }
                break;
            default:
                fields[i] = fbval_int(0);
                break;
        }
    }
    return fields;
}

void udt_free_instance(FBValue* fields, const UDTRegistry* reg, int type_id) {
    if (!fields || type_id < 0 || type_id >= reg->type_count) return;
    const UDTDef* def = &reg->types[type_id];
    for (int i = 0; i < def->field_count; i++) {
        fbval_release(&fields[i]);
    }
    free(fields);
}
