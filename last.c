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
    if (newsize == 0) newsize = 32;
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
    case TK_IF:      /* TODO: implement */
    case TK_FUNCTION: /* TODO: implement */
    case TK_LOCAL: {  /* TODO: implement */
      luaX_next(as->ls);
      as->ls->L->nCcalls--;
      return NULL;  /* stub */
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
    default: {
      /* TODO: expression statement or assignment */
      as->ls->L->nCcalls--;
      (void)line;
      return NULL;  /* stub */
    }
  }
}


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
    default: {
      /* TODO: variables, function calls, etc. */
      luaX_syntaxerror(ls, "unexpected symbol in expression");
      return NULL;
    }
  }
}

/*
** Parse expression
*/
static AstNode *parse_expr (lua_State *L, AstState *as) {
  /* For now, just parse simple expressions */
  return parse_simple_expr(L, as);
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
  ls.buff = lex_buff;  /* Must set buff BEFORE luaX_setinput */
  luaX_setinput(L, &ls, z, luaS_new(L, name), firstchar);
  
  /* AstState 초기화 (이미 초기화된 아레나 사용) */
  as.ls = &ls;
  as.arena = ast_arena;
  
  /* Read first token (from lparser.c mainfunc) */
  luaX_next(&ls);
  /* 파싱 수행 (에러 시 longjmp, 정리는 luaD_protectedparser가 함) */
  root = parse_chunk(L, &as);
  
  /* AST 구조체 생성 */
  ast = luaM_new(L, AST);
  ast->arena = ast_arena;  /* 포인터 참조 (소유 안 함!) */
  ast->root = root;
  
  return ast;
}
