#ifndef __symtable_h__
#define __symtable_h__

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
BEGIN_EXTERN_C
#endif

#define MAX_SYMS 1024

/* source code location */
typedef struct SLoc {
  int row; /* row or lineno */
  int col; /* column */
} SLoc;

typedef enum SymType {
  Sym_Variable,
  Sym_Label,
  Sym_Label_Hanging,
  Sym_ArgList,
  Sym_FnDecl,
  Sym_FnDef,
} SymType;

#define MAX_ARGS 16

typedef struct CADataType {
  int type;       // type type: I32 I64 ... STRUCT ARRAY
  int formalname; // type name symname_xxx
  int size;       // type size
  int signature;  // the signature of the type, which is used to avoid store multiple instance, it used in the symbol table
  union {
    struct CAStruct *struct_layout;  // when type is STRUCT
    struct CAArray *array_layout;    // when type is ARRAY
    struct CAPointer *pointer_layout;
  };
} CADataType;

struct CAStructField {
  int name;           // field name
  CADataType *type;   // field type
};

struct CAStruct {
  int fieldnum;
  struct CAStructField *fields;
};

// the c language treat mutiple dimension array or typedef-ed array as the same
// array, e.g. typedef int aint[3][4]; typedef aint aaint[3]; aaint ca[6];
// (gdb) ptype ca:
// type = int [6][3][3][4]
//
// for pointer it is similar with array:
// typedef int *pint; typedef pint *ppint; typedef ppint *pppint; pppint *a;
// (gdb) ptype a
// type = int ****
//
// for pointer plus array type: aaint *pca[4]; aaint *pc;
// (gdb) ptype pca
// type = int (*[4])[3][3][4]
// (gdb) ptype pc
// type = int (*)[3][3][4]
// the dimension may need compact, ((int [3])[4])[5], after compact, int
// [3][4][5] complex condition combine array and pointer:
// typedef pppint apppint[5]; typedef apppint *papppint; papppint *ppap;
// (gdb) ptype ppap
// type = int ***(**)[5]
// 
typedef struct CAArray {
  CADataType *type;   // array type
  int dimension;      // array size
  int *dimarray;      // dimension array 3, 5, 9
} CAArray;

// the dimension need compact when constructing type, because
// e.g. ((int *) *) *a; after compact, it should be int ***a;
typedef struct CAPointer {
  CADataType *type;
  int dimension;
} CAPointer;

typedef struct CALiteral {
  // specify if literal type is defined (fixed) with postfix (u32,f64, ...)
  // indicate if the literal type is determined, should not combine with
  // postfixtypetok, because when it have no postfix but it's neibough have an
  // postfix, then it will use the neighbour's type, but the postfixtypetok
  // field always be 0
  // if need remove this field, just use datatype null or not null for the existance
  int fixed_type;

  // when fixed_type is false, it stand for the type of the default literal value
  // when have no any context
  int intent_type;

  // the literal I64 for negative integer value, U64 for positive integer value,
  // F64 for floating point value, BOOL is true false value, CHAR is 'x' value,
  // UCHAR is '\x' value
  int littypetok;

  // the just being creating variable's type, used to guide the literal type
  int borning_var_type;

  int postfixtypetok;      // the postfix type when postfix is set

  // text id in symname table, text is used for latering literal type inference
  int textid;

  // when the literal type already determined then datatype is not NULL 
  CADataType *datatype;
  union {
    int64_t  i64value;      // store either integer type value include unsigned
    double   f64value;      // store floating value
    void    *structvalue;
    void    *arrayvalue;
  } u;
} CALiteral;

typedef struct LitBuffer {
  int typetok;
  int len;
  int text;
} LitBuffer;

typedef struct IdToken {
  int symnameid;
  int typetok;
} IdToken;

typedef struct CAVariable {
  CADataType *datatype;
  int name;
  int global; // is global variable

  // opaque memory, for storing llvm Value * type, used here for code generation, in code generation it will be filled and used, when type is Variable
  void *llvm_value;
} CAVariable;

typedef struct ST_ArgList {
  int argc;                 // function argument count
  int contain_varg;         // contain variable argument
  int argnames[MAX_ARGS];   // function argument name
  struct SymTable *symtable;
} ST_ArgList;

typedef enum ArgType {
  AT_Literal,
  AT_Variable,
  AT_Expr,
} ArgType;

typedef struct ActualArg {
  // TODO: later remove all but only use ASTNode
  //ArgType type;
  struct ASTNode *exprn; /* for all the expression */
} ActualArg;

typedef struct ST_ArgListActual {
  int argc;                 // function argument count
  struct ASTNode *args[MAX_ARGS];  // function argument name
} ST_ArgListActual;

// for the labels the symbol name will append a prefix of 'l:' which is
// impossible to be as a variable name. example: l:l1
// for function it will append a prefix of 'f:'. example: f:fibs
typedef struct STEntry {
  int sym_name;		// symbol name index in global symbol table
  SymType sym_type;	// symbol type
  SLoc sloc;		// symbol definition line number and column
  union {
    struct {
      ST_ArgList *arglists; // when type is Sym_ArgList
      CADataType *rettype;
    } f;
    CAVariable *var;
  } u;
} STEntry;

typedef struct SymTable {
  void *opaque;
  struct SymTable *parent;
} SymTable;

// parameter handling
// because function call can use embed form:
// let a = func1(1 + func2(2 + func3(3, 4))), so the actual argument list should
// use a stack struct for handling, just using function defined in symtable.h

// get current actual argument list object
ST_ArgListActual *actualarglist_current();

// create new actual argument list object and push into layer
ST_ArgListActual *actualarglist_new_push();

// popup a actual argument list object and destroy it
void actualarglist_pop();

// type checking
int check_i64_value_scope(int64_t lit, int typetok);
int check_u64_value_scope(uint64_t lit, int typetok);
int check_f64_value_scope(double lit, int typetok);
int check_char_value_scope(char lit, int typetok);
int check_uchar_value_scope(uint8_t lit, int typetok);
int literal_type_convertable(int from, int to);
int as_type_convertable(int from, int to);

// type finding
int catype_init();
int catype_put_by_name(int name, CADataType *datatype);
CADataType *catype_get_by_name(int name);
int catype_put_by_token(int token, CADataType *datatype);
CADataType *catype_get_by_token(int token);
int catype_is_float(int typetok);

const char *get_type_string(int tok);
void set_litbuf(LitBuffer *litb, char *text, int len, int typetok);

CAVariable *cavar_create(int name, CADataType *datatype);
void cavar_destroy(CAVariable **var);

// the globally symbol name table, it store names and it's index
int symname_init();
int symname_insert(const char *name);
int symname_check(const char *name);
int symname_check_insert(const char *name);
const char *symname_get(int pos);

int sym_init(SymTable *t, SymTable *parent);
STEntry *sym_check_insert(SymTable *st, int encode, SymType type);
int sym_check_insert_withname(SymTable *t, const char *name, SymType type);

// insert without check
STEntry *sym_insert(SymTable *st, int encode, SymType type);
int sym_dump(SymTable *st, FILE *file);

// parent: if search parent symtable
STEntry *sym_getsym(SymTable *st, int idx, int parent);
int sym_tablelen(SymTable *t);
SymType sym_gettype(SymTable *t, int idx, int parent);
SLoc sym_getsloc(SymTable *t, int idx, int parent);
void sym_setsloc(SymTable *st, int idx, SLoc loc);

SymTable *load_symtable(char *buf, int len);
void sym_destroy(SymTable *t);

int lexical_init();
int find_lexical_keyword(const char *name);

#ifdef __cplusplus
END_EXTERN_C
#endif

#endif

