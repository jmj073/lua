/*
** $Id: lcodegen.c $
** Code generator from AST
** See Copyright Notice in lua.h
*/

#define lcodegen_c
#define LUA_CORE

#include "lprefix.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"
#include "last.h"
#include "lcodegen.h"


/*
** Local variable info for code generation
*/
typedef struct LocalVar {
  TString *name;
  int reg;               /* register number */
} LocalVar;

/*
** Code generation state (similar to FuncState in lparser)
*/
typedef struct CodeGenState {
  Proto *f;              /* current function header */
  lua_State *L;
  int pc;                /* next position to code */
  int nk;                /* number of elements in 'k' */
  int freereg;           /* first free register */
  LocalVar *locals;      /* local variables array */
  int nlocals;           /* number of local variables */
  int sizelocals;        /* size of locals array */
} CodeGenState;


/*
** Forward declarations
*/
static void gen_chunk (CodeGenState *cg, AstNode *node);
static void gen_statement (CodeGenState *cg, AstNode *node);
static void gen_statlist (CodeGenState *cg, StmtList *list);
static int gen_expr (CodeGenState *cg, AstNode *node);
static int gen_binop (CodeGenState *cg, AstBinOp *binop, int reg);
static int gen_unop (CodeGenState *cg, AstUnOp *unop, int reg);
static int gen_call_expr (CodeGenState *cg, AstCall *call, int reg);
static int gen_var (CodeGenState *cg, AstVar *var, int reg);
static int gen_table_constructor (CodeGenState *cg, AstTableCtor *tctor, int reg);
static int gen_function (CodeGenState *cg, AstFunction *func, int reg);
static void gen_return (CodeGenState *cg, AstReturn *ret);
static void gen_local_decl (CodeGenState *cg, AstLocalDecl *local);
static void gen_if (CodeGenState *cg, AstIf *ifstmt);
static void gen_assignment (CodeGenState *cg, AstAssignment *assign);


/*
** Find local variable by name
*/
static int find_local (CodeGenState *cg, TString *name) {
  int i;
  for (i = cg->nlocals - 1; i >= 0; i--) {
    if (cg->locals[i].name == name) {
      return cg->locals[i].reg;
    }
  }
  return -1;  /* not found */
}

/*
** Add local variable
*/
static int add_local (CodeGenState *cg, TString *name) {
  int reg = cg->freereg++;
  luaM_growvector(cg->L, cg->locals, cg->nlocals, cg->sizelocals,
                  LocalVar, USHRT_MAX, "local variables");
  cg->locals[cg->nlocals].name = name;
  cg->locals[cg->nlocals].reg = reg;
  cg->nlocals++;
  return reg;
}

/*
** Add constant to Proto
*/
static int addk (CodeGenState *cg, TValue *v) {
  Proto *f = cg->f;
  int oldsize = f->sizek;
  int k = cg->nk;
  int i;
  /* Check if constant already exists */
  for (i = 0; i < cg->nk; i++) {
    if (luaV_rawequalobj(&f->k[i], v))
      return i;
  }
  /* Add new constant */
  luaM_growvector(cg->L, f->k, k, f->sizek, TValue, MAXARG_Ax, "constants");
  while (oldsize < f->sizek)
    setnilvalue(&f->k[oldsize++]);
  setobj(cg->L, &f->k[k], v);
  cg->nk++;
  return k;
}


/*
** Emit instruction
*/
static int code_instruction (CodeGenState *cg, Instruction i, int line) {
  Proto *f = cg->f;
  luaM_growvector(cg->L, f->code, cg->pc, f->sizecode, Instruction,
                  INT_MAX, "opcodes");
  f->code[cg->pc] = i;
  luaM_growvector(cg->L, f->lineinfo, cg->pc, f->sizelineinfo, ls_byte,
                  INT_MAX, "opcodes");
  f->lineinfo[cg->pc] = line;
  return cg->pc++;
}


/*
** Generate code for variable reference
*/
static int gen_var (CodeGenState *cg, AstVar *var, int reg) {
  int local_reg = find_local(cg, var->name);
  
  if (local_reg >= 0) {
    /* Local variable: just use the register */
    cg->freereg = local_reg + 1;
    return local_reg;
  } else {
    /* Global variable: get from _ENV (upvalue 0) */
    TValue k;
    int kidx;
    setsvalue(cg->L, &k, var->name);
    kidx = addk(cg, &k);
    code_instruction(cg, CREATE_ABCk(OP_GETTABUP, reg, 0, kidx, 0), var->header.line);
    return reg;
  }
}

/*
** Generate code for function call expression
*/
static int gen_call_expr (CodeGenState *cg, AstCall *call, int reg) {
  int func_reg = gen_expr(cg, call->func);
  ExprList *arg;
  int nargs = 0;
  int base = func_reg;
  
  /* Generate code for arguments */
  for (arg = call->args; arg != NULL; arg = arg->next) {
    gen_expr(cg, arg->expr);
    nargs++;
  }
  
  /* OP_CALL: A B C - R[A], ... ,R[A+C-2] := R[A](R[A+1], ... ,R[A+B-1]) */
  code_instruction(cg, CREATE_ABCk(OP_CALL, base, nargs + 1, 2, 0), call->header.line);
  /* Result is in base register */
  cg->freereg = base + 1;
  return base;
}

/*
** Generate code for binary operation
*/
static int gen_binop (CodeGenState *cg, AstBinOp *binop, int reg) {
  int left_reg, right_reg;
  OpCode op;
  int k = 0;  /* k flag for comparison operators */
  
  /* Special handling for GT, GE, NE - transform to opposite operation */
  switch (binop->op) {
    case OPR_GT:  /* a > b  →  b < a */
      right_reg = gen_expr(cg, binop->left);
      left_reg = gen_expr(cg, binop->right);
      op = OP_LT;
      break;
    case OPR_GE:  /* a >= b  →  b <= a */
      right_reg = gen_expr(cg, binop->left);
      left_reg = gen_expr(cg, binop->right);
      op = OP_LE;
      break;
    case OPR_NE:  /* a != b  →  not (a == b), use k=1 to invert */
      left_reg = gen_expr(cg, binop->left);
      right_reg = gen_expr(cg, binop->right);
      op = OP_EQ;
      k = 1;  /* Invert the result */
      break;
    case OPR_AND: {
      /* a and b: short-circuit evaluation */
      int pc_jump;
      left_reg = gen_expr(cg, binop->left);
      code_instruction(cg, CREATE_ABCk(OP_TESTSET, reg, left_reg, 0, 0), binop->header.line);
      pc_jump = code_instruction(cg, CREATE_sJ(OP_JMP, 0, 0), binop->header.line);
      right_reg = gen_expr(cg, binop->right);
      if (right_reg != reg) {
        code_instruction(cg, CREATE_ABCk(OP_MOVE, reg, right_reg, 0, 0), binop->header.line);
      }
      SETARG_sJ(cg->f->code[pc_jump], cg->pc - pc_jump - 1);
      cg->freereg = reg + 1;
      return reg;
    }
    case OPR_OR: {
      /* a or b: short-circuit evaluation */
      int pc_jump;
      left_reg = gen_expr(cg, binop->left);
      code_instruction(cg, CREATE_ABCk(OP_TESTSET, reg, left_reg, 0, 1), binop->header.line);
      pc_jump = code_instruction(cg, CREATE_sJ(OP_JMP, 0, 0), binop->header.line);
      right_reg = gen_expr(cg, binop->right);
      if (right_reg != reg) {
        code_instruction(cg, CREATE_ABCk(OP_MOVE, reg, right_reg, 0, 0), binop->header.line);
      }
      SETARG_sJ(cg->f->code[pc_jump], cg->pc - pc_jump - 1);
      cg->freereg = reg + 1;
      return reg;
    }
    default:
      /* Normal binary operators */
      left_reg = gen_expr(cg, binop->left);
      right_reg = gen_expr(cg, binop->right);
      
      switch (binop->op) {
        case OPR_ADD: op = OP_ADD; break;
        case OPR_SUB: op = OP_SUB; break;
        case OPR_MUL: op = OP_MUL; break;
        case OPR_DIV: op = OP_DIV; break;
        case OPR_IDIV: op = OP_IDIV; break;
        case OPR_MOD: op = OP_MOD; break;
        case OPR_POW: op = OP_POW; break;
        case OPR_BAND: op = OP_BAND; break;
        case OPR_BOR: op = OP_BOR; break;
        case OPR_BXOR: op = OP_BXOR; break;
        case OPR_SHL: op = OP_SHL; break;
        case OPR_SHR: op = OP_SHR; break;
        case OPR_CONCAT: op = OP_CONCAT; break;
        case OPR_EQ: op = OP_EQ; break;
        case OPR_LT: op = OP_LT; break;
        case OPR_LE: op = OP_LE; break;
        default:
          luaG_runerror(cg->L, "codegen: unknown binary operator");
          return reg;
      }
      break;
  }
  
  code_instruction(cg, CREATE_ABCk(op, reg, left_reg, right_reg, k), binop->header.line);
  cg->freereg = reg + 1;
  return reg;
}

/*
** Generate code for function definition
*/
static int gen_function (CodeGenState *cg, AstFunction *func, int reg) {
  Proto *f;
  CodeGenState nested_cg;
  NameList *param;
  int nparams = 0;
  Closure *cl;
  
  /* Create new Proto for this function */
  f = luaF_newproto(cg->L);
  
  /* Add proto to parent's proto list */
  int proto_idx = cg->f->sizep;
  luaM_growvector(cg->L, cg->f->p, proto_idx, cg->f->sizep, Proto *, MAXARG_Bx, "functions");
  cg->f->p[proto_idx] = f;
  cg->f->sizep = proto_idx + 1;
  
  /* Initialize nested code generation state */
  nested_cg.f = f;
  nested_cg.L = cg->L;
  nested_cg.pc = 0;
  nested_cg.nk = 0;
  nested_cg.freereg = 0;
  nested_cg.locals = NULL;
  nested_cg.nlocals = 0;
  nested_cg.sizelocals = 0;
  
  /* Set function properties */
  f->linedefined = func->header.line;
  f->lastlinedefined = func->header.line;
  f->source = cg->f->source;
  
  /* Count and register parameters */
  for (param = func->params; param != NULL; param = param->next) {
    add_local(&nested_cg, param->name);
    nparams++;
  }
  
  f->numparams = nparams;
  f->flag = func->is_vararg ? PF_ISVARARG : 0;
  f->maxstacksize = 2;
  
  /* Emit VARARGPREP if needed */
  if (func->is_vararg) {
    code_instruction(&nested_cg, CREATE_ABCk(OP_VARARGPREP, nparams, 0, 0, 0), func->header.line);
  }
  
  /* Generate body */
  if (func->body->kind == AST_BLOCK) {
    AstBlock *block = (AstBlock *)func->body;
    gen_statlist(&nested_cg, block->statements);
  }
  
  /* Add final RETURN if not present */
  if (nested_cg.pc == 0 || GET_OPCODE(f->code[nested_cg.pc - 1]) != OP_RETURN) {
    code_instruction(&nested_cg, CREATE_ABx(OP_RETURN, 0, 1), func->header.line);
  }
  
  /* Update max stack size */
  if (nested_cg.freereg > f->maxstacksize)
    f->maxstacksize = nested_cg.freereg;
  
  /* Cleanup locals */
  if (nested_cg.locals)
    luaM_free(cg->L, nested_cg.locals);
  
  /* Generate CLOSURE instruction in parent */
  code_instruction(cg, CREATE_ABx(OP_CLOSURE, reg, proto_idx), func->header.line);
  cg->freereg = reg + 1;
  
  return reg;
}

/*
** Generate code for table constructor
*/
static int gen_table_constructor (CodeGenState *cg, AstTableCtor *tctor, int reg) {
  TableField *field;
  int table_reg = reg;
  int na = 0;  /* array elements */
  int nh = 0;  /* hash elements */
  int tostore = 0;  /* pending array elements */
  int base_reg;
  
  /* Create new table: OP_NEWTABLE A B C k - R[A] := {} */
  code_instruction(cg, CREATE_ABCk(OP_NEWTABLE, table_reg, 0, 0, 0), tctor->header.line);
  cg->freereg = table_reg + 1;
  base_reg = cg->freereg;
  
  /* Process fields */
  for (field = tctor->fields; field != NULL; field = field->next) {
    if (field->kind == FIELD_LIST) {
      /* Array element: t[na+1] = value */
      int val_reg = gen_expr(cg, field->value);
      tostore++;
      
      /* Flush when we have enough pending elements */
      if (tostore >= 50 || field->next == NULL) {
        if (tostore > 0) {
          /* OP_SETLIST: A B C k - R[A][(C-1)*50+i] := R[A+i], 1 <= i <= B */
          code_instruction(cg, CREATE_ABCk(OP_SETLIST, table_reg, tostore, na/50 + 1, 0), tctor->header.line);
          na += tostore;
          tostore = 0;
          cg->freereg = base_reg;  /* Reset register */
        }
      }
    } else {
      /* Record element: t[key] = value */
      int key_reg, val_reg;
      
      /* Flush pending list elements first */
      if (tostore > 0) {
        code_instruction(cg, CREATE_ABCk(OP_SETLIST, table_reg, tostore, na/50 + 1, 0), tctor->header.line);
        na += tostore;
        tostore = 0;
        cg->freereg = base_reg;
      }
      
      /* Generate key and value */
      key_reg = gen_expr(cg, field->key);
      val_reg = gen_expr(cg, field->value);
      
      /* OP_SETTABLE: A B C - R[A][R[B]] := RK(C) */
      code_instruction(cg, CREATE_ABCk(OP_SETTABLE, table_reg, key_reg, val_reg, 0), tctor->header.line);
      nh++;
      cg->freereg = base_reg;  /* Reset register */
    }
  }
  
  cg->freereg = table_reg + 1;
  return table_reg;
}

/*
** Generate code for unary operation
*/
static int gen_unop (CodeGenState *cg, AstUnOp *unop, int reg) {
  int operand_reg = gen_expr(cg, unop->operand);
  OpCode op;
  
  switch (unop->op) {
    case OPR_MINUS: op = OP_UNM; break;
    case OPR_NOT: op = OP_NOT; break;
    case OPR_LEN: op = OP_LEN; break;
    case OPR_BNOT: op = OP_BNOT; break;
    default:
      luaG_runerror(cg->L, "codegen: unknown unary operator");
      return reg;
  }
  
  code_instruction(cg, CREATE_ABCk(op, reg, operand_reg, 0, 0), unop->header.line);
  cg->freereg = reg + 1;
  return reg;
}

/*
** Generate code for expression, returns register where result is stored
*/
static int gen_expr (CodeGenState *cg, AstNode *node) {
  int reg;
  TValue k;
  int kidx;
  
  if (node == NULL)
    return -1;
  
  reg = cg->freereg++;
  
  switch (node->kind) {
    case AST_NIL:
      code_instruction(cg, CREATE_ABx(OP_LOADNIL, reg, 0), node->line);
      break;
    case AST_TRUE:
      code_instruction(cg, CREATE_ABCk(OP_LOADTRUE, reg, 0, 0, 0), node->line);
      break;
    case AST_FALSE:
      code_instruction(cg, CREATE_ABCk(OP_LOADFALSE, reg, 0, 0, 0), node->line);
      break;
    case AST_NUMERAL: {
      AstNumeral *num = (AstNumeral *)node;
      lua_Integer i;
      setfltvalue(&k, num->value);
      if (luaV_tointegerns(&k, &i, LUA_FLOORN2I)) {
        setivalue(&k, i);
      }
      kidx = addk(cg, &k);
      code_instruction(cg, CREATE_ABx(OP_LOADK, reg, kidx), node->line);
      break;
    }
    case AST_STRING: {
      AstString *str = (AstString *)node;
      setsvalue(cg->L, &k, str->str);
      kidx = addk(cg, &k);
      code_instruction(cg, CREATE_ABx(OP_LOADK, reg, kidx), node->line);
      break;
    }
    case AST_VAR:
      return gen_var(cg, (AstVar *)node, reg);
    case AST_CALL:
      return gen_call_expr(cg, (AstCall *)node, reg);
    case AST_TABLECTOR:
      return gen_table_constructor(cg, (AstTableCtor *)node, reg);
    case AST_FUNCTION:
      return gen_function(cg, (AstFunction *)node, reg);
    case AST_INDEXING: {
      AstIndexing *idx = (AstIndexing *)node;
      int table_reg = gen_expr(cg, idx->table);
      int key_reg = gen_expr(cg, idx->index);
      /* OP_GETTABLE: A B C - R[A] := R[B][RK(C)] */
      code_instruction(cg, CREATE_ABCk(OP_GETTABLE, reg, table_reg, key_reg, 0), node->line);
      cg->freereg = reg + 1;
      return reg;
    }
    case AST_BINOP:
      return gen_binop(cg, (AstBinOp *)node, reg);
    case AST_UNOP:
      return gen_unop(cg, (AstUnOp *)node, reg);
    default:
      luaG_runerror(cg->L, "codegen: unsupported expression kind %d", node->kind);
      break;
  }
  
  return reg;
}


/*
** Generate code for return statement
*/
static void gen_return (CodeGenState *cg, AstReturn *ret) {
  int nret = 0;
  int base = cg->freereg;
  ExprList *elist;
  
  /* Generate code for each return value */
  for (elist = ret->exprs; elist != NULL; elist = elist->next) {
    gen_expr(cg, elist->expr);
    nret++;
  }
  
  /* Emit RETURN instruction */
  code_instruction(cg, CREATE_ABx(OP_RETURN, base, nret + 1), ret->header.line);
}

/*
** Generate code for local variable declaration
*/
static void gen_local_decl (CodeGenState *cg, AstLocalDecl *local) {
  NameList *name;
  ExprList *value;
  
  /* Generate values and assign to variables */
  value = local->values;
  for (name = local->names; name != NULL; name = name->next) {
    int reg = add_local(cg, name->name);
    
    if (value) {
      /* Assign value to variable */
      int val_reg = gen_expr(cg, value->expr);
      if (val_reg != reg) {
        /* Move value to variable register */
        code_instruction(cg, CREATE_ABCk(OP_MOVE, reg, val_reg, 0, 0), local->header.line);
      }
      value = value->next;
    } else {
      /* Initialize to nil */
      code_instruction(cg, CREATE_ABx(OP_LOADNIL, reg, 0), local->header.line);
    }
  }
}

/*
** Generate code for assignment statement
*/
static void gen_assignment (CodeGenState *cg, AstAssignment *assign) {
  NameList *var;
  ExprList *value;
  int base_reg = cg->freereg;
  int nvalues = 0;
  
  /* Generate all values first */
  for (value = assign->values; value != NULL; value = value->next) {
    gen_expr(cg, value->expr);
    nvalues++;
  }
  
  /* Assign to variables */
  {
    int i = 0;
    for (var = assign->vars; var != NULL; var = var->next) {
      int local_reg = find_local(cg, var->name);
      int val_reg = base_reg + i;
      
      if (local_reg >= 0) {
        /* Local variable: use MOVE if needed */
        if (i < nvalues) {
          if (val_reg != local_reg) {
            code_instruction(cg, CREATE_ABCk(OP_MOVE, local_reg, val_reg, 0, 0), assign->header.line);
          }
        } else {
          /* No value for this variable, assign nil */
          code_instruction(cg, CREATE_ABx(OP_LOADNIL, local_reg, 0), assign->header.line);
        }
      } else {
        /* Global variable: use SETTABUP */
        TValue k;
        int kidx;
        setsvalue(cg->L, &k, var->name);
        kidx = addk(cg, &k);
        
        if (i < nvalues) {
          /* OP_SETTABUP: A B C k - UpValue[A][K[B]:string] := RK(C) */
          code_instruction(cg, CREATE_ABCk(OP_SETTABUP, 0, kidx, val_reg, 0), assign->header.line);
        } else {
          /* Assign nil */
          int nil_reg = cg->freereg++;
          code_instruction(cg, CREATE_ABx(OP_LOADNIL, nil_reg, 0), assign->header.line);
          code_instruction(cg, CREATE_ABCk(OP_SETTABUP, 0, kidx, nil_reg, 0), assign->header.line);
        }
      }
      i++;
    }
  }
  
  /* Reset free register */
  cg->freereg = base_reg;
}

/*
** Generate code for if statement
*/
static void gen_if (CodeGenState *cg, AstIf *ifstmt) {
  IfClause *clause;
  int *jump_to_end = NULL;
  int njumps = 0;
  int saved_freereg = cg->freereg;
  
  for (clause = ifstmt->clauses; clause != NULL; clause = clause->next) {
    if (clause->condition) {
      /* if/elseif: test condition */
      int cond_reg = gen_expr(cg, clause->condition);
      int pc_jump;
      
      /* TEST: if (not R[A] == k) then pc++ */
      code_instruction(cg, CREATE_ABCk(OP_TEST, cond_reg, 0, 0, 0), clause->condition->line);
      
      /* JMP to next clause if condition is false */
      pc_jump = code_instruction(cg, CREATE_sJ(OP_JMP, 0, 0), clause->condition->line);
      
      /* Generate body */
      cg->freereg = saved_freereg;
      if (clause->body->kind == AST_BLOCK) {
        AstBlock *block = (AstBlock *)clause->body;
        gen_statlist(cg, block->statements);
      }
      
      /* Jump to end after executing this clause */
      if (clause->next) {
        int jump_pc = code_instruction(cg, CREATE_sJ(OP_JMP, 0, 0), clause->body->line);
        jump_to_end = luaM_reallocvector(cg->L, jump_to_end, njumps, njumps + 1, int);
        jump_to_end[njumps++] = jump_pc;
      }
      
      /* Patch jump to next clause */
      SETARG_sJ(cg->f->code[pc_jump], cg->pc - pc_jump - 1);
    } else {
      /* else: just generate body */
      cg->freereg = saved_freereg;
      if (clause->body->kind == AST_BLOCK) {
        AstBlock *block = (AstBlock *)clause->body;
        gen_statlist(cg, block->statements);
      }
    }
  }
  
  /* Patch all jumps to end */
  {
    int i;
    for (i = 0; i < njumps; i++) {
      SETARG_sJ(cg->f->code[jump_to_end[i]], cg->pc - jump_to_end[i] - 1);
    }
    if (jump_to_end)
      luaM_free(cg->L, jump_to_end);
  }
  
  cg->freereg = saved_freereg;
}


/*
** Generate code for statement
*/
static void gen_statement (CodeGenState *cg, AstNode *node) {
  if (node == NULL)
    return;
  
  switch (node->kind) {
    case AST_RETURN:
      gen_return(cg, (AstReturn *)node);
      break;
    case AST_LOCAL_DECL:
      gen_local_decl(cg, (AstLocalDecl *)node);
      break;
    case AST_ASSIGNMENT:
      gen_assignment(cg, (AstAssignment *)node);
      break;
    case AST_FUNCTION_STMT: {
      /* fn name(params) { body } - global function */
      AstFunctionStmt *stmt = (AstFunctionStmt *)node;
      int func_reg = gen_function(cg, stmt->func, cg->freereg);
      TValue k;
      int kidx;
      
      /* Store in global */
      setsvalue(cg->L, &k, stmt->name);
      kidx = addk(cg, &k);
      code_instruction(cg, CREATE_ABCk(OP_SETTABUP, 0, kidx, func_reg, 0), stmt->header.line);
      cg->freereg = func_reg;  /* Reset */
      break;
    }
    case AST_LOCAL_FUNC_STMT: {
      /* local fn name(params) { body } */
      AstLocalFuncStmt *stmt = (AstLocalFuncStmt *)node;
      int func_reg;
      
      /* Register name first for recursion */
      func_reg = add_local(cg, stmt->name);
      
      /* Generate function (can now reference itself) */
      gen_function(cg, stmt->func, func_reg);
      break;
    }
    case AST_IF:
      gen_if(cg, (AstIf *)node);
      break;
    case AST_FUNCCALL_STMT: {
      AstFuncCallStmt *stmt = (AstFuncCallStmt *)node;
      AstCall *call = (AstCall *)stmt->call;
      int func_reg = gen_expr(cg, call->func);
      ExprList *arg;
      int nargs = 0;
      int base = func_reg;
      
      /* Generate code for arguments */
      for (arg = call->args; arg != NULL; arg = arg->next) {
        gen_expr(cg, arg->expr);
        nargs++;
      }
      
      /* OP_CALL with C=1 discards all results */
      code_instruction(cg, CREATE_ABCk(OP_CALL, base, nargs + 1, 1, 0), stmt->header.line);
      cg->freereg = base;  /* Reset to before call */
      break;
    }
    case AST_LABEL:
    case AST_GOTO:
      /* TODO: implement labels and gotos */
      break;
    default:
      luaG_runerror(cg->L, "codegen: unsupported statement kind %d", node->kind);
      break;
  }
}


/*
** Generate code for statement list
*/
static void gen_statlist (CodeGenState *cg, StmtList *list) {
  for (; list != NULL; list = list->next) {
    gen_statement(cg, list->stmt);
  }
}


/*
** Generate code for chunk/block
*/
static void gen_chunk (CodeGenState *cg, AstNode *node) {
  if (node == NULL)
    return;
  
  if (node->kind == AST_CHUNK || node->kind == AST_BLOCK) {
    AstBlock *block = (AstBlock *)node;
    gen_statlist(cg, block->statements);
  }
}


/*
** Code generator entry point
*/
LClosure *luaY_generate (lua_State *L, AST *ast, 
                         Dyndata *dyd, const char *name) {
  CodeGenState cg;
  LClosure *cl;
  Proto *f;
  
  (void)dyd;  /* Not used yet */
  
  /* Create closure and proto */
  cl = luaF_newLclosure(L, 1);  /* 1 upvalue for _ENV */
  setclLvalue2s(L, L->top.p, cl);
  luaD_inctop(L);
  
  f = cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);
  f->source = luaS_new(L, name);
  luaC_objbarrier(L, f, f->source);
  
  /* Setup _ENV upvalue */
  f->sizeupvalues = 1;
  f->upvalues = luaM_newvector(L, 1, Upvaldesc);
  f->upvalues[0].instack = 1;
  f->upvalues[0].idx = 0;
  f->upvalues[0].kind = VDKREG;
  f->upvalues[0].name = luaS_newliteral(L, "_ENV");
  luaC_objbarrier(L, f, f->upvalues[0].name);
  
  /* Initialize code generation state */
  cg.f = f;
  cg.L = L;
  cg.pc = 0;
  cg.nk = 0;
  cg.freereg = 0;
  cg.locals = NULL;
  cg.nlocals = 0;
  cg.sizelocals = 0;
  
  /* Set function properties */
  f->numparams = 0;
  f->flag = PF_ISVARARG;  /* Main function is always vararg */
  f->maxstacksize = 2;  /* Minimum stack size */
  
  /* Emit VARARGPREP for vararg functions */
  code_instruction(&cg, CREATE_ABCk(OP_VARARGPREP, 0, 0, 0, 0), 0);
  
  /* Generate code from AST */
  gen_chunk(&cg, ast->root);
  
  /* Add final RETURN if not present */
  if (cg.pc == 0 || GET_OPCODE(f->code[cg.pc - 1]) != OP_RETURN) {
    code_instruction(&cg, CREATE_ABx(OP_RETURN, 0, 1), 0);
  }
  
  /* Update max stack size based on usage */
  if (cg.freereg > f->maxstacksize)
    f->maxstacksize = cg.freereg;
  
  /* Cleanup locals array */
  if (cg.locals)
    luaM_free(L, cg.locals);
  
  return cl;
}
