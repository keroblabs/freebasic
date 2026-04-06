#ifndef UDT_H
#define UDT_H

#include "value.h"

#define UDT_MAX_FIELDS 256
#define UDT_MAX_TYPES  128

typedef struct {
    char    name[42];
    FBType  type;
    int     is_fixed_str;
    int     fixed_str_len;
    int     udt_type_id;    /* Nested UDT type index (-1 if simple) */
    int     offset;         /* Byte offset within the record */
} UDTField;

typedef struct {
    char      name[42];
    UDTField  fields[UDT_MAX_FIELDS];
    int       field_count;
    int       total_size;
} UDTDef;

typedef struct {
    UDTDef  types[UDT_MAX_TYPES];
    int     type_count;
} UDTRegistry;

void udt_registry_init(UDTRegistry* reg);
int  udt_register(UDTRegistry* reg, const char* name);
int  udt_add_field(UDTRegistry* reg, int type_id, const char* name,
                   FBType type, int fixed_str_len, int nested_udt_id);
void udt_finalize(UDTRegistry* reg, int type_id);
int  udt_find(const UDTRegistry* reg, const char* name);
int  udt_find_field(const UDTRegistry* reg, int type_id, const char* name);
FBValue* udt_alloc_instance(const UDTRegistry* reg, int type_id);
void udt_free_instance(FBValue* fields, const UDTRegistry* reg, int type_id);

#endif
