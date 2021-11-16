#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include <utility>
#include <vector>
#include "ca.h"

#include "type_system.h"
#include "type_system_llvm.h"

#ifdef __cplusplus
BEGIN_EXTERN_C
#endif

#include "ca.tab.h"
#include "config.h"
#include "symtable.h"
#include "dotgraph.h"

void yyerror(const char *s, ...);
int yyparse(void);
int yyparser_init();

CompileEnv genv;

extern int glineno;
extern int gcolno;

#ifdef __cplusplus
END_EXTERN_C
#endif

extern FILE *yyin;

// llvm section
#include "ir1.h"
#include "jit1.h"
#include "dwarf_debug.h"
#include "IR_generator.h"

using namespace llvm;
ir_codegen::IR1 ir1;
std::unique_ptr<dwarf_debug::DWARFDebugInfo> diinfo;
std::unique_ptr<jit_codegen::JIT1> jit1;

static ExitOnError exit_on_error;
static bool g_with_ret_value = false;

// llvm section

// note: rust not support different type variable to do
// calculation, so add `as` to convert between type
// 1. mathimatic calculation must need the same type or error
// 2. the literal type have 2 forms:
//   a. with postfix to indicate it's type,
//   b. without postfix, just consider it's value when do calculation. example
//   432432 means integer value (any integer value), -43433 mean an Neg bound to
//   43433, 33.33 mean f64 value (default is f64 not f32 value)
// 
//   
// 
// So I need to seperate this two condition
// 

#define MAX_OPS 10000

#include <stack>

struct FnDebugInfo {
  DISubprogram *disp;
};

static llvm::Function *main_fn = nullptr;

// handle when processing current function, the top level function is main function
static llvm::Function *curr_fn = nullptr;
static ASTNode *curr_fn_node = nullptr;
static llvm::BasicBlock *main_bb = nullptr;
extern ASTNode *main_fn_node;
static llvm::DIFile *diunit = nullptr;
static std::vector<std::unique_ptr<CalcOperand>> oprand_stack;

// for storing defined BasicBlock, or pre-define BasicBlock in GOTO statement
static std::map<std::string, BasicBlock *> label_map;
static std::map<std::string, ASTNode *> function_map;

// TODO: should here using a current debug info instead of the map, it no need
// to use a map here, because it only used when function define, just like
// curr_fn
static std::map<Function *, std::unique_ptr<FnDebugInfo>> fn_debug_map;

static int walk_stack(ASTNode *p);
extern RootTree *gtree;

static std::unique_ptr<CalcOperand> pop_right_operand(const char *name = "load") {
  std::unique_ptr<CalcOperand> o = std::move(oprand_stack.back());
  oprand_stack.pop_back();

  Value *v;
  if (o->type == OT_Alloc) {
    v = ir1.builder().CreateLoad(o->operand, name);
    o->type = OT_Load;
    o->operand = v;
  }

  return std::move(o);
}

static std::pair<Value *, int> pop_right_value(const char *name = "load") {
  std::unique_ptr<CalcOperand> o = std::move(oprand_stack.back());
  oprand_stack.pop_back();

  Value *v;
  if (o->type == OT_Alloc) {
    v = ir1.builder().CreateLoad(o->operand, name);
  } else {
    v = o->operand;
  }

  return std::make_pair(v, o->datatypetok);
}

static int enable_debug_info() { return genv.emit_debug; }

static void init_fn_param_info(Function *fn, ST_ArgList &arglist, SymTable *st, int startrow) {
  int row = startrow;
  int scope_row = row;
  DISubprogram *disp = nullptr;

  if (enable_debug_info()) {
    DIScope *scope = diunit;
    llvm::DISubroutineType *fnty = diinfo->fn_type_di(fn->arg_size(), diunit);

    disp = diinfo->dibuilder->createFunction(scope, fn->getName(), StringRef(),
					     diunit, row, fnty, scope_row,
					     DINode::FlagPrototyped,
					     DISubprogram::SPFlagDefinition);
    fn->setSubprogram(disp);

    // push current scope
    diinfo->lexical_blocks.push_back(disp);

    // Unset the location for the prologue emission (leading instructions with no
    // location in a function are considered part of the prologue and the debugger
    // will run past them when breaking on a function)
    diinfo->emit_location(-1);

    auto dbginfo = std::make_unique<FnDebugInfo>();
    dbginfo->disp = disp;
    fn_debug_map.insert(std::make_pair(fn, std::move(dbginfo)));
  }

  int i = 0;
  for (auto &arg: fn->args()) {
    int argn = arglist.argnames[i];
    STEntry *entry = sym_getsym(st, argn, 0);

    /* should equal to arg.getName().str().c_str() */
    const char *name = symname_get(argn);

    // get the argument from call parameter into this field
    if (entry->sym_type != Sym_Variable)
      yyerror("symbol type is not variable: (%d != %d)", entry->sym_type, Sym_Variable);

    Type *type = gen_type_from_token(entry->u.var->datatype->type);
    AllocaInst *slot = ir1.gen_var(type, name, &arg);

    if (enable_debug_info()) {
      DIType *ditype = diinfo->get_ditype(get_type_string(entry->u.var->datatype->type));
      DILocalVariable *divar = diinfo->dibuilder->createParameterVariable(disp, arg.getName(), i, diunit, row, ditype, true);

      const DILocation *diloc = DILocation::get(disp->getContext(), row, 0, disp);
      diinfo->dibuilder->insertDeclare(slot, divar, diinfo->dibuilder->createExpression(),
				       diloc, ir1.builder().GetInsertBlock());
    }

    // save the value into symbol table
    entry->u.var->llvm_value = static_cast<void *>(slot);

    ++i;
  }
}

static void emit_global_var_dbginfo(const char *varname, const char *type, int row) {
  DIType *ditype = diinfo->get_ditype(type);
  llvm::DIGlobalVariableExpression *digve =
    diinfo->dibuilder->createGlobalVariableExpression(diunit, varname, StringRef(), diunit, row, ditype, false);
}

static void emit_local_var_dbginfo(llvm::Function *fn, const char *varname,
				   const char *typestr, llvm::Value *var, int row) {
  auto itr = fn_debug_map.find(fn);
  if (itr == fn_debug_map.end())
    yyerror("cannot find function '%s' in map", fn->getName().str().c_str());

  auto &dbginfo = *itr->second;
  DIType *ditype = diinfo->get_ditype(typestr);
  DILocalVariable *divar =
    diinfo->dibuilder->createAutoVariable(dbginfo.disp, varname, diunit, row, ditype, true);

  const DILocation *diloc = DILocation::get(dbginfo.disp->getContext(), row, 0, dbginfo.disp);
  diinfo->dibuilder->insertDeclare(var, divar, diinfo->dibuilder->createExpression(),
				   diloc, ir1.builder().GetInsertBlock());
}

static void walk_empty(ASTNode *p) {}

static Value *walk_literal(ASTNode *p) {
  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  // the intent_type is for determining the binding type of literal value
  // when fixed_type is set use it else use intent_type
  int typetok = 0;
  if (p->litn.litv.fixed_type)
    typetok = p->litn.litv.datatype->type;
  else {
    yyerror("line: %d, col: %d: should never used the intent type",
	    p->begloc.col, p->begloc.row);
    typetok = p->litn.litv.intent_type;
  }

  Value *v = gen_literal_value(&p->litn.litv, typetok, p->begloc);

  auto operands = std::make_unique<CalcOperand>(OT_Const, v, typetok);
  oprand_stack.push_back(std::move(operands));

  return v;
}

static Value *walk_typed_literal(ASTNode *p, CADataType *datatype) {
  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  Value *v = gen_literal_value(&p->litn.litv, datatype->type, p->begloc);
  return v;
}

// generate variable, if in a function then it is a local variable, when not in
// a function but `-nomain` is specified then generate a global variable else
// also generate a global variable for other use
static Value *walk_id_defv(ASTNode *p, Value *defval = nullptr) {
  Value *var;
  const char *name = symname_get(p->idn.i);
  //STEntry *entry = sym_getsym(p->symtable, p->idn.i, 1);
  STEntry *entry = p->entry;
  if (entry->sym_type != Sym_Variable)
    yyerror("line: %d, col: %d: '%s' Not a variable", entry->sloc.col, entry->sloc.row, name);

  if (entry->u.var->llvm_value) {
    var = static_cast<Value *>(entry->u.var->llvm_value);
    if (defval)
      ir1.builder().CreateStore(defval, var, name);
  } else {
    // if nomain specified then curr_fn and main_fn are all nullptr, so they are also equal
    Type *type = gen_type_from_token(p->entry->u.var->datatype->type);
    const char *typestr = get_type_string(p->entry->u.var->datatype->type);

    // here determine if `#[scope(global)]` is specified
    if (curr_fn == main_fn && (!main_fn || entry->u.var->global)) {
      var = ir1.gen_global_var(type, name, defval);
      if (enable_debug_info())
	emit_global_var_dbginfo(name, typestr, p->endloc.row);
    } else {
      var = ir1.gen_var(type, name, defval);
      if (enable_debug_info())
	emit_local_var_dbginfo(curr_fn, name, typestr, var, p->endloc.row);
    }
    entry->u.var->llvm_value = static_cast<void *>(var);
  }

  return var;
}

static Value *walk_id(ASTNode *p) {
  Value *var = walk_id_defv(p);
  
  auto operands = std::make_unique<CalcOperand>(OT_Alloc, var, p->entry->u.var->datatype->type);
  oprand_stack.push_back(std::move(operands));
  return var;
}

static BasicBlock *walk_label(ASTNode *p) {
  if (!curr_fn)
    return nullptr;

  BasicBlock *bb;
  const char *label_name = symname_get(p->idn.i) + 2;
  auto itr = label_map.find(label_name);
  if (itr != label_map.end()) {
    bb = itr->second;
  } else {
    bb = ir1.gen_bb(label_name);
    label_map.insert(std::make_pair(label_name, bb));
  }

  if (enable_debug_info()) {
    diinfo->emit_location(p->endloc.row, p->endloc.col);

    auto itr = fn_debug_map.find(curr_fn);
    if (itr == fn_debug_map.end())
      yyerror("line: %d, col: %d: cannot find function '%s' in map",
	      p->begloc.row, p->begloc.col, curr_fn->getName().str().c_str());

    auto &dbginfo = *itr->second;
    DILabel *dilabel = diinfo->dibuilder->createLabel(dbginfo.disp, label_name, diunit, p->endloc.row);

    const DILocation *diloc = DILocation::get(dbginfo.disp->getContext(), p->endloc.row, 0, dbginfo.disp);
    diinfo->dibuilder->insertLabel(dilabel, diloc, bb);
  }

  ir1.builder().CreateBr(bb);
  curr_fn->getBasicBlockList().push_back(bb);
  ir1.builder().SetInsertPoint(bb);
  return bb;
}

static void walk_label_goto(ASTNode *p) {}

static void walk_stmt_semicolon(ASTNode *p) {
  walk_stack(p->exprn.operands[0]);
  walk_stack(p->exprn.operands[1]);
}

static void walk_while(ASTNode *p) {
  if (!curr_fn)
    return;

  BasicBlock *condbb = ir1.gen_bb("condbb");
  BasicBlock *whilebb = ir1.gen_bb("whilebb");
  BasicBlock *endwhilebb = ir1.gen_bb("endwhilebb");

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  ir1.builder().CreateBr(condbb);
  curr_fn->getBasicBlockList().push_back(condbb);
  ir1.builder().SetInsertPoint(condbb);
  walk_stack(p->whilen.cond);
  auto pair = pop_right_value("cond");
  Value *cond = pair.first;
  if (pair.second != BOOL) {
    // when grammar also support other type compare, here should convert the other
    // type into bool type, like following, but need generate compare with right
    // type not hardcoded `int` type
    //cond = ir1.builder().CreateICmpNE(cond, ir1.gen_int(0), "if_cond_cmp");
    yyerror("line: %d, col: %d: condition only accept `bool` type, but find `%s`",
	    p->begloc.row, p->begloc.col, get_type_string(pair.second));
    return;
  }

  ir1.builder().CreateCondBr(cond, whilebb, endwhilebb);

  curr_fn->getBasicBlockList().push_back(whilebb);
  ir1.builder().SetInsertPoint(whilebb);

  walk_stack(p->whilen.body);
  // TODO: how to remove the stack element that never used?
  ir1.builder().CreateBr(condbb);

  curr_fn->getBasicBlockList().push_back(endwhilebb);
  ir1.builder().SetInsertPoint(endwhilebb);
}

static void walk_if(ASTNode *p) {
  if (!curr_fn)
    return;

  if (enable_debug_info())
    diinfo->emit_location(p->begloc.row, p->begloc.col);

  Value *tmpv1 = nullptr;
  Value *tmpv2 = nullptr;
  // TODO: the clang always alloca in the header of the function, it may error
  // occurs when alloca in other blocks, how to do that?
  Value *tmpc = nullptr; // for storing if expression temporary value
  if (p->ifn.isexpr)
    tmpc = ir1.gen_var(ir1.int_type<int>(), "tmpc");

  BasicBlock *thenbb = ir1.gen_bb("thenbb");
  BasicBlock *outbb = ir1.gen_bb("outbb");
  BasicBlock *elsebb = nullptr;

  walk_stack(p->ifn.conds[0]);
  auto pair = pop_right_value("cond");
  Value *cond = pair.first;
  if (pair.second != BOOL) {
    // when grammar also support other type compare, here should convert the other
    // type into bool type, like following, but need generate compare with right
    // type not hardcoded `int` type
    //cond = ir1.builder().CreateICmpNE(cond, ir1.gen_int(0), "if_cond_cmp");
    yyerror("line: %d, col: %d: condition only accept `bool` type, but find `%s`",
	    p->begloc.row, p->begloc.col, get_type_string(pair.second));
    return;
  }

  int tt1 = 0, tt2 = 0;

  if (p->ifn.remain) { /* if else */
    elsebb = ir1.gen_bb("elsebb");
    ir1.builder().CreateCondBr(cond, thenbb, elsebb);
    curr_fn->getBasicBlockList().push_back(thenbb);
    ir1.builder().SetInsertPoint(thenbb);
    walk_stack(p->ifn.bodies[0]);
    if (p->ifn.isexpr) {
      auto tmpv1 = pop_right_value("tmpv");
      ir1.store_var(tmpc, tmpv1.first);
      tt1 = tmpv1.second;
    }

    ir1.builder().CreateBr(outbb);

    curr_fn->getBasicBlockList().push_back(elsebb);
    ir1.builder().SetInsertPoint(elsebb);
    walk_stack(p->ifn.remain);
    if (p->ifn.isexpr) {
      auto tmpv2 = pop_right_value("tmpv");
      ir1.store_var(tmpc, tmpv2.first);
      tt2 = tmpv2.second;
    }
  } else { /* if */
    ir1.builder().CreateCondBr(cond, thenbb, outbb);
    curr_fn->getBasicBlockList().push_back(thenbb);
    ir1.builder().SetInsertPoint(thenbb);
    walk_stack(p->ifn.bodies[0]);
  }

  ir1.builder().CreateBr(outbb);
  curr_fn->getBasicBlockList().push_back(outbb);
  ir1.builder().SetInsertPoint(outbb);
  
  if (p->ifn.isexpr) {
    if (tt1 != tt2) {
      yyerror("expression type not equal in if ... else ... expression");
      return;
    }

#if 0 // the phi is not debugger friendly, so using alloc for temporary variable
    PHINode *phiv = ir1.gen_phi(ir1.int_type<int>(), thenbb, tmpv1, elsebb, tmpv2);
    auto pnv = std::make_unique<CalcOperand>(OT_PHINode, phiv, tt1);
#else
    auto pnv = std::make_unique<CalcOperand>(OT_Alloc, tmpc, tt1);
#endif

    oprand_stack.push_back(std::move(pnv));
  }
}

static void walk_stmt_print(ASTNode *p) {
  walk_stack(p->exprn.operands[0]);
  auto pair = pop_right_value();
  Value *v = pair.first;
  Function *printf_fn = ir1.module().getFunction("printf");
  if (!printf_fn)
    yyerror("cannot find declared extern printf function");

  const char *format = "%d\n";

  // handle expression value transfer
  format = get_printf_format(pair.second);
  v = tidy_value_with_arith(v, pair.second);

  Constant *format_str = ir1.builder().CreateGlobalStringPtr(format);
  std::vector<Value *> printf_args(1, format_str);
  printf_args.push_back(v);

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  ir1.builder().CreateCall(printf_fn, printf_args, "n");
}

// How to inference the type of variable and determine the literal type in right
// expression?
// 1. when variable type already determined, the variable type will used to
// guide and check the right side type. it requires the right expression have
// the same type as the variable.
//   when the right side expression is a final (left) expression (id or literal)
//   1) When the right side type already determined then check if it match the
//   variable type, when not match then report an error.
//   2) When the right side type not determined, then try to determinate the
//   right side type as the same type of the variable. when cannot make the same
//   type then error occurs.
//   when the right side is a complex expression, then recursive the procedure
//   of following
//   1) the complex expression finally should have the same type as the variable
//   2) each children expression of the complex expression should have the same
//   type as the variable, then recursive each children expression for the type
//   until to leaf and when arrive to leaf it is the procedure of above.
//
// 2. when variable type not determined, the variable's type need inferenced
// according to the right expression, it will use the right side expression's
// type as it's type.
//   when the right side is a final expression (id or literal) then use the id
//   or the literal's type as the variable's type.
//   1) when the literal is not determined yet then use the intent_type of the
//   literal type as both the literal's type and the variable's type. It need
//   check if the intent_type matches the literal value, when not match then
//   report an error.
//   2) when the literal type is determined (with a postfix type), then check if
//   the literal value matches the determined type.
//
//   when the right side is a complex expression, then all the children node of
//   it should have or inferenced into the same type, when they have different
//   type then report error. when all have the same type then the variable type
//   uses it as it's type.
//   1) for each children of the right side expression, do determinate their's
//   type recursively (because the children may also have it's children, so it's
//   recursive), the recursive finally case is the above condition (final
//   expression).
//   TODO: how to spread one determined type in the deep tree structure
//   e.g. let a = (3243 + (432432 + (3432 * 43243 + 43i64 * (433 + 232)))) +
//   333;
//               +
//         +        333
//   3243        +
//        432432     +
//                *             *
//           3432   43243 43i64    +
//                             433   232
//
//   the 43i64 will make all the other part have the same type with it, the
//   32i64 is deeper and in different layers, how to determined it?
//
// Generally, the type of left and right side have following regular:
// 1) when both side have not determined a type then, the right side will use
// the common literal regular to determine the expression's type, and the left
// variable use the right side's type
// 2) when both side have a determined type then they should have the same type,
// or report an error.
// 3) when left side determined a type, right side not determined a type, then
// the left side will guide the right side's type and will check if the literal
// matches the left side type
// 4) when left side not determined a type and the right side determined a type
// then the left side will use the right side's type
// 5) when the right side is complexed expression, then all the part of the
// expression should have the same type. when some part not have a determined
// type then it will use the type in the same expression. 
//

// consider following 4 condition, for an assigment:
// 1) left have a type, right have a type, then check if they are the same
// 2) left have a type, right no type, then invoke `determine_literal_type`
// function uses left's type as the expression's type
// 3) left no type, right have a type, then use the right side type as the left
// (variable)'s type
// 4) both have no type, then inference the right side expression type with
// default one and apply it into the left side variable

// TODO: check the walk_stack procedure for right side expression
static void walk_stmt_assign(ASTNode *p) {
  ASTNode *idn = p->exprn.operands[0];
  ASTNode *exprn = p->exprn.operands[1];

#if 0
  int exprntypetok = get_expr_type_from_tree(exprn, 0);
  if (idn->entry->u.var->datatype) {
    // when the variable has a specified type: `let a: i32 = ?`
    if (exprntypetok) {
      // both side have specified type: `let a: i32 = (33 + (33i32 + 4323))`
      if (idn->entry->u.var->datatype->type != exprntypetok) {
	yyerror("line: %d, column: %d, expected `%s`, found `%s`",
		p->begloc.row, p->begloc.col,
		get_type_string(idn->entry->u.var->datatype->type),
		get_type_string(exprntypetok));
	return;
      }

    } else {
      // right side have no determined a type: `let a: i32 = 4343`
      determine_expr_type(exprn, idn->entry->u.var->datatype->type);
    }
  } else {
    // when variable type not determined yet, it means:
    // 1) the variable is in definition stage, not just assigment 
    // 2) the variable type is not specified by identifier itself
    // it should inferenced by the right value: right expression
    if (!exprntypetok) {
      // when both side have no a determined type, then determine the right side type
      exprntypetok = inference_expr_type(exprn);
    }

    if (!exprntypetok) {
      yyerror("line: %d, column: %d, inference literal type failed",
	      p->begloc.row, p->begloc.col);
      return;
    }

    // when right side have a determined type
    const char *namestr = get_type_string(exprntypetok);
    int name = symname_check(namestr);
    if (name == -1) {
      yyerror("line: %d, column: %d, cannot find name for '%s'",
	      p->begloc.row, p->begloc.col, namestr);
      return;
    }

    idn->entry->u.var->datatype = catype_get_by_name(name);
  }
#endif

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  walk_stack(exprn);
  auto pair = pop_right_value("tmpexpr");
  Value *v = pair.first;
 
  Value *vp = walk_id_defv(idn, v);

  auto u = std::make_unique<CalcOperand>(OT_Store, vp, idn->entry->u.var->datatype->type);
  oprand_stack.push_back(std::move(u));
}

static void walk_stmt_minus(ASTNode *p) {
  int type = get_expr_type_from_tree(p->exprn.operands[0], 0);
  if (is_unsigned_type(type)) {
    yyerror("unsigned type `%s` cannot apply `-` operator", get_type_string(type));
    return;
  }

  walk_stack(p->exprn.operands[0]);

  auto pair = pop_right_value();
  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);
  Value *v = ir1.gen_sub(ir1.gen_int(0), pair.first);
  oprand_stack.push_back(std::make_unique<CalcOperand>(OT_Variable, v, type));
}

static void walk_stmt_goto(ASTNode *p) {
  ASTNode *label = p->exprn.operands[0];
  int i = label->idn.i;
  BasicBlock *bb;
  const char *label_name = symname_get(i) + 2;
  auto itr = label_map.find(label_name);
  if (itr != label_map.end()) {
    bb = itr->second;
  } else {
    bb = ir1.gen_bb(label_name);
    label_map.insert(std::make_pair(label_name, bb));
  }

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);
  ir1.builder().CreateBr(bb);

  // to avoid verify error of 'Terminator found in the middle of a basic block!'
  BasicBlock *extrabb = ir1.gen_bb("extra", curr_fn);
  ir1.builder().SetInsertPoint(extrabb);
}

static void walk_stmt_call(ASTNode *p) {
  ASTNode *name = p->exprn.operands[0];
  ASTNode *args = p->exprn.operands[1];
  const char *fnname = symname_get(name->idn.i);

  Function *fn = ir1.module().getFunction(fnname);
  if (!fn)
    yyerror("line: %d, col: %d: cannot find declared function: '%s'",
	    p->begloc.row, p->begloc.col, fnname);

  if (args->exprn.op != ARG_LISTS_ACTUAL)
    yyerror("line: %d, col: %d: not a argument list: '%s'",
	    p->begloc.row, p->begloc.col, fnname);

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  std::vector<Value *> argv;
  for (int i = 0; i < args->exprn.noperand; ++i) {
    // how to get the name for an expr? not possible / neccessary to get it
    walk_stack(args->exprn.operands[i]);
    auto pair = pop_right_value("exprarg");
    argv.push_back(pair.first);
  }

  bool isvoidty = fn->getReturnType()->isVoidTy();

  auto itr = function_map.find(fnname);
  if (itr == function_map.end()) {
    yyerror("line: %d, col: %d: cannot find function '%s' node",
	    p->begloc.row, p->begloc.col, fnname);
    return;
  }
 
  CallInst *callret = ir1.builder().CreateCall(fn, argv, isvoidty ? "" : fnname);
  auto operands = std::make_unique<CalcOperand>(OT_CallInst, callret, itr->second->fndecln.ret->type);
  oprand_stack.push_back(std::move(operands));
}

static void walk_stmt_ret(ASTNode *p) {
  Type *rettype = curr_fn->getReturnType();
  BasicBlock *retbb = (BasicBlock *)curr_fn_node->fndefn.retbb;

  if (p->exprn.noperand) {
    ASTNode *retn = p->exprn.operands[0];
    if (retn->type == TTE_Literal && !retn->litn.litv.fixed_type) {
      auto itr = function_map.find(curr_fn->getName().str());
      retn->litn.litv.intent_type = itr->second->fndecln.ret->type;
    }

    walk_stack(retn);
    auto pair = pop_right_value();
    Value *v = pair.first;
    if (enable_debug_info())
      diinfo->emit_location(p->endloc.row, p->endloc.col);

    // match the function return value and the literal return value
    if (rettype != v->getType()) {
      int rettypetok = curr_fn_node->fndefn.fn_decl->fndecln.ret->type;
      int exprtypetok = get_expr_type_from_tree(retn, 0);
      yyerror("line: %d, column: %d, return value `%s` type '%s' not match function type '%s'",
	      retn->begloc.row, retn->begloc.col, get_node_name_or_value(retn),
	      get_type_string(exprtypetok), get_type_string(rettypetok));
      return;
    }

    AllocaInst *retslot = (AllocaInst *)curr_fn_node->fndefn.retslot;
    ir1.builder().CreateStore(v, retslot);
  } else {
    if (enable_debug_info())
      diinfo->emit_location(p->endloc.row, p->endloc.col);

    if (rettype != ir1.void_type()) {
      yyerror("line: %d, column: %d, void function type cannot return value",
	      p->begloc.row, p->begloc.col);
      return;
    }
  }

  ir1.builder().CreateBr(retbb);

  BasicBlock *bb = ir1.gen_bb("afterret", curr_fn);
  ir1.builder().SetInsertPoint(bb);

  g_with_ret_value = true;
}

static void walk_stmt_expr(ASTNode *p) {
  walk_stack(p->exprn.operands[0]);
  auto pair1 = pop_right_value("v1");
  walk_stack(p->exprn.operands[1]);
  auto pair2 = pop_right_value("v2");
  Value *v1 = pair1.first;
  Value *v2 = pair2.first;
  Value *v3 = nullptr;

  // TODO: here should can use pair1.second pair2.second
  int type1 = get_expr_type_from_tree(p->exprn.operands[0], 0);
  int type2 = get_expr_type_from_tree(p->exprn.operands[1], 0);

  if (type1 != type2) {
    yyerror("operation have 2 different types: '%s', '%s'",
	    get_type_string(type1), get_type_string(type2));
    return;
  }

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  switch (p->exprn.op) {
  case '+':
    v3 = ir1.gen_add(v1, v2);
    break;
  case '-':
    v3 = ir1.gen_sub(v1, v2);
    break;
  case '*':
    v3 = ir1.gen_mul(v1, v2);
    break;
  case '/':
    v3 = ir1.gen_div(v1, v2);
    break;
  case '<':
  case '>':
  case GE:
  case LE:
  case NE:
  case EQ:
    v3 = generate_cmp_op(type1, v1, v2, p->exprn.op);
    type1 = BOOL; // BOOL for 1 bit of bool type
    break;
  default:
    yyerror("unknown expression operands: %d", p->exprn.op);
    break;
  }

  oprand_stack.push_back(std::make_unique<CalcOperand>(OT_Variable, v3, type1));
}

static void walk_statement(ASTNode *p) {
  // not allow global assign value, global variable definition is not assign
  if (!curr_fn && p->exprn.op != '=' && p->exprn.op != ';')
    return;

  switch (p->exprn.op) {
  case ';':
    walk_stmt_semicolon(p);
    break;
  case PRINT:
    walk_stmt_print(p);
    break;
  case '=':
    walk_stmt_assign(p);
    break;
  case UMINUS:
    walk_stmt_minus(p);
    break;
  case GOTO:
    walk_stmt_goto(p);
    break;
  case FN_CALL:
    walk_stmt_call(p);
    break;
  case RET:
    walk_stmt_ret(p);
    break;
  default:
    walk_stmt_expr(p);
    break;
  }
}

static Function *walk_fn_declare(ASTNode *p) {
  const char *fnname = symname_get(p->fndecln.name);

  Function *fn = ir1.module().getFunction(fnname);
  auto itr = function_map.find(fnname);
  if (itr != function_map.end()) {
    if (!fn) {
      // when consider name, the function map set belongs to ir1 module set
      yyerror("very strange, function must exists in the module");
      return nullptr;
    }

    return fn;
  }

  std::vector<const char *> param_names;
  std::vector<Type *> params;
  for (int i = 0; i < p->fndecln.args.argc; ++i) {
    int argname = p->fndecln.args.argnames[i];
    param_names.push_back(symname_get(argname));

    STEntry *entry = sym_getsym(p->symtable, argname, 0);
    if (!entry) {
      yyerror("cannot get parameter for %s\n", symname_get(argname));
      return NULL;
    }

    if (entry->sym_type != SymType::Sym_Variable) {
      yyerror("parameter is not variable type: %d", entry->sym_type);
      return nullptr;
    }

    Type *type = gen_type_from_token(entry->u.var->datatype->type);
    params.push_back(type);
  }

  Type *rettype = gen_type_from_token(p->fndecln.ret->type);
  fn = ir1.gen_extern_fn(rettype, fnname, params, &param_names, !!p->fndecln.args.contain_varg);
  function_map.insert(std::make_pair(fnname, p));
  fn->setCallingConv(CallingConv::C);

  //AttrListPtr func_printf_PAL;
  //fn->setAttributes(func_printf_PAL);

  return fn;
}

static void generate_final_return(ASTNode *p) {
  // should check if the function returned a value instead of append a return value always
  if (g_with_ret_value) {
    if (p->fndefn.fn_decl->fndecln.ret->type == VOID) {
      ir1.builder().CreateRetVoid();
    } else {
      Value *v = ir1.builder().CreateLoad((Value *)p->fndefn.retslot, "retret");
      ir1.builder().CreateRet(v);
    }
    return;
  }

  Value *retv = create_def_value(p->fndefn.fn_decl->fndecln.ret->type);
  if (retv)
    ir1.builder().CreateRet(retv);
  else
    ir1.builder().CreateRetVoid();
}

static Function *walk_fn_define(ASTNode *p) {
  g_with_ret_value = false;
  Function *fn = walk_fn_declare(p->fndefn.fn_decl);
  if (p->fndefn.fn_decl->fndecln.args.argc != fn->arg_size())
    yyerror("argument number not identical with definition (%d != %d)",
	    p->fndefn.fn_decl->fndecln.args.argc, fn->arg_size());

  curr_fn_node = p;
  curr_fn = fn;

  BasicBlock *retbb = ir1.gen_bb("ret");
  p->fndefn.retbb = (void *)retbb;

  BasicBlock *bb = ir1.gen_bb("entry", fn);
  ir1.builder().SetInsertPoint(bb);

  // insert here debugging information
  init_fn_param_info(fn, p->fndefn.fn_decl->fndecln.args, p->symtable, p->begloc.row);

  if (p->fndefn.fn_decl->fndecln.ret->type != VOID) {
    p->fndefn.retslot = (void *)ir1.gen_var(fn->getReturnType(), "retslot");
  } else {
    p->fndefn.retslot = nullptr;
  }

  if (enable_debug_info())
    diinfo->emit_location(p->begloc.row, p->begloc.col);

  walk_stack(p->fndefn.stmts);

  if (enable_debug_info())
    diinfo->emit_location(p->endloc.row, p->endloc.col);

  ir1.builder().CreateBr(retbb);
  fn->getBasicBlockList().push_back(retbb);
  ir1.builder().SetInsertPoint(retbb);
  generate_final_return(p);

  if (enable_debug_info())
    diinfo->lexical_blocks.pop_back();

  // finalize the debug info for verify function successfully, it may slow down
  // the performance invoke here, may move all function verification into later
  // TODO: move into all module is processed
  if (enable_debug_info())
    diinfo->dibuilder->finalize();

  std::string verify_message;
  llvm::raw_string_ostream rso(verify_message);
  if (llvm::verifyFunction(*fn, &rso)) {
    fprintf(stderr, "\nfrom line %d, to line %d: function verify failed: %s\n",
	    p->begloc.row, p->endloc.row, verify_message.c_str());
  }

  if (enable_emit_main()) {
    ir1.builder().SetInsertPoint(main_bb);
    curr_fn_node = main_fn_node;
    curr_fn = main_fn;
  } else {
    curr_fn_node = nullptr;
    curr_fn = nullptr;
  }

  return fn;
}

static void walk_as(ASTNode *node) {
  CADataType *type = node->exprasn.type;
  ASTNode *exprn = node->exprasn.expr;

  if (enable_debug_info())
    diinfo->emit_location(node->endloc.row, node->endloc.col);

  walk_stack(exprn);

  int stypetok = get_expr_type_from_tree(exprn, 0);
  Instruction::CastOps castopt = gen_cast_ops(stypetok, type->type);
  if (castopt == (ICO)-1) {
    yyerror("line: %d, column: %d, cannot convert `%s` into `%s`",
	    node->begloc.row, node->begloc.col,
	    get_type_string(stypetok), get_type_string(type->type));
    return;
  }

  auto calco = pop_right_operand("tmpexpr");
  Value *v = calco->operand;
  if (castopt != (ICO)0) {
    Type *stype = gen_type_from_token(type->type);
    v = ir1.gen_cast_value(castopt, v, stype);
  }
 
  auto u = std::make_unique<CalcOperand>(calco->type, v, type->type);
  oprand_stack.push_back(std::move(u));
}

static void walk_struct(ASTNode *node) {
  STEntry *entry = node->entry;
  CADataType *dt = entry->u.datatype;

  // TODO: may need generate struct llvm code here
}

typedef void (*walk_fn_t)(ASTNode *p);
static walk_fn_t walk_fn_array[TTE_Num] = {
  (walk_fn_t)walk_empty,
  (walk_fn_t)walk_literal,
  (walk_fn_t)walk_id,
  (walk_fn_t)walk_label,
  (walk_fn_t)walk_label_goto,
  (walk_fn_t)walk_statement,
  (walk_fn_t)walk_fn_declare,
  (walk_fn_t)walk_fn_define,
  (walk_fn_t)walk_while,
  (walk_fn_t)walk_if,
  (walk_fn_t)walk_as,
  (walk_fn_t)walk_struct,
};

static int walk_stack(ASTNode *p) {
  if (!p)
    return 0;

  (*walk_fn_array[p->type])(p);
  return 0;
}

static void do_optimize_pass() {
  switch (genv.opt_level) {
  case OL_O1:
    {
      Function *fn = ir1.module().getFunction("main");
      ir1.fpm().run(*fn);
      break;
    }
  case OL_O2:
    {
      Function *fn = ir1.module().getFunction("main");
      ir1.fpm().run(*fn);
      ir1.pm().run(ir1.module());
      break;
    }
  case OL_O3:
    break;
  case OL_NONE:
  default:
    break;
  }
}

static int llvm_codegen_begin(RootTree *tree) {
  // mock ASTNode for generated main function
  curr_fn = nullptr;

  if (enable_debug_info())
    diunit = diinfo->create_difile();

  if (enable_emit_main()) {
    main_fn = ir1.gen_function(ir1.int_type<int>(), "main", std::vector<Type *>());
    main_bb = ir1.gen_bb("entry", main_fn);
    main_fn_node->fndefn.retbb = (void *)ir1.gen_bb("ret");

    curr_fn_node = main_fn_node;
    curr_fn = main_fn;
    ir1.builder().SetInsertPoint(main_bb);
    ST_ArgList arglist;
    arglist.argc = 0;

    init_fn_param_info(main_fn, arglist, tree->root_symtable, tree->begloc_main.row);
    main_fn_node->fndefn.retslot =
      (void *)ir1.gen_var(main_fn->getReturnType(), "retslot", ir1.gen_int<int>(0));
  }

  // TODO: add grammar for handling extern functions instead hardcoded here
  Function *printf_fn = ir1.module().getFunction("printf");
  if (!printf_fn) {
    auto param_names = std::vector<const char *>(1, "s");
    printf_fn = ir1.gen_extern_fn(ir1.int_type<int>(), "printf",
				  std::vector<Type *>(1, ir1.intptr_type<char>()),
				  &param_names,	true);
    printf_fn->setCallingConv(CallingConv::C);

    //AttrListPtr func_printf_PAL;
    //printf_fn->setAttributes(func_printf_PAL);
  }

  return 0;
}

static int llvm_codegen_ll(const char *output = nullptr) {
  // run pass
  do_optimize_pass();

  if (output && output[0]) {
    std::error_code ec;
    raw_fd_ostream os(output, ec, llvm::sys::fs::F_None);
    if (ec) {
      errs() << "could not open file: " << output
	     << ", error code: " << ec.message();
      return -1;
    }

    ir1.module().print(os, nullptr);
  } else {
    ir1.module().print(outs(), nullptr);
  }
  return 0;
}

static CodeGenOpt::Level to_llvm_codegenopt(Optimize_Level level) {
  return (CodeGenOpt::Level)level;
}

static int llvm_codegen_native(CodeGenFileType type, const char *output = nullptr) {
  // x86_64-pc-linux-gnu (clang --version)
  std::string target_triple = llvm::sys::getDefaultTargetTriple();
  std::string error;
  const Target *target = llvm::TargetRegistry::lookupTarget(target_triple, error);
  if (!target) {
    llvm::errs() << error;
    return -1;
  }

  auto cpu = "generic";
  auto features = "";
  llvm::TargetOptions opt;
  auto rm = llvm::Optional<Reloc::Model>();
  Optional<CodeModel::Model> cm = None;
  CodeGenOpt::Level ol = to_llvm_codegenopt(genv.opt_level);
  bool jit = false;
  TargetMachine *target_machine =
    target->createTargetMachine(target_triple, cpu, features, opt, rm, cm, ol, jit);
  ir1.module().setDataLayout(target_machine->createDataLayout());
  ir1.module().setTargetTriple(target_triple);

  // run pass
  do_optimize_pass();

  legacy::PassManager pass;
  auto filetype = type; // CGFT_ObjectFile; CGFT_AssemblyFile;  CGFT_Null;

  if (output && output[0]) {
    std::error_code ec;
    raw_fd_ostream os(output, ec, llvm::sys::fs::F_None);
    if (ec) {
      errs() << "could not open file: " << output
	     << ", error code: " << ec.message();
      return -1;
    }

    target_machine->addPassesToEmitFile(pass, os, nullptr, filetype);
    pass.run(ir1.module());
    os.flush();
  } else {
    target_machine->addPassesToEmitFile(pass, outs(), nullptr, filetype);
    pass.run(ir1.module());
    outs().flush();
  }

  return 0;
}

static int llvm_codegen_jit() {
  do_optimize_pass();

  ir1.module().setDataLayout(jit1->get_datalayout());
  auto rt = jit1->get_main_jitdl().createResourceTracker();
  auto tsm = orc::ThreadSafeModule(ir1.move_module(), ir1.move_ctx());
  exit_on_error(jit1->add_module(std::move(tsm), rt));
  auto func_symbol = exit_on_error(jit1->find("main"));
  int (*func)() = (int (*)())(intptr_t)func_symbol.getAddress();
  printf("\nreturn value: %d\n", func());
  exit_on_error(rt->remove());
  return 0;
}

static const char *make_native_linker_command(const char *input, const char *output) {
  static char command[1024];

  // sprintf(command, "clang %s -o %s", input, output);

  sprintf(command, "ld -dynamic-linker /lib64/ld-linux-x86-64.so.2 cruntime/*.o %s -o %s -lc", input, output);

  return command;
}

static int llvm_codegen_end() {
  int ret = 0;
  if (enable_emit_main()) {
    BasicBlock *retbb = (BasicBlock *)main_fn_node->fndefn.retbb;
    ir1.builder().CreateBr(retbb);
    main_fn->getBasicBlockList().push_back(retbb);
    ir1.builder().SetInsertPoint(retbb);
    Value *v = ir1.builder().CreateLoad((Value *)main_fn_node->fndefn.retslot, "retret");
    ir1.builder().CreateRet(v);

    // pop off the lexical block for the main function. When enhanced and define
    // other functions it will need encapsulate the related functions into function
    // or class
    if (enable_debug_info()) {
      diinfo->lexical_blocks.pop_back();

      // Validate the generated code, checking for consistency.
      verifyFunction(*main_fn);
    }
  }

#if 0
  // this code fragment should put into when generating a function failed
  // when generating each new function a lexical block should be pushed and when
  // the new function is generate over, then the should pop up the lexical block
  // when any error occurs when generating function body, then remove function.
  main_fn->eraseFromParent();

  // pop up the lexical block for the function since it is added unconditionally
  diinfo->lexical_blocks.pop_back();
#endif

  // finalize the debug info
  if (enable_debug_info())
    diinfo->dibuilder->finalize();

  std::string verify_message;
  llvm::raw_string_ostream rso(verify_message);
  bool verify_debug = true;
  if (verifyModule(ir1.module(), &rso, &verify_debug) ) {
    fprintf(stderr, "\nmodule verify failed: %s\n",
	    verify_message.c_str());
  }

  switch (genv.llvm_gen_type) {
  case LGT_LL:
    ret = llvm_codegen_ll(genv.outfile);
    break;
  case LGT_S:
    ret = llvm_codegen_native(CGFT_AssemblyFile, genv.outfile);
    break;
  case LGT_C:
    ret = llvm_codegen_native(CGFT_ObjectFile, genv.outfile);
    break;
  case LGT_JIT:
    ret = llvm_codegen_jit();
    break;
  case LGT_NATIVE:
    {
    char objname[MAX_PATH + 1];
    sprintf(objname, "%s.o", genv.outfile);
    ret = llvm_codegen_native(CGFT_ObjectFile, objname);
    if (ret == -1)
      return ret;

    system(make_native_linker_command(objname, genv.outfile));
    }

    break;
  default:
    break;
  }

  return ret;
}

static void init_llvm_env() {
  ir1.init_module_and_passmanager(genv.src_path);
  jit1 = exit_on_error(jit_codegen::JIT1::create_instance());
  if (enable_debug_info())
    diinfo = std::make_unique<dwarf_debug::DWARFDebugInfo>(ir1.builder(), ir1.module(), genv.src_path);
}

#ifdef __cplusplus
extern "C"
#endif
int walk(RootTree *tree) {
  NodeChain *p = tree->head;
  llvm_codegen_begin(tree);
  for (int i = 0; i < tree->count; ++i) {
    walk_stack(p->node);
    p = p->next;
  }

  llvm_codegen_end();
  return 0;
}

static void usage() {
  // [-ll] | [-S] | [-native] | [-c] | [-jit] [-O] | [-g] | [-nomain] | [-dot <dotfile>]
  fprintf(stderr,
	  "Usage: ca [options] <input> [<output>]\n"
	  "Options:\n"
	  "         -ll:      compile into IR assembly file: .ll (llvm)\n"
	  "         -S:       compile into native (as) assembly file: .s\n"
	  "         -native:  compile into native execute file: ELF file on linux, PE file on windows (default value)\n"
	  "         -c:       compile into native object file: .o\n"
	  "         -jit:     interpret using jit (llvm)\n"
	  "         -O[123]:  do optimization of level 1 2 3, default is level 2\n"
	  "         -g:       do not do any optimization (default value)\n"
	  "         -main:    do generate the default main function\n"
	  "         -dot <dotfile>:  generate the do not generate the default main function\n"
	  );
  exit(-1);
}

static int init_config(int argc, char *argv[]) {
  int arg = 0;

  if (++arg >= argc)
    usage();

  genv.llvm_gen_type = LGT_JIT;
  genv.opt_level = OL_NONE;
  genv.emit_debug = 0;
  genv.emit_main = 0;
  genv.emit_dot = 0;
  genv.dot_sparsed = 1;

  int i = 0;
  while(i < 3) {
    if (argv[arg][0] == '-') {
      if (!strcmp(argv[arg], "-ll")) {
	genv.llvm_gen_type = LGT_LL;
      } else if (!strcmp(argv[arg], "-S")) {
	genv.llvm_gen_type = LGT_S;
      } else if (!strcmp(argv[arg], "-native")) {
	genv.llvm_gen_type = LGT_NATIVE;
      } else if (!strcmp(argv[arg], "-c")) {
	genv.llvm_gen_type = LGT_C;
      } else if (!strcmp(argv[arg], "-jit")) {
	genv.llvm_gen_type = LGT_JIT;
      } else if (!strcmp(argv[arg], "-O")) {
	genv.opt_level = OL_O2;
      } else if (!strcmp(argv[arg], "-O1")) {
	genv.opt_level = OL_O1;
      } else if (!strcmp(argv[arg], "-O2")) {
	genv.opt_level = OL_O2;
      } else if (!strcmp(argv[arg], "-O3")) {
	genv.opt_level = OL_O3;
      } else if (!strcmp(argv[arg], "-g")) {
	genv.emit_debug = 1;
      } else if (!strcmp(argv[arg], "-main")) {
	genv.emit_main = 1;
      } else if (!strcmp(argv[arg], "-dot")) {
	genv.emit_dot = 1;
	if (++arg >= argc || argv[arg][0] == '-') {
	  fprintf(stderr, "Should specify a dot file path\n\n");
	  usage();
	}
	strcpy(genv.dotpath, argv[arg]);
      } else {
	usage();
      }

      if (++arg >= argc)
	usage();

      ++i;
      continue;
    }

    break;
  }

  if (arg >= argc)
    usage();

  size_t len = strlen(argv[arg]);
  if (len > MAX_PATH) {
    fprintf(stderr, "too long of source file path: %s\n", argv[arg]);
    return -1;
  }

  strcpy(genv.src_path, argv[arg]);

  genv.ginput = fopen(argv[arg], "r");
  if (!genv.ginput) {
    fprintf(stderr, "Open input file failed: %s, errno=%d\n", argv[1], errno);
    return -1;
  }

  if (++arg >= argc) {
    genv.outfile[0] = '\0';
    genv.goutput = stdout;
  } else {
    strcpy(genv.outfile, argv[arg]);
    genv.goutput = fopen(argv[arg], "w+");
    if (!genv.goutput) {
      fprintf(stderr, "Open output file failed: %s, errno=%d\n", argv[arg], errno);
      return -1;
    }
  }

  genv.code_buffer = (int *)calloc(MAX_OPS, sizeof(int));
  if (!genv.code_buffer) {
    fprintf(stderr, "Allocate buffer failed, errno=%d\n", errno);
    return -1;
  }

  genv.stackbased = 1;

  return 0;
}

int main(int argc, char *argv[]) {
  if (init_config(argc, argv)) {
    fprintf(stderr, "init config failed\n");
    exit(-1);
  }

  if (yyparser_init()) {
    fprintf(stderr, "init parser failed\n");
    exit(-1);
  }

  init_llvm_env();

  yyin = genv.ginput;

  if (genv.llvm_gen_type == LGT_JIT)
    fprintf(stderr, "program `%s` :\n", genv.src_path);

  yyparse();
  
  walk(gtree);

  dot_finalize();

  return 0;
}
