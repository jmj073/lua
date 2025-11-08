/*
** $Id: last.c $
** AST (Abstract Syntax Tree) generation
** See Copyright Notice in lua.h
*/

#define last_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>
#include <string.h>

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "last.h"


/*
** Arena allocation helper
*/
static void *arena_alloc (lua_State *L, AstState *as, size_t size) {
  Mbuffer *b = as->arena;
  size_t oldsize = luaZ_bufflen(b);
  void *ptr;
  if (oldsize + size > luaZ_sizebuffer(b)) {
    size_t newsize = luaZ_sizebuffer(b);
    if (newsize == 0) newsize = 4096;  /* Start with 4KB */
    while (newsize < oldsize + size)
      newsize *= 2;
    luaZ_resizebuffer(L, b, newsize);
  }
  ptr = b->buffer + oldsize;
  b->n = oldsize + size;
  return ptr;
}


/*
** Error handling (similar to lparser.c)
*/
static l_noret ast_error_expected (LexState *ls, int token) {
  luaX_syntaxerror(ls,
      luaO_pushfstring(ls->L, "%s expected", luaX_token2str(ls, token)));
}


/*
** Token checking utilities (from lparser.c)
*/
static int ast_testnext (LexState *ls, int c) {
  if (ls->t.token == c) {
    luaX_next(ls);
    return 1;
  }
  return 0;
}

static void ast_check (LexState *ls, int c) {
  if (ls->t.token != c)
    ast_error_expected(ls, c);
}

static void ast_checknext (LexState *ls, int c) {
  ast_check(ls, c);
  luaX_next(ls);
}

static void ast_check_match (LexState *ls, int what, int who, int where) {
  if (l_unlikely(!ast_testnext(ls, what))) {
    if (where == ls->linenumber)
      ast_error_expected(ls, what);
    else {
      luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
             "%s expected (to close %s at line %d)",
              luaX_token2str(ls, what), luaX_token2str(ls, who), where));
    }
  }
}

static TString *ast_str_checkname (LexState *ls) {
  TString *ts;
  ast_check(ls, TK_NAME);
  ts = ls->t.seminfo.ts;
  luaX_next(ls);
  return ts;
}


/*
** Check whether current token is in the follow set of a block
** (from lparser.c)
** Note: '}' is a character token, TK_TBE is for table literals {| |}
*/
static int ast_block_follow (LexState *ls) {
  switch (ls->t.token) {
    case '}': case TK_EOS:
      return 1;
    default: return 0;
  }
}


/*
** Forward declarations
*/
static AstNode *parse_simple_expr (lua_State *L, AstState *as);
static AstNode *parse_expr (lua_State *L, AstState *as);
static AstNode *parse_table_constructor (lua_State *L, AstState *as);

/*
** Reverse linked list
*/
static StmtList *reverse_stmtlist (StmtList *head) {
  StmtList *prev = NULL;
  while (head) {
    StmtList *next = head->next;
    head->next = prev;
    prev = head;
    head = next;
  }
  return prev;
}

static ExprList *reverse_exprlist (ExprList *head) {
  ExprList *prev = NULL;
  while (head) {
    ExprList *next = head->next;
    head->next = prev;
    prev = head;
    head = next;
  }
  return prev;
}


/*
** Forward declarations
*/
static AstNode *parse_chunk (lua_State *L, AstState *as);
static AstNode *parse_block (lua_State *L, AstState *as);
static AstNode *parse_statement (lua_State *L, AstState *as);
static AstNode *parse_expr (lua_State *L, AstState *as);


/*
** Parse statement list (similar to statlist in lparser.c)
*/
static StmtList *parse_statlist (lua_State *L, AstState *as) {
  StmtList *list = NULL;
  AstNode *stmt;
  StmtList *node;
  /* statlist -> {stat [';']} */
  while (!ast_block_follow(as->ls)) {
    if (as->ls->t.token == TK_RETURN) {
      stmt = parse_statement(L, as);
      /* Add to list */
      node = arena_alloc(L, as, sizeof(StmtList));
      node->stmt = stmt;
      node->next = list;
      list = node;
      return reverse_stmtlist(list);  /* 'return' must be last */
    }
    stmt = parse_statement(L, as);
    if (stmt) {  /* skip empty statements */
      node = arena_alloc(L, as, sizeof(StmtList));
      node->stmt = stmt;
      node->next = list;
      list = node;
    }
    /* If we didn't consume a token, we're stuck! */
    if (stmt == NULL && !ast_testnext(as->ls, ';')) {
      luaX_syntaxerror(as->ls, "unexpected symbol");
    }
  }
  return reverse_stmtlist(list);
}


/*
** Parse chunk (root)
*/
static AstNode *parse_chunk (lua_State *L, AstState *as) {
  AstBlock *block = arena_alloc(L, as, sizeof(AstBlock));
  block->header.kind = AST_CHUNK;
  block->header.line = as->ls->linenumber;
  block->statements = parse_statlist(L, as);
  return (AstNode *)block;
}


/*
** Parse block (do...end)
*/
static AstNode *parse_block (lua_State *L, AstState *as) {
  AstBlock *block = arena_alloc(L, as, sizeof(AstBlock));
  block->header.kind = AST_BLOCK;
  block->header.line = as->ls->linenumber;
  block->statements = parse_statlist(L, as);
  return (AstNode *)block;
}


/*
** Parse simple statements (stubs for now)
*/
static AstNode *parse_label_stmt (lua_State *L, AstState *as) {
  AstLabel *label = arena_alloc(L, as, sizeof(AstLabel));
  label->header.kind = AST_LABEL;
  label->header.line = as->ls->linenumber;
  luaX_next(as->ls);  /* skip '::' */
  label->name = ast_str_checkname(as->ls);
  ast_checknext(as->ls, TK_DBCOLON);  /* skip closing '::' */
  return (AstNode *)label;
}

static AstNode *parse_goto_stmt (lua_State *L, AstState *as) {
  AstGoto *gt = arena_alloc(L, as, sizeof(AstGoto));
  gt->header.kind = AST_GOTO;
  gt->header.line = as->ls->linenumber;
  luaX_next(as->ls);  /* skip 'goto' */
  gt->name = ast_str_checkname(as->ls);
  return (AstNode *)gt;
}

static AstNode *parse_do_stmt (lua_State *L, AstState *as) {
  AstNode *block;
  int line = as->ls->linenumber;
  (void)line;  /* unused for now */
  luaX_next(as->ls);  /* skip 'do' */
  /* TODO: handle block syntax (curly braces or other) */
  block = parse_block(L, as);
  return block;
}

static NameList *reverse_namelist (NameList *head) {
  NameList *prev = NULL;
  while (head) {
    NameList *next = head->next;
    head->next = prev;
    prev = head;
    head = next;
  }
  return prev;
}

static AstNode *parse_local_stmt (lua_State *L, AstState *as) {
  AstLocalDecl *local;
  NameList *names = NULL;
  ExprList *values = NULL;
  
  local = arena_alloc(L, as, sizeof(AstLocalDecl));
  local->header.kind = AST_LOCAL_DECL;
  local->header.line = as->ls->linenumber;
  
  luaX_next(as->ls);  /* skip 'local' */
  
  /* Parse name list */
  do {
    NameList *name = arena_alloc(L, as, sizeof(NameList));
    name->name = ast_str_checkname(as->ls);
    name->next = names;
    names = name;
    
    /* Skip optional attribute <const> or <close> */
    if (as->ls->t.token == '<') {
      luaX_next(as->ls);
      ast_str_checkname(as->ls);  /* skip attribute name */
      ast_checknext(as->ls, '>');
    }
  } while (ast_testnext(as->ls, ','));
  
  /* Parse optional initialization */
  if (ast_testnext(as->ls, '=')) {
    /* Parse expression list */
    AstNode *expr = parse_expr(L, as);
    ExprList *node = arena_alloc(L, as, sizeof(ExprList));
    node->expr = expr;
    node->next = values;
    values = node;
    
    while (ast_testnext(as->ls, ',')) {
      expr = parse_expr(L, as);
      node = arena_alloc(L, as, sizeof(ExprList));
      node->expr = expr;
      node->next = values;
      values = node;
    }
    
    local->values = reverse_exprlist(values);
  } else {
    local->values = NULL;
  }
  
  local->names = reverse_namelist(names);
  
  return (AstNode *)local;
}

static AstNode *parse_if_stmt (lua_State *L, AstState *as) {
  AstIf *ifstmt;
  IfClause *clauses = NULL;
  IfClause *clause;
  
  ifstmt = arena_alloc(L, as, sizeof(AstIf));
  ifstmt->header.kind = AST_IF;
  ifstmt->header.line = as->ls->linenumber;
  
  /* Parse 'if' clause */
  luaX_next(as->ls);  /* skip 'if' */
  clause = arena_alloc(L, as, sizeof(IfClause));
  clause->condition = parse_expr(L, as);
  ast_checknext(as->ls, '{');
  clause->body = parse_block(L, as);
  clause->next = clauses;
  clauses = clause;
  
  /* Parse 'elseif' clauses */
  while (as->ls->t.token == TK_ELSEIF) {
    luaX_next(as->ls);  /* skip 'elseif' */
    clause = arena_alloc(L, as, sizeof(IfClause));
    clause->condition = parse_expr(L, as);
    ast_checknext(as->ls, '{');
    clause->body = parse_block(L, as);
    clause->next = clauses;
    clauses = clause;
  }
  
  /* Parse optional 'else' clause */
  if (as->ls->t.token == TK_ELSE) {
    luaX_next(as->ls);  /* skip 'else' */
    clause = arena_alloc(L, as, sizeof(IfClause));
    clause->condition = NULL;  /* else has no condition */
    ast_checknext(as->ls, '{');
    clause->body = parse_block(L, as);
    clause->next = clauses;
    clauses = clause;
  }
  
  /* Reverse clauses list to maintain order */
  {
    IfClause *prev = NULL;
    while (clauses) {
      IfClause *next = clauses->next;
      clauses->next = prev;
      prev = clauses;
      clauses = next;
    }
    ifstmt->clauses = prev;
  }
  
  return (AstNode *)ifstmt;
}

static AstNode *parse_return_stmt (lua_State *L, AstState *as) {
  AstReturn *ret = arena_alloc(L, as, sizeof(AstReturn));
  ExprList *list = NULL;
  ret->header.kind = AST_RETURN;
  ret->header.line = as->ls->linenumber;
  luaX_next(as->ls);  /* skip 'return' */
  
  /* Parse optional expression list */
  if (!ast_block_follow(as->ls) && as->ls->t.token != ';') {
    /* Parse first expression */
    AstNode *expr = parse_expr(L, as);
    ExprList *node = arena_alloc(L, as, sizeof(ExprList));
    node->expr = expr;
    node->next = list;
    list = node;
    
    /* Parse additional expressions */
    while (ast_testnext(as->ls, ',')) {
      expr = parse_expr(L, as);
      node = arena_alloc(L, as, sizeof(ExprList));
      node->expr = expr;
      node->next = list;
      list = node;
    }
    
    /* Reverse to maintain order */
    ret->exprs = reverse_exprlist(list);
  } else {
    ret->exprs = NULL;
  }
  
  ast_testnext(as->ls, ';');  /* skip optional semicolon */
  return (AstNode *)ret;
}


/*
** Parse statement (simplified version)
** Note: This Lua variant has no while/for/break loops (use recursion instead)
*/
static AstNode *parse_statement (lua_State *L, AstState *as) {
  int line = as->ls->linenumber;
  as->ls->L->nCcalls++;  /* control recursion */
  switch (as->ls->t.token) {
    case ';': {  /* empty statement */
      luaX_next(as->ls);
      as->ls->L->nCcalls--;
      return NULL;
    }
    case '{': {
      as->ls->L->nCcalls--;
      return NULL;
    }
    case TK_LOCAL: {
      /* Check if it's local function or local variable */
      int next_token = luaX_lookahead(as->ls);
      if (next_token == TK_FUNCTION) {
        /* local fn name(params) { body } */
        AstLocalFuncStmt *stmt;
        AstFunction *func;
        NameList *params = NULL;
        TString *name;
        
        stmt = arena_alloc(L, as, sizeof(AstLocalFuncStmt));
        stmt->header.kind = AST_LOCAL_FUNC_STMT;
        stmt->header.line = as->ls->linenumber;
        
        luaX_next(as->ls);  /* skip 'local' */
        luaX_next(as->ls);  /* skip 'fn' */
        
        name = ast_str_checkname(as->ls);
        stmt->name = name;
        
        /* Parse function */
        func = arena_alloc(L, as, sizeof(AstFunction));
        func->header.kind = AST_FUNCTION;
        func->header.line = as->ls->linenumber;
        func->is_vararg = 0;
        
        ast_checknext(as->ls, '(');
        
        /* Parse parameters */
        if (as->ls->t.token != ')') {
          do {
            if (as->ls->t.token == TK_DOTS) {
              func->is_vararg = 1;
              luaX_next(as->ls);
              break;
            } else {
              NameList *param = arena_alloc(L, as, sizeof(NameList));
              param->name = ast_str_checkname(as->ls);
              param->next = params;
              params = param;
            }
          } while (ast_testnext(as->ls, ','));
        }
        
        ast_checknext(as->ls, ')');
        func->params = reverse_namelist(params);
        
        /* Parse body */
        ast_checknext(as->ls, '{');
        func->body = parse_block(L, as);
        
        stmt->func = func;
        as->ls->L->nCcalls--;
        return (AstNode *)stmt;
      } else {
        as->ls->L->nCcalls--;
        return parse_local_stmt(L, as);
      }
    }
    case TK_FUNCTION: {
      /* fn name(params) { body } */
      AstFunctionStmt *stmt;
      AstFunction *func;
      NameList *params = NULL;
      TString *name;
      
      stmt = arena_alloc(L, as, sizeof(AstFunctionStmt));
      stmt->header.kind = AST_FUNCTION_STMT;
      stmt->header.line = as->ls->linenumber;
      
      luaX_next(as->ls);  /* skip 'fn' */
      
      name = ast_str_checkname(as->ls);
      stmt->name = name;
      
      /* Parse function */
      func = arena_alloc(L, as, sizeof(AstFunction));
      func->header.kind = AST_FUNCTION;
      func->header.line = as->ls->linenumber;
      func->is_vararg = 0;
      
      ast_checknext(as->ls, '(');
      
      /* Parse parameters */
      if (as->ls->t.token != ')') {
        do {
          if (as->ls->t.token == TK_DOTS) {
            func->is_vararg = 1;
            luaX_next(as->ls);
            break;
          } else {
            NameList *param = arena_alloc(L, as, sizeof(NameList));
            param->name = ast_str_checkname(as->ls);
            param->next = params;
            params = param;
          }
        } while (ast_testnext(as->ls, ','));
      }
      
      ast_checknext(as->ls, ')');
      func->params = reverse_namelist(params);
      
      /* Parse body */
      ast_checknext(as->ls, '{');
      func->body = parse_block(L, as);
      
      stmt->func = func;
      as->ls->L->nCcalls--;
      return (AstNode *)stmt;
    }
    case TK_DBCOLON: {
      as->ls->L->nCcalls--;
      return parse_label_stmt(L, as);
    }
    case TK_GOTO: {
      as->ls->L->nCcalls--;
      return parse_goto_stmt(L, as);
    }
    case TK_RETURN: {
      as->ls->L->nCcalls--;
      return parse_return_stmt(L, as);
    }
    case TK_IF: {
      as->ls->L->nCcalls--;
      return parse_if_stmt(L, as);
    }
    default: {
      /* Try parsing as expression statement (function call or assignment) */
      AstNode *expr = parse_simple_expr(L, as);  /* Parse first variable or call */
      
      /* Check for assignment: var = expr or var, var2 = expr1, expr2 */
      if (as->ls->t.token == '=' || as->ls->t.token == ',') {
        /* This is an assignment */
        AstAssignment *assign = arena_alloc(L, as, sizeof(AstAssignment));
        NameList *vars = NULL;
        ExprList *values = NULL;
        
        assign->header.kind = AST_ASSIGNMENT;
        assign->header.line = line;
        
        /* First variable must be a simple name */
        if (expr->kind != AST_VAR) {
          luaX_syntaxerror(as->ls, "syntax error");
          as->ls->L->nCcalls--;
          return NULL;
        }
        
        /* Build variable list */
        do {
          NameList *var = arena_alloc(L, as, sizeof(NameList));
          if (expr->kind == AST_VAR) {
            var->name = ((AstVar *)expr)->name;
          } else {
            luaX_syntaxerror(as->ls, "syntax error");
          }
          var->next = vars;
          vars = var;
          
          if (ast_testnext(as->ls, ',')) {
            expr = parse_simple_expr(L, as);
          } else {
            break;
          }
        } while (1);
        
        /* Expect '=' */
        ast_checknext(as->ls, '=');
        
        /* Parse expression list */
        do {
          AstNode *val = parse_expr(L, as);
          ExprList *node = arena_alloc(L, as, sizeof(ExprList));
          node->expr = val;
          node->next = values;
          values = node;
        } while (ast_testnext(as->ls, ','));
        
        assign->vars = reverse_namelist(vars);
        assign->values = reverse_exprlist(values);
        
        as->ls->L->nCcalls--;
        return (AstNode *)assign;
      }
      
      /* Check for function call - need to parse the rest */
      if (as->ls->t.token == '(') {
        /* This is a function call, need to re-parse as full expression */
        /* For simplicity, handle direct calls for now */
        if (expr->kind == AST_VAR) {
          AstCall *call = arena_alloc(L, as, sizeof(AstCall));
          ExprList *args = NULL;
          
          call->header.kind = AST_CALL;
          call->header.line = expr->line;
          call->func = expr;
          
          /* Parse arguments */
          luaX_next(as->ls);  /* skip '(' */
          if (as->ls->t.token != ')') {
            do {
              AstNode *arg = parse_expr(L, as);
              ExprList *node = arena_alloc(L, as, sizeof(ExprList));
              node->expr = arg;
              node->next = args;
              args = node;
            } while (ast_testnext(as->ls, ','));
          }
          ast_checknext(as->ls, ')');
          
          call->args = reverse_exprlist(args);
          
          /* Create function call statement */
          AstFuncCallStmt *stmt = arena_alloc(L, as, sizeof(AstFuncCallStmt));
          stmt->header.kind = AST_FUNCCALL_STMT;
          stmt->header.line = call->header.line;
          stmt->call = (AstNode *)call;
          
          as->ls->L->nCcalls--;
          return (AstNode *)stmt;
        }
      }
      
      /* Not a valid statement */
      as->ls->L->nCcalls--;
      luaX_syntaxerror(as->ls, "syntax error");
      return NULL;
    }
  }
}


/*
** Get binary operator
*/
static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case '/': return OPR_DIV;
    case TK_IDIV: return OPR_IDIV;
    case '&': return OPR_BAND;
    case '|': return OPR_BOR;
    case '~': return OPR_BXOR;
    case TK_SHL: return OPR_SHL;
    case TK_SHR: return OPR_SHR;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}

/*
** Get unary operator
*/
static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '~': return OPR_BNOT;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}

/*
** Priority table for binary operators
*/
static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {10, 10}, {10, 10},           /* '+' '-' */
   {11, 11}, {11, 11},           /* '*' '%' */
   {14, 13},                     /* '^' (right associative) */
   {11, 11}, {11, 11},           /* '/' '//' */
   {6, 6}, {4, 4}, {5, 5},       /* '&' '|' '~' */
   {7, 7}, {7, 7},               /* '<<' '>>' */
   {9, 8},                       /* '..' (right associative) */
   {3, 3}, {3, 3}, {3, 3},       /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},       /* ~=, >, >= */
   {2, 2}, {1, 1}                /* and, or */
};

#define UNARY_PRIORITY 12  /* priority for unary operators */

/*
** Forward declaration
*/
static AstNode *parse_subexpr (lua_State *L, AstState *as, int limit);

/*
** Parse simple expression (literals and basic expressions)
*/
static AstNode *parse_simple_expr (lua_State *L, AstState *as) {
  LexState *ls = as->ls;
  switch (ls->t.token) {
    case TK_NIL: {
      AstNode *node = arena_alloc(L, as, sizeof(AstNode));
      node->kind = AST_NIL;
      node->line = ls->linenumber;
      luaX_next(ls);
      return node;
    }
    case TK_TRUE: {
      AstNode *node = arena_alloc(L, as, sizeof(AstNode));
      node->kind = AST_TRUE;
      node->line = ls->linenumber;
      luaX_next(ls);
      return node;
    }
    case TK_FALSE: {
      AstNode *node = arena_alloc(L, as, sizeof(AstNode));
      node->kind = AST_FALSE;
      node->line = ls->linenumber;
      luaX_next(ls);
      return node;
    }
    case TK_FLT: {
      AstNumeral *node = arena_alloc(L, as, sizeof(AstNumeral));
      node->header.kind = AST_NUMERAL;
      node->header.line = ls->linenumber;
      node->value = ls->t.seminfo.r;
      luaX_next(ls);
      return (AstNode *)node;
    }
    case TK_INT: {
      AstNumeral *node = arena_alloc(L, as, sizeof(AstNumeral));
      node->header.kind = AST_NUMERAL;
      node->header.line = ls->linenumber;
      node->value = (lua_Number)ls->t.seminfo.i;
      luaX_next(ls);
      return (AstNode *)node;
    }
    case TK_STRING: {
      AstString *node = arena_alloc(L, as, sizeof(AstString));
      node->header.kind = AST_STRING;
      node->header.line = ls->linenumber;
      node->str = ls->t.seminfo.ts;
      luaX_next(ls);
      return (AstNode *)node;
    }
    case TK_DOTS: {
      AstNode *node = arena_alloc(L, as, sizeof(AstNode));
      node->kind = AST_VARARG;
      node->line = ls->linenumber;
      luaX_next(ls);
      return node;
    }
    case TK_NAME: {
      AstVar *var = arena_alloc(L, as, sizeof(AstVar));
      var->header.kind = AST_VAR;
      var->header.line = ls->linenumber;
      var->name = ast_str_checkname(ls);
      luaX_next(ls);
      
      AstNode *base;
      base = (AstNode *)var;
      
      /* Check for table indexing or method call */
      while (ls->t.token == '[' || ls->t.token == '.') {
        if (ls->t.token == '[') {
          /* t[key] */
          AstIndexing *idx = arena_alloc(L, as, sizeof(AstIndexing));
          idx->header.kind = AST_INDEXING;
          idx->header.line = ls->linenumber;
          idx->table = base;
          luaX_next(ls);  /* skip '[' */
          idx->index = parse_expr(L, as);
          ast_checknext(ls, ']');
          base = (AstNode *)idx;
        } else {  /* '.' */
          /* t.field → t["field"] */
          AstIndexing *idx = arena_alloc(L, as, sizeof(AstIndexing));
          AstString *key;
          idx->header.kind = AST_INDEXING;
          idx->header.line = ls->linenumber;
          idx->table = base;
          luaX_next(ls);  /* skip '.' */
          key = arena_alloc(L, as, sizeof(AstString));
          key->header.kind = AST_STRING;
          key->header.line = ls->linenumber;
          key->str = ast_str_checkname(ls);
          idx->index = (AstNode *)key;
          base = (AstNode *)idx;
        }
      }
      
      /* Check for function call */
      if (ls->t.token == '(') {
        AstCall *call = arena_alloc(L, as, sizeof(AstCall));
        ExprList *args = NULL;
        call->header.kind = AST_CALL;
        call->header.line = ls->linenumber;
        call->func = base;
        
        luaX_next(ls);  /* skip '(' */
        
        /* Parse arguments */
        if (ls->t.token != ')') {
          AstNode *arg = parse_expr(L, as);
          ExprList *node = arena_alloc(L, as, sizeof(ExprList));
          node->expr = arg;
          node->next = args;
          args = node;
          
          while (ast_testnext(ls, ',')) {
            arg = parse_expr(L, as);
            node = arena_alloc(L, as, sizeof(ExprList));
            node->expr = arg;
            node->next = args;
            args = node;
          }
          
          call->args = reverse_exprlist(args);
        } else {
          call->args = NULL;
        }
        
        ast_checknext(ls, ')');
        return (AstNode *)call;
      }
      
      return base;
    }
    case TK_TBS: {  /* Table constructor {| ... |} */
      return parse_table_constructor(L, as);
    }
    case TK_FUNCTION: {  /* Function definition: fn (params) { body } */
      AstFunction *func;
      NameList *params = NULL;
      int line = ls->linenumber;
      
      func = arena_alloc(L, as, sizeof(AstFunction));
      func->header.kind = AST_FUNCTION;
      func->header.line = line;
      func->is_vararg = 0;
      
      luaX_next(ls);  /* skip 'fn' */
      ast_checknext(ls, '(');
      
      /* Parse parameter list */
      if (ls->t.token != ')') {
        do {
          if (ls->t.token == TK_DOTS) {
            func->is_vararg = 1;
            luaX_next(ls);
            break;
          } else {
            NameList *param = arena_alloc(L, as, sizeof(NameList));
            param->name = ast_str_checkname(ls);
            param->next = params;
            params = param;
          }
        } while (ast_testnext(ls, ','));
      }
      
      ast_checknext(ls, ')');
      
      /* Reverse params list */
      func->params = reverse_namelist(params);
      
      /* Parse body */
      ast_checknext(ls, '{');
      func->body = parse_block(L, as);
      
      return (AstNode *)func;
    }
    default: {
      luaX_syntaxerror(ls, "unexpected symbol in expression");
      return NULL;
    }
  }
}

/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where 'binop' is any binary operator with a priority higher than 'limit'
*/
static AstNode *parse_subexpr (lua_State *L, AstState *as, int limit) {
  AstNode *left;
  BinOpr op;
  UnOpr uop;
  
  /* Check for unary operator */
  uop = getunopr(as->ls->t.token);
  if (uop != OPR_NOUNOPR) {
    int line = as->ls->linenumber;
    luaX_next(as->ls);  /* skip operator */
    left = parse_subexpr(L, as, UNARY_PRIORITY);
    /* Create unary op node */
    AstUnOp *unop = arena_alloc(L, as, sizeof(AstUnOp));
    unop->header.kind = AST_UNOP;
    unop->header.line = line;
    unop->op = uop;
    unop->operand = left;
    left = (AstNode *)unop;
  } else {
    left = parse_simple_expr(L, as);
  }
  
  /* Parse binary operators with precedence climbing */
  op = getbinopr(as->ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    int line = as->ls->linenumber;
    AstNode *right;
    AstBinOp *binop;
    
    luaX_next(as->ls);  /* skip operator */
    /* Read right operand with higher priority */
    right = parse_subexpr(L, as, priority[op].right);
    
    /* Create binary op node */
    binop = arena_alloc(L, as, sizeof(AstBinOp));
    binop->header.kind = AST_BINOP;
    binop->header.line = line;
    binop->op = op;
    binop->left = left;
    binop->right = right;
    left = (AstNode *)binop;
    
    op = getbinopr(as->ls->t.token);
  }
  
  return left;
}

/*
** Parse table constructor: {| [field {sep field} [sep]] |}
*/
static AstNode *parse_table_constructor (lua_State *L, AstState *as) {
  AstTableCtor *tctor;
  TableField *fields = NULL;
  int line = as->ls->linenumber;
  
  tctor = arena_alloc(L, as, sizeof(AstTableCtor));
  tctor->header.kind = AST_TABLECTOR;
  tctor->header.line = line;
  
  luaX_next(as->ls);  /* skip TK_TBS '{|' */
  
  /* Parse fields */
  while (as->ls->t.token != TK_TBE) {
    TableField *field = arena_alloc(L, as, sizeof(TableField));
    
    /* Check field type */
    if (as->ls->t.token == '[') {
      /* [exp] = exp */
      luaX_next(as->ls);  /* skip '[' */
      field->kind = FIELD_REC;
      field->key = parse_expr(L, as);
      ast_checknext(as->ls, ']');
      ast_checknext(as->ls, '=');
      field->value = parse_expr(L, as);
    } else if (as->ls->t.token == TK_NAME) {
      /* Could be: Name = exp  or  exp */
      int next_token = luaX_lookahead(as->ls);
      if (next_token == '=') {
        /* Name = exp */
        field->kind = FIELD_REC;
        TString *name = ast_str_checkname(as->ls);
        AstVar *key = arena_alloc(L, as, sizeof(AstVar));
        key->header.kind = AST_STRING;  /* Use string for key */
        key->header.line = as->ls->linenumber;
        AstString *keystr = arena_alloc(L, as, sizeof(AstString));
        keystr->header.kind = AST_STRING;
        keystr->header.line = as->ls->linenumber;
        keystr->str = name;
        field->key = (AstNode *)keystr;
        ast_checknext(as->ls, '=');
        field->value = parse_expr(L, as);
      } else {
        /* exp (list field) */
        field->kind = FIELD_LIST;
        field->key = NULL;
        field->value = parse_expr(L, as);
      }
    } else {
      /* exp (list field) */
      field->kind = FIELD_LIST;
      field->key = NULL;
      field->value = parse_expr(L, as);
    }
    
    field->next = fields;
    fields = field;
    
    /* Check separator */
    if (as->ls->t.token == ',' || as->ls->t.token == ';') {
      luaX_next(as->ls);
    } else {
      break;
    }
  }
  
  ast_checknext(as->ls, TK_TBE);  /* expect '|}' */
  
  /* Reverse fields list */
  {
    TableField *prev = NULL;
    while (fields) {
      TableField *next = fields->next;
      fields->next = prev;
      prev = fields;
      fields = next;
    }
    tctor->fields = prev;
  }
  
  return (AstNode *)tctor;
}

/*
** Parse expression
*/
static AstNode *parse_expr (lua_State *L, AstState *as) {
  return parse_subexpr(L, as, 0);  /* Parse with lowest priority */
}


/*
** Main AST parser entry point
*/
AST *luaA_parser (lua_State *L, ZIO *z,
                  Mbuffer *lex_buff, Mbuffer *ast_arena,
                  const char *name, int firstchar) {
  LexState ls;
  AstState as;
  AST *ast;
  AstNode *root;
  
  /* lexer 초기화 (이미 초기화된 버퍼 사용) */
  ls.h = luaH_new(L);  /* create table for scanner */
  sethvalue2s(L, L->top.p, ls.h);  /* anchor it */
  luaD_inctop(L);
  ls.buff = lex_buff;  /* Must set buff BEFORE luaX_setinput */
  luaX_setinput(L, &ls, z, luaS_new(L, name), firstchar);
  
  /* AstState 초기화 (이미 초기화된 아레나 사용) */
  as.ls = &ls;
  as.arena = ast_arena;
  
  /* Read first token (from lparser.c mainfunc) */
  luaX_next(&ls);
  /* 파싱 수행 (에러 시 longjmp, 정리는 luaD_protectedparser가 함) */
  root = parse_chunk(L, &as);
  
  /* AST 구조체 생성 및 anchor */
  ast = luaM_new(L, AST);
  ast->arena = ast_arena;  /* 포인터 참조 (소유 안 함!) */
  ast->root = root;
  
  /* Push AST root as userdata to anchor it (prevent GC) */
  lua_pushlightuserdata(L, ast);
  
  return ast;
}
