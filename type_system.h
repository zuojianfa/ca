#ifndef __type_system_h__
#define __type_system_h__

#include "ca.h"
#include "ca_types.h"
#include <stdint.h>

#ifdef __cplusplus
BEGIN_EXTERN_C
#endif

const char *get_printf_format(int type);
int is_unsigned_type(tokenid_t type);
const char *get_printf_format(int type);
typeid_t inference_literal_type(CALiteral *lit);
void determine_literal_type(CALiteral *lit, tokenid_t typetok);
const char *get_type_string(int tok);
CADataType *catype_make_type_symname(int formalname, int type, int size);
CADataType *catype_make_pointer_type(CADataType *datatype);
CADataType *catype_make_array_type(CADataType *type, uint64_t len);
CADataType *catype_make_struct_type(int symname, ST_ArgList *arglist);
CADataType *catype_make_unknown_type(SymTable *scope, typeid_t id, int size);

// type finding
int catype_init();
int catype_put_primitive_by_name(typeid_t name, CADataType *datatype);
CADataType *catype_get_primitive_by_name(typeid_t name);
int catype_put_by_token(int token, CADataType *datatype);
CADataType *catype_get_by_token(int token);
int catype_is_float(tokenid_t typetok);
CADataType *catype_get_by_name(SymTable *symtable, typeid_t name);

void put_post_function(typeid_t fnname, void *carrier);
int exists_post_function(typeid_t fnname);
void *get_post_function(typeid_t fnname);

int catype_check_identical(SymTable *st1, typeid_t type1, SymTable *st2, typeid_t type2);
int catype_check_identical_witherror(SymTable *st1, typeid_t type1,
                                     SymTable *st2, typeid_t type2,
                                     int exitwhenerror, SLoc *loc);
const char *catype_get_function_name(typeid_t fnname);
const char *catype_get_type_name(typeid_t type);
CADataType *catype_unwind_type_object(SymTable *symtable, STEntry *entry);
typeid_t catype_unwind_type_signature(SymTable *symtable, typeid_t name,
                                      int *typesize, CADataType **retdt);

void debug_catype_datatype(const CADataType *datatype);

#ifdef __cplusplus
END_EXTERN_C
#endif

#endif

