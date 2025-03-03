/**
 * Copyright (c) 2023 Rusheng Xia <xrsh_2004@163.com>
 * CA Programming Language and CA Compiler are licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v2. You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE. See the
 * Mulan PSL v2 for more details.
 */

#include <alloca.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ca_parser.h"
#include "ca.tab.h"
#include "ca_types.h"
#include "config.h"
#include "dotgraph.h"
#include "symtable.h"
#include "type_system.h"

/* How to manage forward type definitions, allowing the type to be used
   before it is defined?
   1. define an unknown type table
   2. when using a type
     a) when found it in symbol table
        just get and use the it
     b) when the type is not found in symbol table
       1) when found it in the unknown type table
          get and use it, but it is in undefined state
       2) when not found in the unknown type table
          create an unknown type and put it into unknown type table and use it
   3. when defining a type
     a) when found it in symbol table
        report error
     b) when not found
       1) when found in the unknown type table
          get the type object, defining it using the object memory and then
   remove it from the unknown type table 2) when not found in unknown type table
          create the object and define it
       3) put the new type object into symbol table
   4. when walk the tree
      Check the variable type inference to identify its type.
*/

RootTree *gtree = NULL;

/// the root symbol table for global symbols and layer 0 statement running
SymTable g_root_symtable;

ASTNode *main_fn_node = NULL;

/// the generated (when use `-main` option) main function symbol table
SymTable *g_main_symtable = NULL;

SymTable *curr_symtable = NULL;

/// mainly for label processing, because label is function scope symbol
SymTable *curr_fn_symtable = NULL;

/**
 * @details flag to indicate the background type to guide inference the type of
 * literal contained in the right expresssion, it has following regular for type
 * inferencing:
 * 1. if all the operands for a operator are non-fixed literal value, it uses
 * the variable's type
 * 2. When one of the operands is a fixed literal, the type of the other
 * non-fixed literal will be determined by the fixed literal's type. If there
 * are multiple different fixed types in the expression, an error will be
 * reported.
 * 3. When a variable does not specify a value, its type will be inferred from
 * the type of the right-hand side value. The type will be determined by the
 * first value with a fixed type in the expression. If any other part of the
 * expression has a different type, an error will be reported.
 */
int extern_flag = 0; /// indicate if need handling the extern function
// int call_flag = 0;  // indicate if it under a call statement, used for actual
// parameter checking
ST_ArgList curr_arglist;

typeid_t curr_fn_rettype = 0;
int g_node_seqno = 0;

TypeImplInfo *current_type_impl = NULL;
void *type_impl_stack = NULL;
int current_trait_id = 0;

extern int glineno_prev;
extern int gcolno_prev;
extern int glineno;
extern int gcolno;
extern int yychar, yylineno;

int walk(RootTree *tree);

int enable_emit_main() { return genv.emit_main; }

typedef enum OverloadType {
  OLT_Label,
  OLT_Type,
  OLT_Struct,
} OverloadType;

ASTNode *new_ASTNode(ASTNodeType nodetype) {
  ASTNode *p;

  // allocate node
  if ((p = malloc(sizeof(ASTNode))) == NULL) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "out of memory");
    return NULL;
  }

  p->seq = ++g_node_seqno;
  p->type = nodetype;
  p->grammartype = NGT_None;

  return p;
}

void free_ASTNode(ASTNode *node) { free(node); }

const char *sym_form_label_name(const char *name) {
  // TODO: the buffer need reimplement
  static char label_buf[1024];
  sprintf(label_buf, "l:%s", name);
  return label_buf;
}

const char *sym_form_type_name(const char *name) {
  // TODO: the buffer need reimplement
  static char type_buf[1024];
  sprintf(type_buf, "t:%s", name);
  return type_buf;
}

const char *sym_form_function_name(const char *name) {
  // TODO: the buffer need reimplement
  static char type_buf[1024];
  sprintf(type_buf, "f:%s", name);
  return type_buf;
}

const char *sym_form_pointer_name(const char *name) {
  // TODO: the buffer need reimplement
  static char type_buf[1024];
  sprintf(type_buf, "t:*%s", name);
  return type_buf;
}

const char *sym_form_array_name(const char *name, int dimension) {
  // TODO: the buffer need reimplement
  static char type_buf[1024];
  sprintf(type_buf, "t:[%s;%d]", name, dimension);
  return type_buf;
}

typeid_t sym_form_expr_typeof_id(ASTNode *expr) {
  // TODO: the buffer need reimplement
  char type_buf[32];
  sprintf(type_buf, "+:%p", expr);
  typeid_t id = sym_form_type_id_by_str(type_buf);
  return id;
}

ASTNode *astnode_unwind_from_addr(const char *addr, int *len) {
  ASTNode *expr = NULL;
  //*len = sscanf(addr, "+:%p", &expr);
  char *end = NULL;
  long v = strtol(addr + 2, &end, 16);
  *len = end - addr;
  expr = (ASTNode *)v;
  return expr;
}

// id -> (t:)id or (l:)id
typeid_t sym_form_type_id(int id) {
  // const char *name = symname_get(id);
  const char *name = get_inner_type_string(id);

  const char *typename = sym_form_type_name(name);
  typeid_t typeid = symname_check_insert(typename);
  return typeid;
}

typeid_t sym_form_type_id_by_str(const char *idname) {
  const char *typename = sym_form_type_name(idname);
  typeid_t typeid = symname_check_insert(typename);
  return typeid;
}

typeid_t sym_form_label_id(int id) {
  const char *name = symname_get(id);
  const char *typename = sym_form_label_name(name);
  typeid_t labelid = symname_check_insert(typename);
  return labelid;
}

typeid_t sym_form_function_id(int fnid) {
  const char *name = symname_get(fnid);
  const char *typename = sym_form_function_name(name);
  typeid_t typeid = symname_check_insert(typename);
  return typeid;
}

static const char *get_method_impl_prefix(int class_id, int trait_id) {
  static char namebuf[1024];
  assert(class_id != -1);
  const char *clsname = catype_get_type_name(class_id);
  if (trait_id != -1) {
    const char *trait_name = catype_get_type_name(trait_id);
    sprintf(namebuf, "%s::<%s>", clsname, trait_name);
  } else {
    sprintf(namebuf, "%s", clsname);
  }

  return namebuf;
}

typeid_t sym_form_method_id(int fnid, int class_id, int trait_id) {
  const char *name = symname_get(fnid);

  /* For struct implementation, prepending the struct and trait names to the
   function name can prevent confusion when searching for the struct method
   definition, avoiding conflicts with normal function calls that share the same
   name as defined in the struct.
   */
  const char *prefix = get_method_impl_prefix(class_id, trait_id);

  char namebuf[1024];
  sprintf(namebuf, "%s::%s", prefix, name);

  const char *typename = sym_form_function_name(namebuf);
  typeid_t typeid = symname_check_insert(typename);
  return typeid;
}

typeid_t sym_form_pointer_id(typeid_t type) {
  const char *name = catype_get_type_name(type);
  const char *typename = sym_form_pointer_name(name);
  typeid_t typeid = symname_check_insert(typename);
  return typeid;
}

typeid_t sym_form_array_id(typeid_t type, int dimension) {
  const char *name = catype_get_type_name(type);
  const char *typename = sym_form_array_name(name, dimension);
  typeid_t typeid = symname_check_insert(typename);
  return typeid;
}

typeid_t sym_form_tuple_id(typeid_t *types, int argc) {
  // format: t:(;), t:(;i32), t:(;i32, bool), t:(;i32, (;i32, i32)), t:(;(;i32, i32,), i32), ...
  void *hs = string_new();
  string_append(hs, "t:(;");
  for (int i = 0; i < argc; ++i) {
    const char *name = catype_get_type_name(types[i]);
    string_append(hs, name);
    string_append(hs, ",");
  }

  if (argc) {
    // remove tailing ','
    string_pop_back(hs);
  }

  string_append(hs, ")");

  const char *typestr = string_c_str(hs);
  typeid_t typeid = symname_check_insert(typestr);
  string_drop(hs);

  return typeid;
}

const char *sym_form_struct_signature(const char *name, SymTable *st) {
  static char name_buf[1024];
  sprintf(name_buf, "%s@%p", name, st);
  return name_buf;
}

typeid_t sym_form_symtable_type_id(SymTable *st, typeid_t name) {
  static char name_buf[1024];
  const char *chname = catype_get_type_name(name);
  if (st->assoc)
    // TODO: when st->assoc->assoc_table table is temporary created table, it
    // should never hit the cache
    sprintf(name_buf, "%s@%p@%p", chname, st, st->assoc->assoc_table);
  else
    sprintf(name_buf, "%s@%p", chname, st);

  return symname_check_insert(name_buf);
}

typeid_t sym_form_trait_impl_by_str(const char *impl_str) {
  static char name_buf[1024];
  sprintf(name_buf, "i:%s", impl_str);
  return symname_check_insert(name_buf);
}

void set_address(ASTNode *node, const SLoc *first, const SLoc *last) {
  node->begloc = *first;
  node->endloc = *last;
  node->symtable = curr_symtable;
}

int make_program() {
  dot_emit("program", "paragraphs");
  gtree->root_symtable = &g_root_symtable;
  return 0;
}

void make_paragraphs(ASTNode *paragraph) {
  dot_emit("paragraphs", "paragraphs paragraph");
  node_chain(gtree, paragraph);
}

ASTNode *make_fn_def(ASTNode *proto, ASTNode *body) {
  dot_emit("fn_def", "fn_proto fn_body");

  // fix the lexical body with function body for later use (in stage 2 parse)
  assert(body->type == TTE_LexicalBody);

  body->lnoden.fnbuddy = proto;

  proto->fndefn.stmts = body;
  proto->endloc.row = glineno;
  proto->endloc.col = gcolno;
  pop_symtable();

  curr_fn_rettype = 0;
  curr_fn_symtable = NULL;

  if (enable_emit_main()) {
    // push generated main function, current will be the main symbol table
    push_symtable(g_main_symtable);
    curr_fn_symtable = g_main_symtable;
  }

  return proto;
}

ASTNode *make_fn_body(ASTNode *blockbody) {
  dot_emit("fn_body", "block_body");
  return blockbody;
}

ASTNode *make_fn_def_impl_begin(ASTNode *fndef) {
  ASTNode *p = new_ASTNode(TTE_FnDefImpl);
  p->fndefn_impl.impl_info = *current_type_impl;
  p->fndefn_impl.count = fndef ? 1 : 0;
  p->fndefn_impl.data = vec_new();
  if (fndef) {
    int id = symname_check_insert(CSELF);
    STEntry *entry =
        make_type_def_entry(id, current_type_impl->class_id, fndef->symtable,
                            &fndef->begloc, &fndef->endloc);

    vec_append(p->fndefn_impl.data, (void *)fndef);
    set_address(p, &fndef->begloc, &fndef->endloc);
  } else {
    SLoc stloc = {glineno, gcolno};
    set_address(p, &stloc, &stloc);
  }
  return p;
}

ASTNode *make_fn_def_impl_next(ASTNode *impl, ASTNode *fndef) {
  if (impl->type != TTE_FnDefImpl) {
    caerror(&fndef->begloc, &fndef->endloc,
            "wrong impl type: %d required, but %d found", TTE_FnDefImpl,
            impl->type);
    return NULL;
  }

  int id = symname_check_insert(CSELF);
  STEntry *entry =
      make_type_def_entry(id, current_type_impl->class_id, fndef->symtable,
                          &fndef->begloc, &fndef->endloc);

  impl->fndefn_impl.count += 1;
  vec_append(impl->fndefn_impl.data, (void *)fndef);
  impl->endloc = fndef->endloc;

  return impl;
}

ASTNode *trait_fn_begin(ASTNode *fndef_proto) {
  ASTNode *p = new_ASTNode(TTE_TraitFn);
  p->traitfnlistn.count = fndef_proto ? 1 : 0;
  p->traitfnlistn.data = vec_new();
  if (fndef_proto) {
    vec_append(p->traitfnlistn.data, (void *)fndef_proto);
    set_address(p, &fndef_proto->begloc, &fndef_proto->endloc);
  } else {
    SLoc stloc = {glineno, gcolno};
    set_address(p, &stloc, &stloc);
  }
  return p;
}

ASTNode *trait_fn_next(ASTNode *fnlist, ASTNode *fndef_proto) {
  if (fnlist->type != TTE_TraitFn) {
    caerror(&fndef_proto->begloc, &fndef_proto->endloc,
            "wrong impl type: %d required, but %d found", TTE_TraitFn,
            fnlist->type);
    return NULL;
  }

  fnlist->traitfnlistn.count += 1;
  vec_append(fnlist->traitfnlistn.data, (void *)fndef_proto);
  fnlist->endloc = fndef_proto->endloc;

  return fnlist;
}

ASTNode *make_trait_defs(int id, ASTNode *defs) {
  // register it into symbol table, which can query when do implementing
  int trait_id = sym_form_type_id(id);

  defs->traitfnlistn.trait_id = trait_id;

  STEntry *entry = sym_getsym(defs->symtable, trait_id, 0);
  if (entry) {
    caerror(&defs->begloc, &defs->endloc,
            "trait '%s' already defined on (%d, %d)", symname_get(id),
            entry->sloc.row, entry->sloc.col);
    return NULL;
  }

  entry = sym_check_insert(defs->symtable, trait_id, Sym_TraitDef);
  entry->u.trait_def.node = defs;
  entry->u.trait_def.trait_entry = sym_create_trait_defs_entry(defs);

  return defs;
}

ASTNode *make_fn_decl(ASTNode *proto) {
  dot_emit("fn_decl", "EXTERN fn_proto");
  pop_symtable();
  extern_flag = 0;
  curr_fn_rettype = 0;

  if (enable_emit_main()) {
    // push generated main function, current will be the main symbol table
    push_symtable(g_main_symtable);
    curr_fn_symtable = g_main_symtable;
  }

  return proto;
}

static void check_expr_arglists(ST_ArgList *al) {
  int noperands = al->argc;

  // the variable is for checking void type function, can only have void parameter
  int void_count = 0;
  for (int i = 0; i < noperands; ++i) {
    int name = al->argnames[i];
    STEntry *entry = sym_getsym(curr_symtable, name, 0);
    if (!entry) {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "cannot get entry for %s\n", symname_get(name));
      return;
    }

    if (entry->u.varshielding.current->datatype ==
        sym_form_type_id_from_token(VOID))
      void_count += 1;
  }

  if (noperands > 1 && void_count > 0) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "void function should only have void");
    return;
  }
}

void add_fn_args_p(ST_ArgList *arglist, int varg) {
  if (varg)
    dot_emit("fn_args", "fn_args_p VARG");
  else
    dot_emit("fn_args", "fn_args_p");

  arglist->contain_varg = varg;
  check_expr_arglists(arglist);
}

ASTNode *make_stmt_print(ASTNode *expr) {
  ASTNode *p = new_ASTNode(TTE_DbgPrint);
  p->printn.expr = expr;
  set_address(p, &expr->begloc, &expr->endloc);
  return p;
}

ASTNode *make_stmt_print_datatype(typeid_t tid) {
  ASTNode *p = new_ASTNode(TTE_DbgPrintType);
  p->printtypen.type = tid;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_stmt_expr(ASTNode *expr) {
  dot_emit("stmt", "expr");
  expr->grammartype = NGT_stmt_expr;
  return expr;
}

ASTNode *make_stmt_ret_expr(ASTNode *expr) {
  if (!curr_fn_rettype && genv.emit_main) {
    // when not in a function and emit main provided, then use i32 type as the
    // return type `rettype`
    curr_fn_rettype = sym_form_type_id_from_token(I32);
  }

  // check_return_type(curr_fn_rettype);

  ASTNode *p = new_ASTNode(TTE_Ret);
  p->retn.expr = expr;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_stmt_ret() {
  /* if (curr_fn_rettype != sym_form_type_id_from_token(VOID)) {
     SLoc stloc = {glineno, gcolno};
     caerror(&stloc, NULL, "function have no return type"); }
  */

  ASTNode *p = new_ASTNode(TTE_Ret);
  p->retn.expr = NULL;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_stmtexpr_list_block(ASTNode *exprblockbody) {
  dot_emit("stmtexpr_list_block", "exprblock_body");
  // or
  dot_emit("stmt_list_block", "block_body");

  SymTable *st = pop_symtable();
  return exprblockbody;
}

ASTNode *make_stmtexpr_list(ASTNode *stmts, ASTNode *expr) {
  dot_emit("stmtexpr_list", "stmt_list expr");
  // or
  dot_emit("stmtexpr_list", "expr");

  ASTNode *node = make_expr(STMT_EXPR, 2, stmts, expr);
  return node;
}

ASTNode *make_lexical_body(ASTNode *stmts) {
  ASTNode *node = new_ASTNode(TTE_LexicalBody);
  node->lnoden.stmts = stmts;
  node->lnoden.fnbuddy = NULL;
  set_address(node, &stmts->begloc, &stmts->endloc);
  return node;
}

typeid_t make_pointer_type(typeid_t type) {
  return sym_form_pointer_id(type);
  // return catype_make_pointer_type(type);
}

typeid_t make_array_type(typeid_t type, LitBuffer *size) {
  if (size->typetok != U64) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "array size not usize (u64) type, but `%s` type",
            get_type_string(size->typetok));
    return typeid_novalue;
  }

  const char *text = symname_get(size->text);
  if (!text) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "get literal size failed");
    return typeid_novalue;
  }

  uint64_t len;
  sscanf(text, "%lu", &len);
  return sym_form_array_id(type, (int)len);
  // return catype_make_array_type(type, len);
}

typeid_t make_tuple_type(ST_ArgList *arglist) {
  // t:(;), t:(;i32), t:(;i32, bool), t:(;i32, (;i32, i32)), t:(;(;i32, i32,), i32), ...
  typeid_t id = typeid_novalue;
  if (arglist->argc == 1)
    id = arglist->types[0];
  else
    id = sym_form_tuple_id(arglist->types, arglist->argc);

  return id;
}

STEntry *make_type_def_entry(int id, typeid_t type, SymTable *symtable,
                             SLoc *beg, SLoc *end) {
  /*
   * Allow the type name and variable name to share the same name.
   * This is implemented similarly to the label type: add a prefix
   * before the type name.
   */
  typeid_t newtype = sym_form_type_id(id);
  CADataType *primtype = catype_get_primitive_by_name(newtype);
  if (primtype) {
    caerror(beg, NULL, "type alias id `%s` cannot be primitive type",
            symname_get(id));
    return NULL;
  }

  STEntry *entry = sym_getsym(symtable, newtype, 0);
  if (entry) {
    caerror(beg, NULL, "type `%s` defined multiple times", symname_get(id));
    return NULL;
  }

  entry = sym_insert(symtable, newtype, Sym_DataType);
  entry->u.datatype.id = type;
  entry->u.datatype.idtable = symtable;
  entry->u.datatype.runables.opaque = NULL;
  entry->u.datatype.members = NULL;
  entry->sloc = *beg;

  return entry;
}

ASTNode *make_type_def(int id, typeid_t type) {
  SLoc stloc = {glineno, gcolno};
  STEntry *entry = make_type_def_entry(id, type, curr_symtable, &stloc, NULL);
  ASTNode *p = new_ASTNode(TTE_TypeDef);
  p->typedefn.newtype = entry->sym_name;
  p->typedefn.type = type;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &stloc);
  return p;
}

typeid_t make_ret_type_void() {
  dot_emit("ret_type", "");
  typeid_t typesym = sym_form_type_id_from_token(VOID);
  return typesym;
}

void make_type_postfix(IdToken *idt, int id, int typetok) {
  dot_emit("type_postfix", get_type_string(typetok));
  idt->symnameid = id;
  idt->typetok = typetok;
}

void check_return_type(typeid_t fnrettype) {
  if (fnrettype == sym_form_type_id_from_token(VOID)) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "void type function, cannot return a value");
  }
}

/*
 * TODO: Check if the text matches the typetok. For example:
 * - 'a' means char and cannot apply any postfix.
 * - true and false indicate boolean values and cannot apply any postfix.
 *
 * If postfixtypetok == -1, only the type from littypetok will be used,
 * or both typetok values will be considered for error message checking.
 *
 * U64 stands for a positive integer value in lexical context.
 * I64 stands for a signed integer value in lexical context.
 * F64 stands for a floating-point number in lexical context.
 * BOOL stands for a boolean value in lexical context.
 * U8 stands for an unsigned 8-bit integer value in lexical context.
 * I8 stands for a character value in lexical context.
 */

/**
 * @details literal type depends on the input of
 * 1. littypetok: it's the literal type by itself, I64 is for negative integer
 *    value, U64 is for positive integer value, F64 is for floating point value, 
 *    BOOL is true false value, I8 is 'x' value and U8 is '\x' value.
 * 2. postfixtypetok: it's the literal type in the postfix of the literal, e.g.
 *    43243u32 4343243.432f32 43243.343f64 -4332i64 3f64 ..., the scope or type of
 *    postfixtypetok must compitable with the littypetok type. e.g. when literal
 *    value is 4324324321433u32 then the postfixtypetok is U32, it is a bad value
 *    and will report an error because U32 is out of the range the literal. and
 *    when literal value is 43243243.343i32 it also report the error, because
 *    floating point literal value cannot be i32 type. 432432f32 is right value.
 * 3. borning_var_type: if not 0, it means a variable is borning (creating)
 *    and the variable's type is borning_var_type, it will guide the
 *    final literal type.
 * 
 * The three parameters will influence the inference of the literal type 
 * according to the following rules:
 * 1. if all the operands of an operator are non-fixed literal value
 *    (`lit->fixed_type == 0` or postfixtypetok is not provided (-1 value)), it
 *    uses the variable's type (`borning_var_type`)
 * 2. When one operand is a fixed literal, the type of the other non-fixed
 *    literal will be inferred as the type of the fixed literal. If there are
 *    multiple different fixed types in the expression, an error will be reported.
 * 3. When a variable does not specify a value, the literal's type will be
 *    inferred from the type of the right-hand expression (i.e., `right-expr`).
 *    The inference will use the first value with a fixed type as the
 *    expression's type. If other parts of the expression have different types,
 *    an error will be reported regarding the final literal's type.
 *
 * The final literal type should ideally be determined in the walk routines,
 * as it is challenging to ascertain the types during the first scan. This
 * difficulty arises because the expression may consist of multiple parts,
 * and the preceding parts do not have knowledge of the types in the later
 * parts.
 */

void create_literal(CALiteral *lit, int textid, tokenid_t littypetok,
                    tokenid_t postfixtypetok) {
  lit->textid = textid;
  lit->littypetok = littypetok;

  //  The 'a' character is equivalent to the postfixed typetok
  lit->postfixtypetok =
      littypetok == I8 ? I8 : postfixtypetok;
  lit->fixed_type = 0;
  lit->datatype = typeid_novalue;
}

void create_string_literal(CALiteral *lit, const LitBuffer *litb) {
  lit->fixed_type = 1;
  lit->littypetok = litb->typetok; // litb->typetok should be CSTRING;
  lit->textid = litb->text;
  lit->datatype = sym_form_type_id_from_token(litb->typetok);
  lit->u.strvalue.text = litb->text;
  lit->u.strvalue.len = litb->len;
}

SymTable *push_new_symtable_with_parent(SymTable *parent) {
  SymTable *st = (SymTable *)malloc(sizeof(SymTable));
  sym_init(st, parent);
  return st;
}

SymTable *push_new_symtable() {
  SymTable *st = push_new_symtable_with_parent(curr_symtable);
  curr_symtable = st;
  return st;
}

SymTable *push_symtable(SymTable *st) {
  st->parent = curr_symtable;
  curr_symtable = st;
  return st;
}

SymTable *pop_symtable() {
  if (curr_symtable != &g_root_symtable)
    curr_symtable = curr_symtable->parent;

  return curr_symtable;
}

void free_symtable(SymTable *symtable) { free(symtable); }

int add_fn_args(ST_ArgList *arglist, SymTable *st, CAVariable *var) {
  dot_emit("fn_args_p", "fn_args_p ',' iddef_typed");
  // or
  dot_emit("fn_args_p", "iddef_typed");

  SLoc stloc = {glineno, gcolno};

  int name = var->name;
  if (arglist->argc >= MAX_ARGS) {
    caerror(&stloc, NULL, "too many args '%s', max args support is %d",
            symname_get(name), MAX_ARGS);
    return -1;
  }

  STEntry *entry = sym_getsym(st, name, 0);
  if (entry) {
    caerror(&stloc, NULL, "parameter '%s' already defined on line %d, col %d.",
            symname_get(name), entry->sloc.row, entry->sloc.col);
    return -1;
  }

  entry = sym_insert(st, name, Sym_Variable);
  entry->u.varshielding.current = cavar_create(name, var->datatype);
  entry->u.varshielding.varlist = vec_new();
  arglist->argnames[arglist->argc++] = name;
  entry->sloc = var->loc;
  return 0;
}

int add_fn_args_actual(SymTable *st, ASTNode *arg) {
  dot_emit("fn_args_call_p", "fn_args_call_p ',' fn_args_actual");
  // or
  dot_emit("fn_args_call_p", "fn_args_actual");

  ST_ArgListActual *aa = actualarglist_current();
  if (aa->argc >= MAX_ARGS) {
    /*
     * TODO: To output the expression's value or name (noting that literals and
     * single variables can obtain their names), we should implement full text
     * functionality. This functionality would allow us to retrieve text from
     * the source file based on line and column numbers.
     */
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "too many args '%s', max args support is %d",
            "todo:get the args text :)", MAX_ARGS);

    return -1;
  }

  // arg.type must be == AT_Expr
  aa->args[aa->argc++] = arg;
  return 0;
}

ASTNode *make_empty() {
  dot_emit("stmt_list_star", "");

  ASTNode *p = new_ASTNode(TTE_Empty);
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_literal(CALiteral *litv) {
  dot_emit("expr", "literal");

  ASTNode *p = new_ASTNode(TTE_Literal);
  p->litn.litv = *litv;
  p->litn.litv.begloc = (SLoc){glineno_prev, gcolno_prev};
  p->litn.litv.endloc = (SLoc){glineno, gcolno};
  set_address(p, &p->litn.litv.begloc, &p->litn.litv.endloc);

  return p;
}

ASTNode *make_general_range(GeneralRange *range) {
  ASTNode *p = new_ASTNode(TTE_Range);
  p->rangen.range = *range;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  ASTNode *node = make_expr(RANGE, 1, p);
  return node;
}

ASTNode *make_array_def(CAArrayExpr expr) {
  ASTNode *p = new_ASTNode(TTE_ArrayDef);
  p->anoden.aexpr = expr;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  ASTNode *node = make_expr(ARRAY, 1, p);
  return node;
}

CAArrayExpr make_array_def_fill(ASTNode *expr, CALiteral *lit) {
  if (lit->littypetok != U64) {
    caerror(&lit->begloc, &lit->endloc, "litreal is not a valid type",
            get_type_string(lit->littypetok));
    return (CAArrayExpr){.repeat_count = 0, .data = NULL};
  }

  CAArrayExpr caexpr = arrayexpr_new();
  inference_literal_type(lit);
  caexpr = arrayexpr_fill(caexpr, expr, lit->u.i64value);
  return caexpr;
}

ASTNode *make_struct_expr(CAStructExpr expr) {
  ASTNode *p = new_ASTNode(TTE_StructExpr);
  p->snoden = expr;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  ASTNode *node = make_expr(STRUCT, 1, p);
  return node;
}

/**
 * This function is not currently in use, as the tuple expression now
 * utilizes the form of a function call. This is because the syntax for
 * named tuple definitions is the same as that of function calls.
 */
ASTNode *make_tuple_expr(CAStructExpr expr) {
  return NULL;
}

/**
 * Represents the node for the value of an element in an array.
 * This node contains information related to accessing or modifying
 * the value stored at the specified index of the array.
 */
ASTNode *make_arrayitem_right(ArrayItem ai) {
  ASTNode *p = new_ASTNode(TTE_ArrayItemRight);
  p->aitemn = ai;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  ASTNode *node = make_expr(ARRAYITEM, 1, p);
  return node;
}

/**
 * return node:
 * it is an `expression` with operator `STRUCTITEM` who have only `1` operands
 * whose type is `TTE_StructFieldOpRight`
 */
ASTNode *make_structfield_right(StructFieldOp sfop) {
  ASTNode *p = new_ASTNode(TTE_StructFieldOpRight);
  p->sfopn = sfop;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  ASTNode *node = make_expr(STRUCTITEM, 1, p);
  return node;
}

ASTNode *make_boxed_expr(ASTNode *expr) {
  ASTNode *p = new_ASTNode(TTE_Box);
  p->boxn.expr = expr;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  ASTNode *node = make_expr(BOX, 1, p);
  return node;
}

ASTNode *make_drop(int id) {
  ASTNode *p = new_ASTNode(TTE_Drop);
  p->dropn.var = id;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_id(int i, IdType idtype) {
  ASTNode *p = new_ASTNode(TTE_Id);
  p->idn.i = i;
  p->idn.idtype = idtype;

  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_deref_left(DerefLeft deleft) {
  ASTNode *p = new_ASTNode(TTE_DerefLeft);
  p->deleftn = deleft;

  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

int make_attrib_scope(int attrfn, int attrparam) {
  const char *scope = symname_get(attrfn);
  const char *global = symname_get(attrparam);
  if (strcmp(scope, "scope")) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "attribute function here only support `scope`");
    return -1;
  }

  if (strcmp(global, "global") && strcmp(global, "local")) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "attribute function only support `scope`");
    return -1;
  }

  return attrparam;
}

/**
 * For shadowing, there are two options to chain the different shadowing
 * variables with different types:
 * 
 * 1. Create separate CAVariable objects for each shadowing variable.
 * 2. Store all shadowing variables in one CAVariable object.
 * 
 * I chose option 1 because it only requires rotating the variable list in
 * the symbol entry for both the parse stage and the LLVM generation stage.
 */
void register_variable(CAVariable *cavar, SymTable *symtable) {
  STEntry *entry = sym_getsym(symtable, cavar->name, 0);
  if (entry) {
    if (entry->sym_type != Sym_Variable) {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL,
              "symbol '%s' already defined with a non-variable type `%d` in "
              "scope on line %d, col %d.",
              symname_get(cavar->name), entry->sym_type, entry->sloc.row,
              entry->sloc.col);
      return;
    }

    vec_append(entry->u.varshielding.varlist, entry->u.varshielding.current);
  } else {
    entry = sym_insert(symtable, cavar->name, Sym_Variable);
    entry->u.varshielding.varlist = vec_new();
  }

  entry->u.varshielding.current = cavar;
}

ASTNode *make_global_vardef(CAVariable *var, ASTNode *exprn, int global) {
  dot_emit("stmt", "vardef");

  // TODO: realize multiple let statement in one scope in the future

  SymTable *symtable = curr_symtable;
  var->global = 0;

  // curr_symtable == g_main_symtable` already includes the judgement
  if (enable_emit_main()) {
    // only take effect when `-main` option is specified to generate main function
    var->global = global;

    // it is in generated main function scope, and `#[scope(global)]` is provided
    if (curr_symtable == g_main_symtable && var->global) {
      // generate a global variable, use global symbol table here
      symtable = &g_root_symtable;

      /*
       * TODO: FIXME: There are bugs when declaring global variables with attributes.
       * When the right expression is complex and/or not in global scope (due to
       * the use of a generated main function), the declared variable (in global
       * scope) will be placed into the main function scope, causing it to become
       * a non-global variable. 
       * 
       * To address this, reassign the symtable using the global table. However,
       * some bugs may still persist.
       */
      exprn->symtable = symtable;
    }
    // or else generate local variable against main or defined function
  }

  register_variable(var, symtable);

  CAPattern *cap = capattern_new(var->name, PT_Var, NULL);
  cap->datatype = var->datatype;

  ASTNode *p = new_ASTNode(TTE_LetBind);
  p->letbindn.cap = cap;
  p->letbindn.expr = exprn;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &exprn->endloc);
  p->symtable = symtable;
  return p;
}

static void capattern_register_variable(int name, typeid_t datatype, SLoc *loc,
                                        void *sethandler) {
  if (set_exists(sethandler, (void *)(long)name)) {
    caerror(loc, NULL, "the variable `%s` already exists in the same binding",
            symname_get(name));
    return;
  }

  set_insert(sethandler, (void *)(long)name);

  CAVariable *cavar = cavar_create_with_loc(name, datatype, loc);
  register_variable(cavar, curr_symtable);
}

static void register_capattern_symtable(CAPattern *cap, SLoc *loc,
                                        void *sethandler);
void register_structpattern_symtable(CAPattern *cap, int withname, int withsub,
                                     SLoc *loc, void *sethandler) {
  typeid_t type = cap->datatype;
  if (withname) {
    type = sym_form_type_id(cap->name);
    if (cap->datatype != typeid_novalue && cap->datatype != type) {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "left `%s` type not match right `%s`",
              symname_get(cap->name), catype_get_type_name(cap->datatype));
      return;
    }
  }

  if (!withname && !withsub) {
    capattern_register_variable(cap->name, type, loc, sethandler);
  }

  size_t size = vec_size(cap->morebind);
  for (size_t i = 0; i < size; ++i) {
    int name = (int)(long)vec_at(cap->morebind, i);
    capattern_register_variable(name, type, loc, sethandler);
  }

  // walk for structure items
  if (withsub) {
    PatternGroup *pg = cap->items;
    if (cap->type == PT_Array) {
      // array type need all elements be the same data type
      for (int i = 1; i < pg->size; ++i) {
        if (pg->patterns[i - 1]->datatype != pg->patterns[i]->datatype) {
          caerror(loc, loc,
                  "Array pattern expected `%s` on `%d`, but found `%s`",
                  catype_get_type_name(pg->patterns[i - 1]->datatype), i,
                  catype_get_type_name(pg->patterns[i]->datatype));
        }
      }
    }

    for (int i = 0; i < pg->size; ++i) {
      register_capattern_symtable(pg->patterns[i], loc, sethandler);
    }
  }
}

static void register_capattern_symtable(CAPattern *cap, SLoc *loc,
                                        void *sethandler) {
  switch (cap->type) {
  case PT_Var:
    register_structpattern_symtable(cap, 0, 0, loc, sethandler);
    break;
  case PT_Array:
  case PT_GenTuple:
    register_structpattern_symtable(cap, 0, 1, loc, sethandler);
    break;
  case PT_Tuple:
  case PT_Struct:
    register_structpattern_symtable(cap, 1, 1, loc, sethandler);
    break;
  case PT_IgnoreOne:
  case PT_IgnoreRange:
    return;
  }
}

static const char *capattern_check_ignore(CAPattern *cap) {
  int count = 0;
  switch (cap->type) {
  case PT_Var:
    return NULL;
  case PT_Array:
  case PT_GenTuple:
  case PT_Tuple:
  case PT_Struct:
    for (int i = 0; i < cap->items->size; ++i) {
      if (cap->items->patterns[i]->type == PT_IgnoreRange)
        count += 1;
      else {
        const char *error = capattern_check_ignore(cap->items->patterns[i]);
        if (error)
          return error;
      }
    }

    if (!count)
      return NULL;

    // The struct ignore range must be placed at the end of the declaration
    if (count > 1)
      return "capattern: can only have one ignore range field";

    if (cap->type == PT_Struct &&
        cap->items->patterns[cap->items->size - 1]->type != PT_IgnoreRange)
      return "capattern: can only have one ignore range field and must be in "
             "last field for a struct style pattern matching";

    return NULL;
  case PT_IgnoreOne:
    return NULL;
  case PT_IgnoreRange:
    // the ignore range must be in tuple or struct
    return "capattern: the ignore range must be in tuple or struct";
  }
}

/**
 * @brief When the `exprn` is NULL, it indicates an uninitialized let binding:
 *        let a: i32;
 */
ASTNode *make_let_stmt(CAPattern *cap, ASTNode *exprn) {
  const char *error = NULL;
  if ((error = capattern_check_ignore(cap)) != NULL) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, error);
    return NULL;
  }

  /*
   * Parse variables (possibly with different data types) in the CApattern 
   * and record them in the symbol table for later use.
   */
  if (curr_symtable == &g_root_symtable && cap->type != PT_Var) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "left `%s` cannot do pattern match for `%s` globally",
            symname_get(cap->name));
    return NULL;
  }

  void *sethandler = set_new();
  register_capattern_symtable(cap, &exprn->endloc, sethandler);
  set_drop(sethandler);

  ASTNode *p = new_ASTNode(TTE_LetBind);
  p->letbindn.cap = cap;
  p->letbindn.expr = exprn;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &exprn->endloc);
  p->symtable = exprn->symtable;
  return p;
}

ASTNode *make_vardef_zero_value(VarInitType init_type) {
  ASTNode *p = new_ASTNode(TTE_VarDefZeroValue);
  p->varinitn.type = init_type;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

static ASTNode *make_assign_common(ASTNode *left, ASTNode *right) {
  ASTNode *p = new_ASTNode(TTE_Assign);
  p->assignn.id = left;
  p->assignn.op = -1;
  p->assignn.expr = right;
  set_address(p, &left->begloc, &right->endloc);
  return p;
}

static ASTNode *make_assign_var(int id, ASTNode *exprn) {
  dot_emit("stmt", "varassign");

  STEntry *entry = sym_getsym(curr_symtable, id, 1);
  if (!entry) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "symbol '%s' not defined", symname_get(id));
    return NULL;
  }

  ASTNode *idn = make_id(id, TTEId_VarAssign);
  idn->entry = entry;

  ASTNode *p = make_assign_common(idn, exprn);
  return p;
}

static ASTNode *make_deref_left_assign(DerefLeft deleft, ASTNode *exprn) {
  ASTNode *derefln = make_deref_left(deleft);
  ASTNode *p = make_assign_common(derefln, exprn);
  return p;
}

static ASTNode *make_arrayitem_left_assign(ArrayItem ai, ASTNode *exprn) {
  ASTNode *aitemn = new_ASTNode(TTE_ArrayItemLeft);
  aitemn->aitemn = ai;
  set_address(aitemn, &(SLoc){glineno_prev, gcolno_prev},
              &(SLoc){glineno, gcolno});

  ASTNode *p = make_assign_common(aitemn, exprn);
  return p;
}

static ASTNode *make_structfield_left_assign(StructFieldOp sfop,
                                             ASTNode *exprn) {
  ASTNode *sfopn = new_ASTNode(TTE_StructFieldOpLeft);
  sfopn->sfopn = sfop;
  set_address(sfopn, &(SLoc){glineno_prev, gcolno_prev},
              &(SLoc){glineno, gcolno});

  ASTNode *p = make_assign_common(sfopn, exprn);
  return p;
}

ASTNode *make_assign(LeftValueId *lvid, ASTNode *exprn) {
  switch (lvid->type) {
  case LVT_Var:
    return make_assign_var(lvid->var, exprn);
  case LVT_Deref:
    return make_deref_left_assign(lvid->deleft, exprn);
  case LVT_ArrayItem:
    return make_arrayitem_left_assign(lvid->aitem, exprn);
  case LVT_StructOp:
    return make_structfield_left_assign(lvid->structfieldop, exprn);
  default: {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "unknown assignment type");
    return NULL;
  }
  }
}

ASTNode *make_assign_op(LeftValueId *lvid, int op, ASTNode *exprn) {
  ASTNode *node = make_assign(lvid, exprn);
  node->assignn.op = op;
  return node;
}

ASTNode *make_goto(int labelid) {
  dot_emit("stmt", "GOTO label_id");

  /*
   * Because the label name can use the same name as other entities 
   * (variables, functions, etc.), it is internally represented as 
   * "l:<name>" to distinguish them.
   */
  int lpos = sym_form_label_id(labelid);
  STEntry *entry = sym_getsym(curr_fn_symtable, lpos, 0);
  if (entry) {
    switch (entry->sym_type) {
    case Sym_Label:
    case Sym_Label_Hanging:
      break;
    default: {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "label name '%s' appear but not aim to be a label",
              symname_get(labelid));
      return NULL;
    }
    }
  } else {
    entry = sym_insert(curr_fn_symtable, lpos, Sym_Label_Hanging);
    SLoc loc = {glineno, gcolno};
    entry->sloc = loc;
  }

  return make_goto_node(lpos);
}

ASTNode *make_label_def(int labelid) {
  dot_emit("label_def", "label_id");

  const char *name = symname_get(labelid);

  /*
   * Because the label name can use the same name as other entities 
   * (variables, functions, etc.), it is internally represented as 
   * "l:<name>" to distinguish them.
   */
  int lpos = sym_form_label_id(labelid);
  STEntry *entry = sym_getsym(curr_fn_symtable, lpos, 0);
  if (entry) {
    switch (entry->sym_type) {
    case Sym_Label: {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "Label '%s' redefinition", name);
      return NULL;
    }
    case Sym_Label_Hanging:
      entry->sym_type = Sym_Label;
      break;
    default: {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "label name '%s' appear but not aimed to be a label",
              name);
      return NULL;
    }
    }
  } else {
    entry = sym_insert(curr_fn_symtable, lpos, Sym_Label);
  }

  SLoc loc = {glineno, gcolno};
  entry->sloc = loc;

  return make_label_node(lpos);
}

static typeid_t get_structfield_expr_type_from_tree(ASTNode *node) {
  inference_expr_type(node->sfopn.expr);
  typeid_t objtype = get_expr_type_from_tree(node->sfopn.expr);
  if (objtype == typeid_novalue)
    return typeid_novalue;

  CADataType *catype = catype_get_by_name(node->symtable, objtype);
  CHECK_GET_TYPE_VALUE(node, catype, objtype);
  // it's a pointer type
  if (!node->sfopn.direct) {
    if (catype->type != POINTER) {
      caerror(&(node->begloc), &(node->endloc),
              "member reference `%s` is not a pointer type; try '.'?",
              catype_get_type_name(catype->signature));
      return typeid_novalue;
    }

    assert(catype->pointer_layout->dimension == 1);
    catype = catype->pointer_layout->type;
  }

  if (catype->type != STRUCT) {
    caerror(&(node->begloc), &(node->endloc),
            "member reference `%s` is not a struct type",
            catype_get_type_name(catype->signature));
    return typeid_novalue;
  }

  if (catype->struct_layout->type) {
    if (node->sfopn.fieldname < catype->struct_layout->fieldnum)
      return catype->struct_layout->fields[node->sfopn.fieldname]
          .type->signature;
  } else {
    for (int i = 0; i < catype->struct_layout->fieldnum; ++i) {
      if (catype->struct_layout->fields[i].name == node->sfopn.fieldname)
        return catype->struct_layout->fields[i].type->signature;
    }
  }

  caerror(&(node->begloc), &(node->endloc),
          "cannot find field name `%s` in struct `%s`",
          symname_get(node->sfopn.fieldname),
          symname_get(catype->struct_layout->name));

  return typeid_novalue;
}

typeid_t get_expr_type_from_tree(ASTNode *node) {
  switch (node->type) {
  case TTE_Literal:
    return node->litn.litv.datatype;
  case TTE_Id:
    if (!node->entry || node->entry->sym_type != Sym_Variable) {
      caerror(&(node->begloc), &(node->endloc),
              "the name '%s' is not a variable", symname_get(node->idn.i));
      return typeid_novalue;
    }

    return node->entry->u.varshielding.current->datatype;
  case TTE_DerefLeft: {
    ASTNode *expr = node->deleftn.expr;
    typeid_t innerid = get_expr_type_from_tree(expr);
    if (innerid == typeid_novalue)
      return typeid_novalue;

    CADataType *catype = catype_get_by_name(expr->symtable, innerid);
    for (int i = 0; i < node->deleftn.derefcount; ++i) {
      if (catype->type != POINTER) {
        caerror(&(expr->begloc), &(expr->endloc),
                "non pointer type `%s` cannot do dereference, index: `%d`",
                catype_get_type_name(catype->signature), i);
        return typeid_novalue;
      }
      assert(catype->pointer_layout->dimension == 1);
      catype = catype->pointer_layout->type;
    }

    return catype->signature;
  }
  case TTE_ArrayItemLeft: {
    /*
    STEntry *entry = sym_getsym(node->symtable, node->aitemn.varname, 1);
    CADataType *catype = catype_get_by_name(node->symtable,
    entry->u.var->datatype);
    */
    inference_expr_type(node->aitemn.arraynode);
    typeid_t arraytype = get_expr_type_from_tree(node->aitemn.arraynode);
    CADataType *catype = catype_get_by_name(node->symtable, arraytype);
    void *indices = node->aitemn.indices;
    size_t size = vec_size(indices);
    for (int i = 0; i < size; ++i) {
      if (catype->type != ARRAY) {
        caerror(&(node->begloc), &(node->endloc),
                "type `%d` not an array on index `%d`", catype->type, i);
        return typeid_novalue;
      }

      catype = catype->array_layout->type;
    }

    return catype->signature;
  }
  case TTE_StructFieldOpLeft:
    return get_structfield_expr_type_from_tree(node);
  case TTE_As:
    return node->exprasn.type;
  case TTE_Expr:
    return node->exprn.expr_type;
  default:
    return typeid_novalue;
  }
}

const char *get_node_name_or_value(ASTNode *node) {
  switch (node->type) {
  case TTE_Literal:
    return symname_get(node->litn.litv.textid);
  case TTE_Id:
    return symname_get(node->idn.i);
  default:
    /*
     * TODO: Traverse the node and get the string representation of the node 
     * when it is not a literal ID, using walk_node_string or from lexical 
     * analysis.
     */
    return "";
  }
}

int parse_lexical_char(const char *text) {
  if (text[0] != '\\')
    return text[0];

  int n = 0;

  switch (text[1]) {
  case 'r':
    return '\r';
  case 'n':
    return '\n';
  case 't':
    return '\t';
  default:
    n = atoi(text + 1);
    return n;
    {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "unimplemented special character", glineno, gcolno);
      return -1;
    }
  }
}

int is_logic_op(int op) {
  switch (op) {
  case '<':
  case '>':
  case GE:
  case LE:
  case NE:
  case EQ:
    return 1;
  default:
    return 0;
  }
}

typeid_t get_fncall_form_datatype(ASTNode *node, int id) {
  STEntry *entry = sym_getsym(node->symtable, id, 1);
  if (entry)
    return entry->u.f.rettype;

  const char *fnname = catype_get_type_name(id);
  typeid_t tupleid = sym_form_type_id_by_str(fnname);
  entry = sym_getsym(node->symtable, tupleid, 1);
  if (!entry) {
    caerror(&(node->begloc), &(node->endloc),
            "can find entry for function call or tuple call: `%s`", fnname);
    return typeid_novalue;
  }

  if (entry->sym_type != Sym_DataType) {
    caerror(&(node->begloc), &(node->endloc),
            "the entry is not datatype entry: `%s`", fnname);
    return typeid_novalue;
  }

  if (!entry->u.datatype.tuple) {
    caerror(&(node->begloc), &(node->endloc),
            "only support tuple call here: `%s`", fnname);
    return typeid_novalue;
  }

  return entry->u.datatype.id;
}

typeid_t inference_expr_type(ASTNode *node);
typeid_t inference_expr_expr_type(ASTNode *node) {
  CADataType *catype = NULL;
  typeid_t type1 = typeid_novalue;
  int possible_pointer_op = 0;
  int pointer_op = 0;

  if (node->exprn.expr_type != typeid_novalue)
    return node->exprn.expr_type;

  switch (node->exprn.op) {
  case ARRAY: {
    ASTNode *anode = node->exprn.operands[0];
    size_t size = arrayexpr_size(anode->anoden.aexpr);
    typeid_t prevtypeid = typeid_novalue;

    // check if all array element have the same type
    for (int i = 0; i < size; ++i) {
      ASTNode *node = arrayexpr_get(anode->anoden.aexpr, i);
      typeid_t typeid = inference_expr_type(node);
      if (i == 0)
        prevtypeid = typeid;
      if (prevtypeid != typeid) {
        SLoc stloc = {glineno, gcolno};
        caerror(&stloc, NULL,
                "array element `%d` have different type `%s` != `%s`", i,
                catype_get_type_name(prevtypeid), catype_get_type_name(typeid));
        return typeid_novalue;
      }

      prevtypeid = typeid;
    }

    // CADataType *subcatype = catype_get_primitive_by_name(prevtypeid);
    CADataType *subcatype = catype_get_by_name(node->symtable, prevtypeid);
    if (anode->anoden.aexpr.repeat_count)
      size = anode->anoden.aexpr.repeat_count;

    CADataType *catype = catype_make_array_type(subcatype, size, 0);
    type1 = catype->signature;
    break;
  }
  case ARRAYITEM: {
    assert(node->exprn.noperand == 1);
    ASTNode *right = node->exprn.operands[0];
    assert(right->type == TTE_ArrayItemRight);
    // STEntry *entry = sym_getsym(right->symtable, right->aitemn.varname, 1);
    // CADataType *catype = catype_get_by_name(right->symtable,
    // entry->u.var->datatype);
    inference_expr_type(right->aitemn.arraynode);
    typeid_t arraytype = get_expr_type_from_tree(right->aitemn.arraynode);
    CADataType *catype = catype_get_by_name(right->symtable, arraytype);
    void *indices = right->aitemn.indices;
    size_t size = vec_size(indices);
    for (int i = 0; i < size; ++i) {
      if (catype->type != ARRAY && catype->type != SLICE) {
        caerror(&(node->begloc), &(node->endloc),
                "type `%d` not an array on index `%d`", catype->type, i);
        return typeid_novalue;
      }

      ASTNode *indextypenode = (ASTNode *)vec_at(indices, i);
      typeid_t indextypeid = inference_expr_type(indextypenode);
      CADataType *indexcatype =
          catype_get_by_name(right->symtable, indextypeid);
      CHECK_GET_TYPE_VALUE(right, indexcatype, indextypeid);

      switch (catype->type) {
      case ARRAY:
        catype = catype->array_layout->type;
        if (indexcatype->type == RANGE) {
          catype = slice_create_catype(catype);
        }
        break;
      case SLICE:
        catype = catype->struct_layout->fields[0].type->pointer_layout->type;
        if (indexcatype->type == RANGE) {
          catype = slice_create_catype(catype);
        }
        break;
      default:
        break;
      }
    }

    type1 = catype->signature;
    break;
  }
  case STRUCT: {
    ASTNode *anode = node->exprn.operands[0];
    CADataType *catype =
        catype_get_by_name(anode->symtable, anode->snoden.name);
    CHECK_GET_TYPE_VALUE(anode, catype, anode->snoden.name);
    type1 = catype->signature;
    break;
  }
  case TUPLE: {
    // the general tuple expresssion
    ASTNode *anode = node->exprn.operands[0];
    assert(anode->type == TTE_ArgList);
    int argc = anode->arglistn.argc;
    typeid_t *args = (typeid_t *)malloc(argc * sizeof(typeid_t));
    for (int i = 0; i < argc; ++i)
      args[i] = inference_expr_type(anode->arglistn.exprs[i]);

    type1 = sym_form_tuple_id(args, argc);
    free(args);

    CADataType *catype = catype_get_by_name(anode->symtable, type1);
    CHECK_GET_TYPE_VALUE(anode, catype, type1);
    type1 = catype->signature;
    break;
  }
  case RANGE: {
    ASTNode *anode = node->exprn.operands[0];
    GeneralRange *range = &anode->rangen.range;
    CADataType *start_type = NULL;
    CADataType *end_type = NULL;
    typeid_t startid =
        range->start ? inference_expr_type(range->start) : typeid_novalue;
    typeid_t endid =
        range->end ? inference_expr_type(range->end) : typeid_novalue;
    if (range->start) {
      start_type = catype_get_by_name(anode->symtable, startid);
      CHECK_GET_TYPE_VALUE(anode, start_type, startid);
    }

    if (range->end) {
      end_type = catype_get_by_name(anode->symtable, endid);
      CHECK_GET_TYPE_VALUE(anode, end_type, endid);
    }

    CADataType *catype =
        catype_from_range(anode, (GeneralRangeType)range->type,
                          range->inclusive, start_type, end_type);
    CHECK_GET_TYPE_VALUE(anode, catype, 0);
    type1 = catype->signature;
    break;
  }
  case STRUCTITEM: {
    assert(node->exprn.noperand == 1);
    ASTNode *p = node->exprn.operands[0];
    assert(p->type == TTE_StructFieldOpRight);
    inference_expr_type(p->sfopn.expr);
    type1 = get_structfield_expr_type_from_tree(p);
    break;
  }
  case FN_CALL: {
    // Handle calls: suitable for cases when the idn is of all kinds of call types

    // get function return type
    ASTNode *idn = node->exprn.operands[0];
    switch (idn->type) {
    case TTE_Id:
      type1 = get_fncall_form_datatype(node, idn->idn.i);
      break;
    case TTE_Expr: {
      CADataType *struct_catype = NULL;
      assert(node->exprn.noperand == 2);
      ASTNode *name = node->exprn.operands[0];
      STEntry *entry =
          sym_get_function_entry_for_method_novalue(name, &struct_catype);
      type1 = entry->u.f.rettype;
      break;
    }
    case TTE_Domain: {
      STEntry *cls_entry = NULL;
      int fnname = 0;
      STEntry *entry = sym_get_function_entry_for_domainfn(
          idn, node->exprn.operands[1], &cls_entry, &fnname);
      if (IS_GENERIC_FUNCTION(entry->u.f.ca_func_type) ||
          entry->u.f.ca_func_type != CAFT_Function) {
        if (IS_GENERIC_FUNCTION(entry->u.f.ca_func_type) ||
            entry->u.f.ca_func_type == CAFT_MethodInTrait) {
          SymTableAssoc *assoc =
              runable_find_entry_assoc(cls_entry, fnname, -1);
          entry->u.f.arglists->symtable->assoc = assoc;
        }

        CADataType *catype = catype_get_by_name(entry->u.f.arglists->symtable,
                                                entry->u.f.rettype);
        CHECK_GET_TYPE_VALUE(idn, catype, entry->u.f.rettype);
        entry->u.f.arglists->symtable->assoc = NULL;
        type1 = catype->signature;
      } else {
        type1 = entry->u.f.rettype;
      }
      break;
    }
    default:
      caerror(&(node->begloc), &(node->endloc), "bad function call type: %d",
              idn->type);
      break;
    }
    break;
  }
  case STMT_EXPR:
    type1 = inference_expr_type(node->exprn.operands[1]);
    break;
  case SIZEOF:
    type1 = sym_form_type_id_from_token(U64);
    break;
  case DEREF:
    type1 = inference_expr_type(node->exprn.operands[0]);
    catype = catype_get_by_name(node->symtable, type1);
    if (catype->type != POINTER) {
      caerror(
          &(node->begloc), &(node->endloc),
          "only an address (pointer) type can do dereference, `%s` type cannot",
          catype_get_type_name(catype->signature));
      return typeid_novalue;
    }

    // TODO: check if the pointer signature is already formalized
    type1 = catype->pointer_layout->type->signature;
    break;
  case ADDRESS:
    type1 = inference_expr_type(node->exprn.operands[0]);
    catype = catype_get_by_name(node->symtable, type1);
    catype = catype_make_pointer_type(catype);
    type1 = catype->signature;
    break;
  case '+':
  case '-':
    assert(node->exprn.noperand == 2);
    possible_pointer_op = 1;
  case AS:
  case UMINUS:
  case IF_EXPR:
  default:
    for (int i = 0; i < node->exprn.noperand; ++i) {
      typeid_t type = inference_expr_type(node->exprn.operands[i]);
      if (type1 == typeid_novalue) {
        type1 = type;
      } else if (!catype_check_identical_in_symtable(node->symtable, type1,
                                                     node->symtable, type)) {
        if (node->exprn.op == SHIFTL || node->exprn.op == SHIFTR) {
          CADataType *catype1 = catype_get_by_name(node->symtable, type1);
          CADataType *catype2 = catype_get_by_name(node->symtable, type);
          if (!catype_is_integer(catype1->type) ||
              !catype_is_integer(catype2->type)) {
            caerror(
                &(node->begloc), &(node->endloc),
                "expected `integer`, but found `%s` `%s` for shift operation",
                symname_get(type1), symname_get(type));
            return typeid_novalue;
          }
        } else {
          if (possible_pointer_op) {
            CADataType *catype1 = catype_get_by_name(node->symtable, type1);
            CADataType *catype2 = catype_get_by_name(node->symtable, type);
            pointer_op =
                (catype1->type == POINTER && catype_is_integer(catype2->type));
          }

          if (!pointer_op) {
            caerror(&(node->begloc), &(node->endloc),
                    "expected `%s`, found `%s`", symname_get(type1),
                    symname_get(type));
            return typeid_novalue;
          }
        }
      }
    }
    break;
  }

  if (is_logic_op(node->exprn.op))
    type1 = sym_form_type_id_from_token(BOOL);

  node->exprn.expr_type = type1;
  return type1;
}

/**
 * @brief Infer from and set the type property to `expr` when the `expr` have no
 *        determined type associated. This is different from the function
 *        `determine_expr_type`, which need to pass a defined type when it is called.
 */
typeid_t inference_expr_type(ASTNode *p) {
  /*
   * Steps: this is a recursive process.
   * 1. First, infer the expression type. It needs to check for type conflicts 
   *    and determine a valid type.
   * 2. Resolve the node type using function `determine_expr_type(exprn, type)`.
   */
  typeid_t type1 = typeid_novalue;
  CADataType *typedt, *exprdt;
  switch (p->type) {
  case TTE_Literal: {
    CALiteral *litv = &p->litn.litv;
    if (litv->postfixtypetok != tokenid_novalue && !litv->fixed_type) {
      // when the literal carries a postfix, such as i32, u64 etc,
      // then determine them directly
      CADataType *catype = catype_get_primitive_by_token(litv->postfixtypetok);
      determine_primitive_literal_type(litv, catype);
      type1 = catype->signature;
    } else {
      type1 = inference_literal_type(litv);
    }

    litv->fixed_type = 1;
    return type1;
  }
  case TTE_Id:
    if (p->idn.idtype == TTEId_FnName)
      return typeid_novalue;

    return p->entry->u.varshielding.current->datatype;
  case TTE_As:
    type1 = inference_expr_type(p->exprasn.expr);
    // TODO: handle when it is a complex type
    typedt = catype_get_by_name(p->symtable, p->exprasn.type);
    CHECK_GET_TYPE_VALUE(p, typedt, p->exprasn.type);

    exprdt = catype_get_by_name(p->symtable, type1);
    CHECK_GET_TYPE_VALUE(p, exprdt, type1);

    if (!as_type_convertable(exprdt->type, typedt->type)) {
      caerror(&(p->begloc), &(p->endloc),
              "type `%s` cannot convert (as) to type `%s`",
              get_type_string(exprdt->type), get_type_string(typedt->type));
      return -1;
    }

    // return p->exprasn.type;
    return typedt->signature;
  case TTE_Expr:
    return inference_expr_expr_type(p);
  case TTE_If:
    if (!p->ifn.isexpr) {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "if statement is not if expression");
      return typeid_novalue;
    }

    // determine the if expression type
    // TODO: realize multiple if else statement for the if expression
    type1 = inference_expr_type((ASTNode *)(vec_at(p->ifn.bodies, 0)));
    if (p->ifn.remain) {
      typeid_t type2 = inference_expr_type(p->ifn.remain);
      if (!catype_check_identical_in_symtable(p->symtable, type1, p->symtable,
                                              type2)) {
        SLoc stloc = {glineno, gcolno};
        caerror(&stloc, NULL, "if expression type not same `%s` != `%s`",
                symname_get(type1), symname_get(type2));
        return typeid_novalue;
      }
    }
    return type1;
  case TTE_LexicalBody:
    return inference_expr_type(p->lnoden.stmts);
  case TTE_Box:
    type1 = inference_expr_type(p->boxn.expr);
    typedt = catype_get_by_name(p->boxn.expr->symtable, type1);
    exprdt = catype_make_pointer_type(typedt);
    return exprdt->signature;
  default: {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL,
            "the expression already typed, no need to do inference");
    return typeid_novalue;
  }
  }
}

int determine_expr_type(ASTNode *node, typeid_t type);
static int determine_expr_expr_type(ASTNode *node, typeid_t type) {
  if (node->exprn.expr_type != typeid_novalue)
    return 0;

  CADataType *datatype = NULL;

  switch (node->exprn.op) {
  case ARRAY: {
    // Most importantly, check if the inferred type and the determined are compatible
    CADataType *determinedcatype = catype_get_by_name(node->symtable, type);
    if (determinedcatype->type != ARRAY) {
      caerror(&(node->begloc), &(node->endloc),
              "expression type is array type, cannot determined into `%s` type",
              catype_get_type_name(type));
      return -1;
    }

    assert(determinedcatype->array_layout->dimension == 1);
    CAArrayExpr aexpr = node->exprn.operands[0]->anoden.aexpr;
    size_t size = arrayexpr_size(aexpr);
    int len = determinedcatype->array_layout->dimarray[0];
    if (len != size) {
      caerror(&(node->begloc), &(node->endloc),
              "determined array size `%d` not match the expression type `%d`",
              len, size);
      return -1;
    }

    for (size_t i = 0; i < size; ++i) {
      ASTNode *subnode = arrayexpr_get(aexpr, i);
      CADataType *subcatype = determinedcatype->array_layout->type;
      determine_expr_type(subnode, subcatype->signature);
    }

    break;
  }
  case ARRAYITEM: {
    /*
     * For the arrayitem operation, its type should not be determined here, 
     * as its type should already be established before arriving at this point.
     * The following statement is just performing some checks.
     */
    assert(node->exprn.noperand == 1);
    ASTNode *right = node->exprn.operands[0];
    assert(right->type == TTE_ArrayItemRight);
    /* STEntry *entry = sym_getsym(right->symtable, right->aitemn.varname, 1);
       CADataType *catype = catype_get_by_name(right->symtable,
       entry->u.var->datatype);
    */
    inference_expr_type(right->aitemn.arraynode);
    typeid_t arraytype = get_expr_type_from_tree(right->aitemn.arraynode);
    CADataType *catype = catype_get_by_name(right->symtable, arraytype);
    void *indices = right->aitemn.indices;
    size_t size = vec_size(indices);
    for (int i = 0; i < size; ++i) {
      if (catype->type != ARRAY) {
        caerror(&(node->begloc), &(node->endloc),
                "type `%d` not an array on index `%d`", catype->type, i);
        return typeid_novalue;
      }

      catype = catype->array_layout->type;
    }

    CADataType *determinedcatype = catype_get_by_name(node->symtable, type);
    if (determinedcatype->signature != catype->signature) {
      caerror(&(node->begloc), &(node->endloc),
              "determined type on arrayitem operation not equal the array's "
              "nature type `%s` != `%s`",
              catype_get_type_name(determinedcatype->signature),
              catype_get_type_name(catype->signature));
      return -1;
    }

    break;
  }
  case STRUCTITEM: {
    /*
     * Just like the condition of ARRAYITEM.
     * For the structitem operation, its type should not be determined here, 
     * as its type should already be established before arriving at this point.
     * The following statement is just performing some checks.
     */
    assert(node->exprn.noperand == 1);
    ASTNode *p = node->exprn.operands[0];
    assert(p->type == TTE_StructFieldOpRight);
    inference_expr_type(p->sfopn.expr);
    typeid_t origtype = get_structfield_expr_type_from_tree(p);
    if (origtype != type) {
      caerror(&(node->begloc), &(node->endloc),
              "determined type `%s` not compatible with original one `%s`",
              catype_get_type_name(type), catype_get_type_name(origtype));

      return -1;
    }
    break;
  }
  case FN_CALL: {
    // get return type of the function
    // NEXT TODO: handle method call and domain call when idn is not normal id
    ASTNode *idn = node->exprn.operands[0];
    typeid_t type1 = get_fncall_form_datatype(node, idn->idn.i);
    catype_check_identical_in_symtable_witherror(
        node->symtable, type, node->symtable, type1, 1, &node->begloc);
    break;
  }
  case STMT_EXPR:
    determine_expr_type(node->exprn.operands[1], type);
    break;
  case SIZEOF:
    if (type != sym_form_type_id_from_token(U64)) {
      caerror(&(node->begloc), &(node->endloc),
              "conflict when determining type: `%s` != `u64`",
              catype_get_type_name(type));

      return -1;
    }
    break;
  case DEREF:
    datatype = catype_get_by_name(node->symtable, type);
    datatype = catype_make_pointer_type(datatype);
    determine_expr_type(node->exprn.operands[0], datatype->signature);
    break;
  case ADDRESS: {
    /*
     * Check if the right-hand side is a variable and if its address type 
     * matches the determined type. Only a variable can have an address.
     */
    // TODO: handle function address
    ASTNode *idnode = node->exprn.operands[0];
    if (idnode->type != TTE_Id) {
      caerror(&(node->begloc), &(node->endloc),
              "only a variable can have an address, but find type `%d`",
              idnode->type);
      return -1;
    }

    datatype = catype_get_by_name(node->symtable, type);
    if (datatype->type != POINTER) {
      caerror(&(node->begloc), &(node->endloc),
              "a pointer type cannot determined into `%s` type",
              catype_get_type_name(datatype->signature));
      return -1;
    }

    CADataType *idcatype = catype_get_by_name(
        idnode->symtable, idnode->entry->u.varshielding.current->datatype);

    if (idcatype->signature != datatype->pointer_layout->type->signature) {
      caerror(&(node->begloc), &(node->endloc),
              "variable address type `%s` cannot be type of `%s`",
              catype_get_type_name(idcatype->signature),
              catype_get_type_name(datatype->pointer_layout->type->signature));
      return -1;
    }

    break;
  }
  case '>':
  case '<':
  case GE:
  case LE:
  case NE:
  case EQ: {
    // determine the type of logical expresssion, they must be bool
    datatype = catype_get_by_name(node->symtable, type);
    if (datatype->type != BOOL) {
      caerror(&(node->begloc), &(node->endloc),
              "`bool` type is required for determining the logical operation, but `%s` "
              "type found",
              catype_get_type_name(datatype->signature));
      return -1;
    }

    inference_expr_type(node);
    break;
  }
  case '+':
  case '-':
    assert(node->exprn.noperand == 2);
    datatype = catype_get_by_name(node->symtable, type);
    if (datatype->type == POINTER) {
      if (node->exprn.operands[0]->type == TTE_Expr)
        determine_expr_type(node->exprn.operands[0], type);

      typeid_t firstid = get_expr_type_from_tree(node->exprn.operands[0]);
      if (firstid == typeid_novalue) {
        caerror(&(node->begloc), &(node->endloc),
                "when determining pointer type, right value should already "
                "determined, but here cannot find a determined type");
        return -1;
      }

      CADataType *firstca = catype_get_by_name(node->symtable, firstid);
      if (firstca->type != POINTER) {
        caerror(
            &(node->begloc), &(node->endloc),
            "should only can determined into pointer type, but find `%s` type",
            catype_get_type_name(firstid));
        return -1;
      }

      if (datatype->signature != firstca->signature) {
        caerror(&(node->begloc), &(node->endloc),
                "determined type `%s` not equal to determining type `%s`",
                catype_get_type_name(datatype->signature),
                catype_get_type_name(firstca->signature));
        return -1;
      }

      typeid_t secondid = inference_expr_type(node->exprn.operands[1]);
      CADataType *secondca = catype_get_by_name(node->symtable, secondid);
      if (!catype_is_integer(secondca->type)) {
        caerror(&(node->begloc), &(node->endloc),
                "the 2nd pointer operand not support non-integer type, but "
                "find `%s`",
                catype_get_type_name(secondca->signature));
        return -1;
      }

      node->exprn.operands[1]->exprn.expr_type = secondca->signature;
      break;
    }
  case LAND:
  case LOR:
  case AS:
  case UMINUS:
  case IF_EXPR:
  default:
    for (int i = 0; i < node->exprn.noperand; ++i) {
      determine_expr_type(node->exprn.operands[i], type);
    }
    break;
  }

  node->exprn.expr_type = type;
  return 0;
}

/**
 * @brief Determining and set the type property to `expr`. The type property is
 *        coming from @param type. This function is different from function
 *        `inference_expr_type` which lack of @param type.
*/
int determine_expr_type(ASTNode *node, typeid_t type) {
  // TODO: handle when complex type
  CADataType *catype = catype_get_by_name(node->symtable, type);
  CHECK_GET_TYPE_VALUE(node, catype, type);
  typeid_t id;
  CADataType *exprcatype = NULL;
  switch (node->type) {
  case TTE_Literal: {
    CALiteral *litv = &node->litn.litv;
    if (litv->postfixtypetok != tokenid_novalue && !litv->fixed_type) {
      // when the literal carries a postfix, such as i32, u64 etc,
      // then determine them directly
      CADataType *postcatype =
          catype_get_primitive_by_token(litv->postfixtypetok);
      determine_primitive_literal_type(litv, postcatype);
      litv->fixed_type = 1;
      if (postcatype->signature != catype->signature) {
        caerror(&(node->begloc), &(node->endloc),
                "`%s` type required, but found `%s`\n",
                catype_get_type_name(catype->signature),
                catype_get_type_name(postcatype->signature));
        return -1;
      }
    }

    if (litv->fixed_type)
      return 0;

    /*
     * Here, determine the literal type at this point, 
     * in contrast to when creating the literal node.
     */
    determine_literal_type(litv, catype);
    break;
  }
  case TTE_Id:
    if (!node->entry) {
      STEntry *entry = sym_getsym(node->symtable, node->idn.i, 1);
      if (!entry) {
        caerror(&(node->begloc), &(node->endloc),
                "cannot find symbol for id: `%d` in symbol table\n",
                node->idn.i);
        return -1;
      }

      if (entry->sym_type == Sym_Variable) {
        caerror(&(node->begloc), &(node->endloc),
                "variable not filled the entry yet: `%s`\n",
                symname_get(entry->sym_name));
        return -1;
      } else {
        // no need to determine type
        return 0;
      }
    }

    if (node->entry->u.varshielding.current->datatype == typeid_novalue)
      node->entry->u.varshielding.current->datatype = catype->signature;
    else if (!catype_check_identical_in_symtable(
                 node->symtable, node->entry->u.varshielding.current->datatype,
                 node->symtable, type)) {
      // fprintf(stderr,
      caerror(&(node->begloc), &(node->endloc),
              "determine different type `%s` != `%s`\n", symname_get(type),
              symname_get(node->entry->u.varshielding.current->datatype));
      return 0;
    }

    break;
  case TTE_DerefLeft: {
    ASTNode *expr = node->deleftn.expr;
    typeid_t exprid = inference_expr_type(expr);
    typeid_t innerid = get_expr_type_from_tree(expr);
    assert(exprid == innerid);
    if (innerid == typeid_novalue) {
      caerror(&(expr->begloc), &(expr->endloc),
              "dereference left operation must be fixed type to: `%s`, but "
              "find non-fixed",
              catype_get_type_name(type));
      return typeid_novalue;
    }

    CADataType *catype = catype_get_by_name(expr->symtable, innerid);
    for (int i = 0; i < node->deleftn.derefcount; ++i) {
      if (catype->type != POINTER) {
        caerror(&(expr->begloc), &(expr->endloc),
                "non array type `%s` cannot do dereference, index: `%d`",
                catype_get_type_name(catype->signature), i);
        return -1;
      }
      assert(catype->pointer_layout->dimension == 1);
      catype = catype->pointer_layout->type;
    }

    if (catype->signature != type) {
      caerror(&(expr->begloc), &(expr->endloc),
              "determined type `%s` not compatible with `%s`",
              catype_get_type_name(type),
              catype_get_type_name(catype->signature));
      return -1;
    }

    break;
  }
  case TTE_As:
    exprcatype = catype_get_by_name(node->symtable, node->exprasn.type);
    CHECK_GET_TYPE_VALUE(node, exprcatype, node->exprasn.type);

    if (!catype_check_identical(catype, exprcatype)) {
      caerror(&(node->begloc), &(node->endloc),
              "type `%s` cannot determined into `%s`",
              catype_get_type_name(catype->signature),
              catype_get_type_name(exprcatype->signature));
      return -1;
    }

    // here should keep the original type as it is
    id = inference_expr_type(node->exprasn.expr);
    // determine_expr_type(node->exprasn.expr, type);
    break;
  case TTE_Expr:
    return determine_expr_expr_type(node, type);
  case TTE_If:
    // only expression can arrive here, statement cannot go here
    if (!node->ifn.isexpr) {
      SLoc stloc = {glineno, gcolno};
      caerror(&stloc, NULL, "if statement is not if expression");
      return -1;
    }

    // determine `if` expression type
    // TODO: realize multiple if else statement
    determine_expr_expr_type((ASTNode *)(vec_at(node->ifn.bodies, 0)), type);
    if (node->ifn.remain) {
      determine_expr_expr_type(node->ifn.remain, type);
    }
    break;
  case TTE_StructExpr: {
    CADataType *struct_catype =
        catype_get_by_name(node->symtable, node->snoden.name);
    CHECK_GET_TYPE_VALUE(node, struct_catype, node->snoden.name);
    if (struct_catype->signature != catype->signature) {
      caerror(&node->begloc, &node->endloc, "type `%s` required, but find `%s`",
              catype_get_type_name(catype->signature),
              catype_get_type_name(struct_catype->signature));
      return -1;
    }
    break;
  }
  default:
    // this node don't need to determine a type, for it's not a literal or expression node
    break;
  }

  return 0;
}

// reduce the node type when existing one of the determined type in the
// expression, when all part are not determined then not determine the type
// when in walk stage the assignment statement will determine the variable's
// type and the right expression's type when the expression's type not
// determined: int reduce_node_and_type(ASTNode *p, typeid_t *expr_types, int
// noperands)

/**
 * @brief
 * In this function, the 'reduce' word means to infer something from a group
 * of nodes. Here, it infers the node type as a whole. When one of the nodes
 * in the group already has a type associated with it, then the node group type
 * can be determined. When none of the nodes in the group has an associated
 * type, then the node group's type cannot be determined.
 *
 * During the walk stage, for the assignment statement, both the left side
 * variable and the right side expression can have their types determined
 * if the expression type is not already determined.
 */
typeid_t reduce_node_and_type_group(ASTNode **nodes, typeid_t *expr_types,
                                    int nodenum, int assignop) {
  /*
   * Check each node in the group to see if they are already associated with
   * a type, and verify if there are any conflicts for the nodes that already
   * have an associated type. However, a literal value cannot have a
   * determined type if it is not associated with one. When the literal value
   * is in tree form, we can still infer the tree's type by traversing the
   * tree nodes and find if any node can be determined a type. For
   * example, when the expression contains a fixed type postfix, we can
   * definitely determine its type.
   */
  typeid_t type1 = typeid_novalue;
  int typei = 0;
  int notypeid = 0;
  int *nonfixed_node = (int *)alloca(nodenum * sizeof(int));
  for (int i = 0; i < nodenum; ++i) {
    if (expr_types[i] != typeid_novalue) {
      nonfixed_node[i] = 0;
      if (type1 == typeid_novalue) {
        type1 = expr_types[i];
        typei = i;
      } else if (assignop == -1 && !catype_check_identical_in_symtable(
                                       nodes[i]->symtable, type1,
                                       nodes[i]->symtable, expr_types[i])) {
        // when assignop not -1 it will not check the type
        CADataType *dt1 = catype_get_by_name(nodes[i]->symtable, type1);
        CADataType *dt2 = catype_get_by_name(nodes[i]->symtable, expr_types[i]);

        caerror(&(nodes[i]->begloc), &(nodes[i]->endloc),
                "type name conflicting: type `%s`(`%s`) with type `%s`(`%s`)",
                catype_get_type_name(type1),
                catype_get_type_name(dt1->signature),
                catype_get_type_name(expr_types[i]),
                catype_get_type_name(dt2->signature));
        return 0;
      }
    } else {
      nonfixed_node[i] = 1;
    }
  }

  if (assignop != -1) {
    CADataType *dt = catype_get_by_name(nodes[0]->symtable, type1);
    if (dt->type == POINTER) {
      if (assignop != ASSIGN_ADD && assignop != ASSIGN_SUB) {
        caerror(
            &(nodes[0]->begloc), &(nodes[1]->endloc),
            "pointer inplace assignment only support `+` `-`, but find `%d`",
            assignop);
        return 0;
      }

      typeid_t id = expr_types[1];
      if (id == typeid_novalue) {
        id = inference_expr_type(nodes[1]);
      }

      CADataType *right_dt = catype_get_by_name(nodes[1]->symtable, id);
      if (!catype_is_integer(right_dt->type)) {
        caerror(&(nodes[0]->begloc), &(nodes[1]->endloc),
                "pointer inplace assignment right side number only support "
                "integer, but find `%s`",
                catype_get_type_name(right_dt->signature));
        return 0;
      }

      return dt->signature;
    }
  }

  // when the group contains any node with fixed type
  for (int i = 0; i < nodenum; ++i) {
    if (nonfixed_node[i]) {
      determine_expr_type(nodes[i], type1);
    }
  }

  return type1;
}

/**
 * For the expression of UMINUS + - * / < > GE LE NE EQ
 * The left and right side type are calculated or inferred separately.
 * If the right side has a fixed type, or if the right side will use the
 * left side's type, and the left side does not have a type yet, then it
 * will use the right side's type. If the right side has no fixed type,
 * the right side literal will use its default type or intended type.
 */
ASTNode *make_expr(int op, int noperands, ...) {
  dot_emit_expr("from", "to", op);

  va_list ap;
  int i;

  ASTNode *p = new_ASTNode(TTE_Expr);
  p->exprn.op = op;
  p->exprn.noperand = noperands;

  if ((p->exprn.operands = malloc(noperands * sizeof(ASTNode))) == NULL) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "out of memory");
    return NULL;
  }

  // try to infer the expression's type here
  va_start(ap, noperands);
  for (i = 0; i < noperands; i++) {
    p->exprn.operands[i] = va_arg(ap, ASTNode *);
  }
  va_end(ap);

  p->exprn.expr_type = typeid_novalue;

  const SLoc *beg = &(SLoc){glineno, gcolno};
  const SLoc *end = &(SLoc){glineno, gcolno};

  if (noperands == 1) {
    if (p->exprn.operands[0]) {
      beg = &p->exprn.operands[0]->begloc;
      end = &p->exprn.operands[0]->endloc;
    }
  } else if (noperands > 1) {
    if (p->exprn.operands[0]) {
      beg = &p->exprn.operands[0]->begloc;
    }

    if (p->exprn.operands[noperands - 1]) {
      end = &p->exprn.operands[noperands - 1]->endloc;
    }
  }

  p->begloc = *beg;
  p->endloc = *end;

  p->symtable = curr_symtable;
  return p;
}

ASTNode *make_expr_arglists_actual(ST_ArgListActual *al) {
  dot_emit("fn_args_call", "fn_args_call_p");

  int argc = al ? al->argc : 0;

  ASTNode *p = new_ASTNode(TTE_ArgList);
  p->arglistn.argc = argc;

  if (al && (p->arglistn.exprs = (ASTNode **)malloc(argc * sizeof(ASTNode))) ==
                NULL) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "out of memory");
    return NULL;
  }

  for (int i = 0; i < argc; i++)
    p->arglistn.exprs[i] = al->args[i];

  if (argc == 1) {
    p->begloc = p->arglistn.exprs[0]->begloc;
    p->endloc = p->arglistn.exprs[0]->endloc;
  } else if (argc > 1) {
    p->begloc = p->arglistn.exprs[0]->begloc;
    p->endloc = p->arglistn.exprs[argc - 1]->endloc;
  } else {
    p->begloc = (SLoc){glineno, gcolno};
    p->endloc = (SLoc){glineno, gcolno};
  }

  p->symtable = curr_symtable;
  if (al)
    actualarglist_pop();

  return p;
}

ASTNode *make_label_node(int i) {
  ASTNode *p = make_id(i, TTEId_Label);
  if (p) {
    p->type = TTE_Label;
    return p;
  }

  return NULL;
}

ASTNode *make_goto_node(int i) {
  ASTNode *p = make_id(i, TTEId_LabelGoto);
  if (p) {
    p->type = TTE_LabelGoto;
    return p;
  }

  return NULL;
}

ASTNode *build_mock_main_fn_node() {
  ASTNode *decl = new_ASTNode(TTE_FnDecl);

  typeid_t typesym = sym_form_type_id_from_token(I32);
  decl->fndecln.ret = typesym;

  int fnid = symname_check("main");
  decl->fndecln.name = sym_form_function_id(fnid);

  decl->fndecln.is_extern = 0;

  set_address(decl, &(SLoc){glineno_prev, gcolno_prev},
              &(SLoc){glineno, gcolno});
  ASTNode *p = new_ASTNode(TTE_FnDef);
  p->fndefn.fn_decl = decl;
  p->fndefn.stmts = NULL;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});

  return p;
}

static ASTNode *build_fn_decl(typeid_t name, void *generic_types,
                              ST_ArgList *al, typeid_t rettype, SLoc beg,
                              SLoc end) {
  ASTNode *p = new_ASTNode(TTE_FnDecl);
  p->fndecln.ret = rettype;
  p->fndecln.name = name;
  p->fndecln.generic_types = generic_types;
  p->fndecln.args = *al;
  p->fndecln.is_extern = 0; // TODO: make extern real extern

  set_address(p, &beg, &end);
  return p;
}

static ASTNode *build_fn_define(typeid_t name, void *generic_types,
                                ST_ArgList *al, typeid_t rettype, SLoc beg,
                                SLoc end) {
  ASTNode *decl = build_fn_decl(name, generic_types, al, rettype, beg, end);
  ASTNode *p = new_ASTNode(TTE_FnDef);
  p->fndefn.fn_decl = decl;
  p->fndefn.stmts = NULL;

  set_address(p, &beg, &end);
  return p;
}

ASTNode *make_break() {
  ASTNode *p = new_ASTNode(TTE_Break);
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_continue() {
  ASTNode *p = new_ASTNode(TTE_Continue);
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_loop(ASTNode *loopbody) {
  ASTNode *p = new_ASTNode(TTE_Loop);
  p->loopn.body = loopbody;

  set_address(p, &loopbody->begloc, &loopbody->endloc);
  return p;
}

void make_for_var_entry(int id) {
  STEntry *entry = sym_getsym(curr_symtable, id, 0);
  if (entry) {
    SLoc stloc = {glineno, gcolno};
    caerror(
        &stloc, NULL,
        "strange variable '%s' already defined in scope on line %d, col %d.",
        symname_get(id), entry->sloc.row, entry->sloc.col);
    return;
  }

  entry = sym_insert(curr_symtable, id, Sym_Variable);
  CAVariable *cavar = cavar_create(id, typeid_novalue);
  entry->u.varshielding.current = cavar;
}

ASTNode *make_for(ForStmtId id, ASTNode *listnode, ASTNode *stmts) {
  ASTNode *p = new_ASTNode(TTE_For);
  p->forn.var = id;
  p->forn.listnode = listnode;
  p->forn.body = stmts;

  set_address(p, &listnode->begloc, &stmts->endloc);
  return p;
}

ASTNode *make_for_stmt(ForStmtId id, ASTNode *listnode, ASTNode *stmts) {
  ASTNode *forn = make_for(id, listnode, stmts);

  // Inner variable and(or) listnode also need a lexical body in the for statement
  ASTNode *node = make_lexical_body(forn);
  SymTable *st = pop_symtable();
  return node;
}

ASTNode *make_while(ASTNode *cond, ASTNode *whilebody) {
  dot_emit("stmt", "whileloop");

  ASTNode *p = new_ASTNode(TTE_While);
  p->whilen.cond = cond;
  p->whilen.body = whilebody;

  set_address(p, &cond->begloc, &whilebody->endloc);
  return p;
}

ASTNode *new_ifstmt_node() {
  ASTNode *p = new_ASTNode(TTE_If);
  p->ifn.ncond = 0;
  p->ifn.isexpr = 0;
  p->ifn.conds = NULL;
  p->ifn.bodies = NULL;
  p->ifn.remain = NULL;
  return p;
}

ASTNode *make_ifpart(ASTNode *p, ASTNode *cond, ASTNode *body) {
  vec_append(p->ifn.conds, cond);
  vec_append(p->ifn.bodies, body);
  return p;
}

ASTNode *make_elsepart(ASTNode *p, ASTNode *body) {
  p->ifn.remain = body;
  return p;
}

/**
 * @details Compare if the function prototype which is defined prevously is the
 * same as current defining
 */
static int pre_check_fn_proto(STEntry *prev, typeid_t fnname,
                              ST_ArgList *currargs, typeid_t rettype) {
  ST_ArgList *prevargs = prev->u.f.arglists;

  // check if function declaration is the same
  if (currargs->argc != prevargs->argc) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL,
            "function '%s' parameter number not identical with previous, see: "
            "line %d, col %d.",
            catype_get_function_name(fnname), prev->sloc.row, prev->sloc.col);
    return -1;
  }

  // check parameter types
  if (prevargs->contain_varg != currargs->contain_varg) {
    SLoc stloc = {glineno, gcolno};
    caerror(
        &stloc, NULL,
        "function '%s' variable parameter not identical, see: line %d, col %d.",
        catype_get_function_name(fnname), prev->sloc.row, prev->sloc.col);
    return -1;
  }

  return 0;
}

static STEntry *check_tuple_name(int id) {
  // check tuple type
  typeid_t type = sym_form_type_id(id);
  SymTable *symtable = sym_parent_or_global(curr_symtable);
  STEntry *entry = sym_getsym(symtable, type, 0);

  if (entry && entry->u.datatype.tuple)
    return entry;

  return NULL;
}

static void check_arglist_names(ST_ArgList *arglist) {
  if (current_trait_id ||
      (current_type_impl && current_type_impl->fn_def_recursive_count == 0)) {
    for (int i = 1; i < arglist->argc; ++i) {
      const char *argname = symname_get(arglist->argnames[i]);
      if (!strcmp(argname, OSELF)) {
        STEntry *entry = sym_getsym(arglist->symtable, arglist->argnames[i], 0);
        caerror(&entry->sloc, NULL,
                "`self` must be the first parameter of an associated function");
      }
    }
  }
}

ASTNode *make_fn_proto(FnNameInfo *name_info, ST_ArgList *arglist,
                       typeid_t rettype) {
  dot_emit("fn_proto", "FN IDENT ...");
  int fnid = name_info->fnname;

  // NEXT TODO: handle name_info->generic_types

  typeid_t fnname = typeid_novalue;
  if (current_trait_id)
    fnname = sym_form_method_id(fnid, current_trait_id, -1);
  else if (current_type_impl && current_type_impl->fn_def_recursive_count == 0)
    fnname = sym_form_method_id(fnid, current_type_impl->class_id,
                                current_type_impl->trait_id);
  else
    fnname = sym_form_function_id(fnid);

  check_arglist_names(arglist);

  curr_fn_rettype = rettype;

  SLoc beg = {glineno, gcolno};
  SLoc end = {glineno, gcolno};

  if (check_tuple_name(fnid)) {
    caerror(&beg, NULL, "function '%s' already defined as tuple in previous",
            symname_get(fnid));
    return NULL;
  }

  SymTable *symtable = sym_parent_or_global(curr_symtable);
  STEntry *entry = sym_getsym(symtable, fnname, 0);
  if (extern_flag) {
    if (entry) {
      pre_check_fn_proto(entry, fnname, arglist, rettype);
    } else {
      entry = sym_check_insert(symtable, fnname, Sym_FnDecl);
      entry->u.f.arglists = (ST_ArgList *)malloc(sizeof(ST_ArgList));
      *entry->u.f.arglists = *arglist;
      entry->u.f.ca_func_type = CAFT_Function;
      entry->u.f.generic_types = NULL;
    }
    entry->u.f.rettype = rettype;

    ASTNode *decln = build_fn_decl(fnname, name_info->generic_types, arglist,
                                   rettype, beg, end);

    return decln;
  } else {
    if (entry) {
      if (current_trait_id) {
        caerror(&beg, NULL,
                "trait method '%s' already defined on line %d, col %d.",
                symname_get(fnname), entry->sloc.row, entry->sloc.col);
        return NULL;
      }

      if (entry->sym_type == Sym_FnDef) {
        caerror(&beg, NULL, "function '%s' already defined on line %d, col %d.",
                symname_get(fnname), entry->sloc.row, entry->sloc.col);
        return NULL;
      }

      if (entry->sym_type == Sym_FnDecl) {
        entry->sym_type = Sym_FnDef;
        entry->sloc = beg;
      } else {
        caerror(&beg, NULL,
                "name '%s' is not a function defined on line %d, col %d.",
                symname_get(fnname), entry->sloc.row, entry->sloc.col);
        return NULL;
      }

      pre_check_fn_proto(entry, fnname, arglist, rettype);
    } else {
      entry = sym_check_insert(symtable, fnname, Sym_FnDef);
      entry->u.f.arglists = (ST_ArgList *)malloc(sizeof(ST_ArgList));
      *entry->u.f.arglists = *arglist;

      if (current_trait_id)
        entry->u.f.ca_func_type = CAFT_MethodInTrait;
      else if (current_type_impl &&
               current_type_impl->fn_def_recursive_count == 0) {
        if (current_type_impl->trait_id != -1)
          entry->u.f.ca_func_type = CAFT_MethodForTrait;
        else
          entry->u.f.ca_func_type = CAFT_Method;
      } else
        entry->u.f.ca_func_type = CAFT_Function;

      entry->u.f.generic_types = name_info->generic_types;
      if (name_info->generic_types) {
        entry->u.f.ca_func_type |= CAFT_GenericFunction;
      }
    }
    entry->u.f.rettype = rettype;

    ASTNode *defn = build_fn_define(fnname, name_info->generic_types, arglist,
                                    rettype, beg, end);

    /*
     * fix the symbol table, when function can be defined in inner scope,
     * it should uses the parent's symbol table
     * node->symtable = symtable;
     */
    return defn;
  }
}

int check_fn_define(typeid_t fnname, ASTNode *param, int tuple, STEntry *entry,
                    int is_method) {
  // check for formal parameter and actual parameter
  ST_ArgList *formalparam = NULL;
  if (tuple)
    formalparam = entry->u.datatype.members;
  else
    formalparam = entry->u.f.arglists;

  if (!formalparam) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL,
            "cannot find arglist, seems `%s` is not a function or named tuple",
            catype_get_type_name(fnname));
    return -1;
  }

  int argc = param->arglistn.argc;
  if (is_method)
    argc += 1;

  // check parameter number
  if (formalparam->contain_varg && formalparam->argc > argc ||
      !formalparam->contain_varg && formalparam->argc != argc) {
    SLoc stloc = {glineno, gcolno};
    caerror(&param->begloc, &param->endloc,
            "actual parameter count `%d` not match formal parameter count `%d`",
            param->arglistn.argc, formalparam->argc);
    return -1;
  }

  return 0;
}

/**
 * @param id can be function name or tuple name, tuple is special
 */
ASTNode *make_fn_call_or_tuple(int id, ASTNode *param) {
  dot_emit("fn_call", symname_get(id));
  typeid_t fnname = sym_form_function_id(id);

  // tuple type cannot have the same name with function in the same symbol table
  return make_expr(FN_CALL, 2, make_id(fnname, TTEId_FnName), param);
}

ASTNode *make_method_call(StructFieldOp sfop, ASTNode *param) {
  ASTNode *fieldnode = make_structfield_right(sfop);

  // tuple type cannot have the same name with function in the same symbol table
  return make_expr(FN_CALL, 2, fieldnode, param);
}

DomainNames domain_init(int relative, int name) {
  void *parts = vec_new();
  vec_append(parts, (void *)(long)name);

  return (DomainNames){
      .relative = relative,
      .count = 1,
      .parts = parts,
  };
}

void domain_append(DomainNames *names, int name) {
  vec_append(names->parts, (void *)(long)name);
  names->count += 1;
}

DomainAs make_domain_as(DomainNames *type, DomainNames *as, int fnname) {
  DomainNames *domain_main = (DomainNames *)malloc(sizeof(DomainNames));
  DomainNames *domain_trait = (DomainNames *)malloc(sizeof(DomainNames));
  *domain_main = *type;
  *domain_trait = *as;

  return (DomainAs){
      .domain_main = domain_main,
      .domain_trait = domain_trait,
      .fnname = fnname,
  };
}

DomainFn make_domainfn_domain(DomainNames *domain_names) {
  DomainNames *domain = (DomainNames *)malloc(sizeof(DomainNames));
  *domain = *domain_names;
  return (DomainFn){
      .type = DFT_Domain,
      .u.domain = domain,
  };
}

DomainFn make_domainfn_domainas(DomainAs *domainas) {
  DomainAs *domain_as = (DomainAs *)malloc(sizeof(DomainAs));
  *domain_as = *domainas;
  return (DomainFn){
      .type = DFT_DomainAs,
      .u.domain_as = domain_as,
  };
}

ASTNode *make_domain(DomainFn *domain_fn) {
  ASTNode *p = new_ASTNode(TTE_Domain);
  p->domainfn = *domain_fn;

  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});
  return p;
}

ASTNode *make_domain_call(DomainFn *domain_fn, ASTNode *param) {
  ASTNode *domainnode = make_domain(domain_fn);
  return make_expr(FN_CALL, 2, domainnode, param);
}

ASTNode *make_gen_tuple_expr(ASTNode *param) {
  return make_expr(TUPLE, 1, param);
}

ASTNode *make_ident_expr(int id) {
  dot_emit("expr", "IDENT");

  STEntry *entry = sym_getsym(curr_symtable, id, 1);
  if (!entry) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "Variable name '%s' not defined", symname_get(id));
    return NULL;
  }

  ASTNode *node = make_id(id, TTEId_VarUse);
  node->entry = entry;
  return node;
}

ASTNode *make_uminus_expr(ASTNode *expr) {
  /*
   * Only U64 literal type can combine with '-' here. When littypetok is I64,
   * it means the literal has already been combined with '-', so there is no
   * need to combine it again. When littypetok is of other types, such as BOOL,
   * I8, U8, etc., they do not support combination with '-', so we will just
   * proceed with a UMINUS operator.
   */
  if (expr->type != TTE_Literal || expr->litn.litv.littypetok != U64)
    return make_expr(UMINUS, 1, expr);

  /*
   * Handle the combination of unary minus literals to address the lexer's
   * inability to process unary minus values correctly.
   */
  CALiteral *lit = &expr->litn.litv;
  const char *littext = symname_get(lit->textid);
  char buffer[1024] = "-";
  strcpy(buffer + 1, littext);

  lit->textid = symname_check_insert(buffer);
  lit->littypetok = I64;

  return expr;
}

/**
 * @brief Similar to the function 'add_fn_args'
 */
int add_struct_member(ST_ArgList *arglist, SymTable *st, CAVariable *var) {
  // TODO: combine this function with add_fn_args into one function
  int name = var->name;
  if (arglist->argc >= MAX_ARGS) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL,
            "too many struct members '%s', max member supports are %d",
            symname_get(name), MAX_ARGS);
    return -1;
  }

  STEntry *entry = sym_getsym(st, name, 0);
  if (entry) {
    caerror(&(var->loc), NULL,
            "member '%s' already defined on line %d, col %d.",
            symname_get(name), entry->sloc.row, entry->sloc.col);
    return -1;
  }

  entry = sym_insert(st, name, Sym_Member);
  entry->u.varshielding.current = cavar_create(name, var->datatype);
  arglist->argnames[arglist->argc++] = name;
  return 0;
}

int add_tuple_member(ST_ArgList *arglist, typeid_t tid) {
  if (arglist->argc >= MAX_ARGS) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL,
            "too many struct members '%d', max member supports are `%d`",
            arglist->argc, MAX_ARGS);
    return -1;
  }

  arglist->types[arglist->argc++] = tid;
  return 0;
}

void reset_arglist_with_new_symtable() {
  SymTable *st = push_new_symtable();
  curr_arglist.argc = 0;
  curr_arglist.contain_varg = 0;
  curr_arglist.symtable = curr_symtable;
}

static STEntry *check_function_name(int id) {
  // check tuple type
  typeid_t fnname = sym_form_function_id(id);
  STEntry *entry = sym_getsym(curr_symtable, fnname, 0);

  if (entry)
    return entry;

  return NULL;
}

ASTNode *make_struct_type(int id, ST_ArgList *arglist, int tuple) {
  dot_emit("struct_type_def", "IDENT");

  // see make_fn_proto
  arglist->symtable = curr_symtable;

  /*
   * Populate the structure member symbol table.
   * After that, the type name will be defined within it.
   */
  if (!tuple)
    curr_symtable = pop_symtable();

  /*
   * 0. Check if the current scope already contains such type and
   * report an error if it already exists.
   */
  const char *structname = symname_get(id);
  if (check_function_name(id)) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "tuple '%s' already defined as function in previous",
            structname);
    return NULL;
  }

  typeid_t structtype = sym_form_type_id(id);
  STEntry *entry = sym_getsym(curr_symtable, structtype, 0);
  if (entry) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "type '%s' already defined", symname_get(id));
    return NULL;
  }

  CADataType *primtype = catype_get_primitive_by_name(structtype);
  if (primtype) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "struct type id `%s` cannot be primitive type",
            symname_get(id));
    return NULL;
  }

  ASTNode *p = new_ASTNode(TTE_Struct);
  entry = sym_insert(curr_symtable, structtype, Sym_DataType);
  entry->u.datatype.tuple = tuple;
  entry->u.datatype.id = structtype;
  entry->u.datatype.idtable = curr_symtable;
  entry->u.datatype.runables.opaque = NULL;
  entry->u.datatype.members = (ST_ArgList *)malloc(sizeof(ST_ArgList));

  // just remember the argument list for later use
  *entry->u.datatype.members = *arglist;
  entry->sloc = (SLoc){glineno, gcolno};
  p->entry = entry;

  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &entry->sloc);
  return p;
}

ASTNode *make_as(ASTNode *expr, typeid_t type) {
  dot_emit("expr", "expr AS datatype");

  ASTNode *p = new_ASTNode(TTE_As);
  p->exprasn.type = type;
  p->exprasn.expr = expr;
  set_address(p, &(SLoc){glineno_prev, gcolno_prev}, &(SLoc){glineno, gcolno});

  ASTNode *node = make_expr(AS, 1, p);
  return node;
}

ASTNode *make_sizeof(typeid_t type) {
  ASTNode *p = make_id(type, TTEId_Type);
  ASTNode *node = make_expr(SIZEOF, 1, p);
  node->exprn.expr_type = sym_form_type_id_from_token(U64);
  return node;
}

typeid_t make_typeof(ASTNode *expr) { return sym_form_expr_typeof_id(expr); }

ASTNode *make_deref(ASTNode *expr) {
  ASTNode *node = make_expr(DEREF, 1, expr);
  return node;
}

ASTNode *make_address(ASTNode *expr) {
  ASTNode *node = make_expr(ADDRESS, 1, expr);
  return node;
}

int parse_tuple_fieldname(int fieldname) {
  const char *name = symname_get(fieldname);
  int len = strlen(name);
  int i = 0;
  for (; i < len; ++i) {
    if (name[i] < '0' || name[i] > '9')
      break;
  }

  if (i != len || (len > 1 && name[0] == '0')) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "unknown field name `%s`", name);
    return -1;
  }

  fieldname = atoi(name);
  return fieldname;
}

StructFieldOp make_element_field(ASTNode *expr, int fieldname, int direct,
                                 int tuple) {
#if 1
  int count = 0;
  if (tuple) {
    // FIXME: Patches for a list of tuples (aa.1.2.0)
    const char *name = symname_get(fieldname);
    int len = strlen(name);
    char *names = strdup(name);
    char *pname = names;
    int i = 0;
    for (; i < len; ++i) {
      if (names[i] == '.') {
        names[i] = '\0';

        int subname = symname_check_insert(pname);
        fieldname = parse_tuple_fieldname(subname);
        StructFieldOp sfop = {expr, fieldname, count ? 1 : direct, tuple};
        expr = make_structfield_right(sfop);

        count += 1;
        pname = names + i + 1;
      }
    }

    if (count)
      fieldname = symname_check_insert(pname);

    free(names);

    fieldname = parse_tuple_fieldname(fieldname);
  }
#else
  if (tuple)
    fieldname = parse_tuple_fieldname(fieldname);
#endif

  StructFieldOp sfop = {expr, fieldname, count ? 1 : direct, tuple};
  return sfop;
}

typedef struct ASTNodeList {
  ASTNode **stmtlist;
  int capacity;
  int len;
  struct ASTNodeList *next;
} ASTNodeList;

ASTNodeList *nodelisthead = NULL;
void put_astnode_into_list(ASTNode *stmt, int begin) {
  dot_emit("stmt_list", "stmt");

  if (begin) {
    // Push a new list handle here when the list is first encountered
    ASTNodeList *newlist = (ASTNodeList *)malloc(sizeof(ASTNodeList));
    newlist->len = 0;
    newlist->capacity = 10;
    newlist->stmtlist =
        (ASTNode **)malloc(sizeof(ASTNode *) * newlist->capacity);
    newlist->next = nodelisthead;
    nodelisthead = newlist;
  }

  // enhance capacity when needed
  if (nodelisthead->len == nodelisthead->capacity) {
    nodelisthead->capacity *= 2;
    nodelisthead->stmtlist = (ASTNode **)realloc(
        nodelisthead->stmtlist, sizeof(ASTNode *) * nodelisthead->capacity);
  }

  nodelisthead->stmtlist[nodelisthead->len++] = stmt;
}

ASTNode *make_stmt_list_zip() {
  dot_emit("stmt_list_star", "stmt_list_zip");

  // pop the previous list handle when reducing a handler
  ASTNodeList *oldlist = nodelisthead;
  nodelisthead = nodelisthead->next;

  if (oldlist->len == 1) {
    ASTNode *p = oldlist->stmtlist[0];
    free(oldlist->stmtlist);
    free(oldlist);
    return p;
  }

  int len = oldlist->len;
  ASTNode *p = new_ASTNode(TTE_StmtList);
  p->stmtlistn.nstmt = len;
  if ((p->stmtlistn.stmts = (ASTNode **)malloc(len * sizeof(ASTNode))) ==
      NULL) {
    SLoc stloc = {glineno, gcolno};
    caerror(&stloc, NULL, "out of memory");
    return NULL;
  }

  for (int i = 0; i < len; ++i)
    p->stmtlistn.stmts[i] = oldlist->stmtlist[i];

  free(oldlist->stmtlist);
  free(oldlist);

  const SLoc *beg = &(SLoc){glineno, gcolno};
  const SLoc *end = &(SLoc){glineno, gcolno};

  if (len == 1) {
    if (p->stmtlistn.stmts[0]) {
      beg = &p->stmtlistn.stmts[0]->begloc;
      end = &p->stmtlistn.stmts[0]->endloc;
    }
  } else if (len > 1) {
    if (p->stmtlistn.stmts[0]) {
      beg = &p->stmtlistn.stmts[0]->begloc;
    }

    if (p->stmtlistn.stmts[len - 1]) {
      end = &p->stmtlistn.stmts[len - 1]->endloc;
    }
  }

  p->begloc = *beg;
  p->endloc = *end;

  p->symtable = curr_symtable;

  return p;
}

/*
 * TODO: Change the name related to 'arrayitem' to a more appropriate name,
 * as 'arrayitem' can be used for more than just array types (e.g., slice
 * types or pointer types). The related names include:
 * ARRAYITEM, TTE_ArrayItemLeft, TTE_ArrayItemRight,
 * extract_value_from_array, etc.
 */
ArrayItem arrayitem_begin(ASTNode *expr) {
  void *handle = vec_new();
  vec_append(handle, expr);
  return (ArrayItem){NULL, handle};
}

ArrayItem arrayitem_append(ArrayItem ai, ASTNode *expr) {
  vec_append(ai.indices, expr);
  return ai;
}

ArrayItem arrayitem_end(ArrayItem ai, ASTNode *arraynode) {
  ai.arraynode = arraynode;
  vec_reverse(ai.indices);
  return ai;
}

CAStructExpr structexpr_new() {
  CAStructExpr o = {typeid_novalue, 0, vec_new()};
  return o;
}

CAStructExpr structexpr_append(CAStructExpr sexpr, ASTNode *expr) {
  vec_append(sexpr.data, expr);
  return sexpr;
}

CAStructExpr structexpr_append_named(CAStructExpr sexpr, ASTNode *expr,
                                     int name) {
  CAStructNamed *s = (CAStructNamed *)malloc(sizeof(CAStructNamed));
  s->expr = expr;
  s->name = name;
  vec_append(sexpr.data, s);
  return sexpr;
}

CAStructExpr structexpr_end(CAStructExpr sexpr, int name, int named) {
  typeid_t structtype = sym_form_type_id(name);
  sexpr.name = structtype;
  sexpr.named = named;
  return sexpr;
}

TypeImplInfo begin_impl_type(int class_id) {
  return (TypeImplInfo){.class_id = class_id,
                        .trait_id = -1,
                        .trait_impl_id = typeid_novalue,
                        .fn_def_recursive_count = 0};
}

TypeImplInfo begin_impl_trait_for_type(int class_id, int trait_id) {
  const char *prefix = get_method_impl_prefix(class_id, trait_id);
  typeid_t trait_impl_id = sym_form_trait_impl_by_str(prefix);

  STEntry *entry = sym_getsym(curr_symtable, trait_impl_id, 0);
  if (entry) {
    SLoc stloc = {glineno, gcolno};
    caerror_noexit(&stloc, NULL,
                   "conflicting implementations of trait `%s` for type `%s`, "
                   "already implemented"
                   "\n\npreviously defined here:",
                   catype_get_type_name(class_id),
                   catype_get_type_name(trait_id));
    caerror(&entry->sloc, NULL, "already implemented here");
  }

  TypeImplInfo type_impl = {.class_id = class_id,
                            .trait_id = trait_id,
                            .trait_impl_id = trait_impl_id,
                            .fn_def_recursive_count = 0};

  entry = sym_insert(curr_symtable, type_impl.trait_impl_id, Sym_TraitImpl);
  entry->sloc = (SLoc){glineno, gcolno};
  entry->u.trait_impl.impl_info = type_impl;

  return type_impl;
}

/**
 * @brief Push the current type implementation info and copy
 *        the new implementation info into the current.
 */
void push_type_impl(TypeImplInfo *new_info) {
  if (!type_impl_stack)
    type_impl_stack = vec_new();

  vec_append(type_impl_stack, current_type_impl);
  current_type_impl = (TypeImplInfo *)malloc(sizeof(TypeImplInfo));
  *current_type_impl = *new_info;
}

/*
 * @brief Pop the saved type implementation info and save it into current
 */
void pop_type_impl() {
  free(current_type_impl);
  current_type_impl = vec_popback(type_impl_stack);
}

// void push_lexical_body() {}
// void pop_lexical_body() {}

void freeNode(ASTNode *p) {
  int i;
  if (!p)
    return;
  if (p->type == TTE_Expr) {
    for (i = 0; i < p->exprn.noperand; i++)
      freeNode(p->exprn.operands[i]);
    free(p->exprn.operands);
  }
  free(p);
}

NodeChain *node_chain(RootTree *tree, ASTNode *p) {
  static int is_main_start_set = 0;
  switch (p->type) {
  case TTE_Literal:
  case TTE_LabelGoto:
  case TTE_As:
  case TTE_Expr:
    if (!is_main_start_set) {
      gtree->begloc_main = p->begloc;
      is_main_start_set = 1;
    }
    gtree->endloc_main = p->endloc;
  default:
    gtree->endloc_prog = p->endloc;
    break;
  }

  NodeChain *node = (NodeChain *)calloc(1, sizeof(NodeChain));
  node->node = p;

  if (!tree->head) {
    tree->head = tree->tail = node;
    tree->count = 1;
    return node;
  }

  tree->tail->next = node;
  tree->tail = node;
  tree->count++;
  return node;
}

void yyerror(const char *s, ...) {
  fprintf(stderr, "[grammar line: %d, token: %d] ", yylineno, yychar);

  va_list ap;
  va_start(ap, s);
  int n = vfprintf(stderr, s, ap);
  va_end(ap);

  fprintf(stderr, "\n");
  exit(-1);
}

void caerror_source_code(const SLoc *beg, const SLoc *end) {
  fprintf(stderr, "[grammar line: %d, token: %d] ", yylineno, yychar);
  int linefrom = -1;
  int lineto = -1;
  int firstcol = -1;
  int lastcol = -1;
  if (beg) {
    fprintf(stderr, "line: %d, col: %d:\n", beg->row, beg->col);
    linefrom = beg->row;
    firstcol = beg->col;

    lineto = beg->row;
    lastcol = beg->col;
  }

  if (end) {
    lineto = end->row;
    lastcol = end->col;
  }

  if (firstcol < 1)
    firstcol = 1;

  if (lastcol < 1)
    lastcol = 1;

  const char *sourceline = source_lines(linefrom, lineto);
  fprintf(stderr, "%s", sourceline);

  char *buffer = source_buffer();
  memset(buffer, ' ', firstcol);
  buffer[firstcol - 1] = '^';
  buffer[firstcol] = 0;
  fprintf(stderr, "%s\n", buffer);
}

void caerror_noexit(const SLoc *beg, const SLoc *end, const char *s, ...) {
  caerror_source_code(beg, end);

  va_list ap;
  va_start(ap, s);
  int n = vfprintf(stderr, s, ap);
  va_end(ap);

  fprintf(stderr, "\n");
}

void caerror(const SLoc *beg, const SLoc *end, const char *s, ...) {
  caerror_source_code(beg, end);

  va_list ap;
  va_start(ap, s);
  int n = vfprintf(stderr, s, ap);
  va_end(ap);

  fprintf(stderr, "\n");
  exit(-1);
}

int yyparser_init() {
  gtree = (RootTree *)calloc(1, sizeof(RootTree));
  if (!gtree) {
    yyerror("init root tree failed\n");
  }

  symname_init();
  lexical_init();
  catype_init();
  dot_init();
  source_info_init(genv.src_path);

  if (sym_init(&g_root_symtable, NULL)) {
    yyerror("init symbol table failed\n");
  }

  curr_symtable = &g_root_symtable;
  curr_fn_symtable = NULL;
  if (enable_emit_main()) {
    main_fn_node = build_mock_main_fn_node();
    g_main_symtable = push_new_symtable();
    curr_fn_symtable = g_main_symtable;
  }

  return 0;
}

