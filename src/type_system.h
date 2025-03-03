/**
 * Copyright (c) 2023 Rusheng Xia <xrsh_2004@163.com>
 * CA Programming Language and CA Compiler are licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

/**
 * @file The type system in upper level environment.
 */

#ifndef __type_system_h__
#define __type_system_h__

#include "ca_parser.h"
#include "ca_types.h"
#include "symtable.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
BEGIN_EXTERN_C
#endif

extern CADataType *g_catype_void_ptr;

const char *get_printf_format(int type);
bool catype_is_signed(tokenid_t type);
bool catype_is_unsigned(tokenid_t type);
bool catype_is_integer(tokenid_t type);
bool catype_is_integer_range(CADataType *catype);
const char *get_printf_format(int type);
typeid_t inference_literal_type(CALiteral *lit);
void determine_primitive_literal_type(CALiteral *lit, CADataType *catype);
void determine_literal_type(CALiteral *lit, CADataType *catype);
const char *get_inner_type_string_by_str(const char *name);
const char *get_inner_type_string(int id);
const char *get_type_string(int tok);
const char *get_type_string_for_signature(int tok);
CADataType *catype_clone_thin(const CADataType *type);
CADataType *catype_make_type_symname(int formalname, int type, int size);
CADataType *catype_make_pointer_type(CADataType *datatype);
CADataType *catype_make_array_type(CADataType *type, uint64_t len, bool compact);
CADataType *catype_make_struct_type(int nameid, int typesize, CAStructType struct_type, int init_capacity);
void castruct_add_member(CAStruct *castruct, int name, CADataType *dt, size_t offset);

// type finding
int catype_init();
int catype_put_primitive_by_name(typeid_t name, CADataType *datatype);
CADataType *catype_get_primitive_by_name(typeid_t name);
int catype_put_primitive_by_token(tokenid_t token, CADataType *datatype);
CADataType *catype_get_primitive_by_token(tokenid_t token);
bool catype_is_float(tokenid_t typetok);
bool catype_is_complex_type(CADataType *catype);
CADataType *catype_get_by_name(SymTable *symtable, typeid_t name);
CADataType *catype_from_capattern(CAPattern *cap, SymTable *symtable);
CADataType *catype_from_range(ASTNode *node, GeneralRangeType type, int inclusive, CADataType *startdt, CADataType *enddt);

/*
 * Create a slice catype with only the item catype, not the item pointer
 * catype for convenience. Many data types can perform slice operations,
 * such as arrays, pointers, and slices. The catype for these does not
 * provide the pointer type for their elements directly. Therefore, we use
 * the item catype here, and in the function, it will create the pointer
 * type from the element (item) type.
 */
CADataType *slice_create_catype(CADataType *item_catype);

int catype_check_identical(CADataType *type1, CADataType *type2);
int catype_check_identical_in_symtable(SymTable *st1, typeid_t type1, SymTable *st2, typeid_t type2);
int catype_check_identical_in_symtable_witherror(SymTable *st1, typeid_t type1,
						 SymTable *st2, typeid_t type2,
						 int exitwhenerror, SLoc *loc);
const char *catype_get_function_name(typeid_t fnname);
const char *catype_get_type_name(typeid_t type);
const char *catype_remove_impl_prefix(const char *name);
const char *catype_struct_impl_id_to_function_name_str(typeid_t fnname);
typeid_t catype_struct_impl_id_to_function_name(typeid_t fnname);
typeid_t catype_unwind_type_signature(SymTable *symtable, typeid_t name,
                                      int *typesize, CADataType **retdt);

typeid_t catype_trait_self_ptr_id();
typeid_t catype_trait_self_id();
void debug_catype_datatype(const CADataType *datatype);
int extract_function_or_tuple(SymTable *symtable, int name, STEntry **entry, const char **fnname);
int can_type_binding(CALiteral *lit, tokenid_t typetok);
int64_t parse_to_int64(CALiteral *value);
double parse_to_double(CALiteral *value);

#ifdef __cplusplus
END_EXTERN_C
#endif

#endif

