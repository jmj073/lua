/*
** $Id: lcodegen.c $
** Code generator from AST
** See Copyright Notice in lua.h
*/

#define lcodegen_c
#define LUA_CORE

#include "lprefix.h"

#include <limits.h>
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
** Code generation state (similar to FuncState in lparser)
*/
typedef struct CodeGenState {
  Proto *f;              /* current function header */
  lua_State *L;
  int pc;                /* next position to code */
  int freereg;           /* first free register */
} CodeGenState;


/*
** Forward declarations
*/
static void gen_chunk (CodeGenState *cg, AstNode *node);
static void gen_statement (CodeGenState *cg, AstNode *node);
static int gen_expr (CodeGenState *cg, AstNode *node);


/*
** Add constant to Proto
*/
static int addk (CodeGenState *cg, TValue *v) {
  Proto *f = cg->f;
  int i;
  /* Check if constant already exists */
  for (i = 0; i < f->sizek; i++) {
    if (luaV_rawequalobj(&f->k[i], v))
      return i;
  }
  /* Add new constant */
  luaM_growvector(cg->L, f->k, f->sizek, f->sizek, TValue, MAXARG_Ax, "constants");
  setobj(cg->L, &f->k[f->sizek], v);
  return f->sizek++;
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
    case AST_NIL: {
      code_instruction(cg, CREATE_ABx(OP_LOADNIL, reg, 0), node->line);
      break;
    }
    case AST_TRUE: {
      code_instruction(cg, CREATE_Ax(OP_LOADTRUE, reg), node->line);
      break;
    }
    case AST_FALSE: {
      code_instruction(cg, CREATE_Ax(OP_LOADFALSE, reg), node->line);
      break;
    }
    case AST_NUMERAL: {
      AstNumeral *num = (AstNumeral *)node;
      lua_Integer i;
      /* Try to encode as integer */
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
** Generate code for statement
*/
static void gen_statement (CodeGenState *cg, AstNode *node) {
  if (node == NULL)
    return;
  
  switch (node->kind) {
    case AST_RETURN: {
      gen_return(cg, (AstReturn *)node);
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
  
  /* Initialize code generation state */
  cg.f = f;
  cg.L = L;
  cg.pc = 0;
  cg.freereg = 0;
  
  /* Set function properties */
  f->numparams = 0;
  f->maxstacksize = 2;  /* Minimum stack size */
  
  /* Generate code from AST */
  gen_chunk(&cg, ast->root);
  
  /* Add final RETURN if not present */
  if (cg.pc == 0 || GET_OPCODE(f->code[cg.pc - 1]) != OP_RETURN) {
    code_instruction(&cg, CREATE_ABx(OP_RETURN, 0, 1), 0);
  }
  
  /* Update max stack size based on usage */
  if (cg.freereg > f->maxstacksize)
    f->maxstacksize = cg.freereg + 1;
  
  return cl;
}
