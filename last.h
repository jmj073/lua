/*
** $Id: last.h $
** AST (Abstract Syntax Tree) generation
** See Copyright Notice in lua.h
*/

#ifndef last_h
#define last_h

#include "lobject.h"
#include "lzio.h"


/*
** AST Node Types - BNF에 맞게 분리
*/
typedef enum {
  /* Expressions */
  AST_NIL,
  AST_TRUE,
  AST_FALSE,
  AST_NUMERAL,
  AST_STRING,
  AST_VARARG,
  AST_VAR,
  AST_BINOP,
  AST_UNOP,
  AST_FUNCTION,
  AST_TABLE,
  AST_INDEX,
  AST_CALL,
  
  /* Statements */
  AST_ASSIGNMENT,
  AST_FUNCCALL_STMT,
  AST_LABEL,
  AST_BREAK,
  AST_GOTO,
  AST_DO,
  AST_WHILE,
  AST_REPEAT,
  AST_IF,
  AST_FOR_NUM,
  AST_FOR_LIST,
  AST_FUNCTION_STMT,
  AST_LOCAL_DECL,
  AST_LOCAL_FUNC,
  AST_RETURN,
  
  /* Structural */
  AST_BLOCK,
  AST_CHUNK
} AstNodeKind;


/*
** Node header - 모든 AST 노드의 공통 헤더
*/
typedef struct AstNode {
  AstNodeKind kind;
  int line;  /* line number where this node starts */
} AstNode;


/*
** Expression List (linked list)
*/
typedef struct ExprList {
  AstNode *expr;
  struct ExprList *next;
} ExprList;


/*
** Statement List (linked list)
*/
typedef struct StmtList {
  AstNode *stmt;
  struct StmtList *next;
} StmtList;


/*
** Expressions
*/
typedef struct {
  AstNode header;
  lua_Number value;
} AstNumeral;

typedef struct {
  AstNode header;
  TString *str;
} AstString;

typedef struct {
  AstNode header;
  TString *name;
} AstVar;

typedef struct {
  AstNode header;
  OpCode op;
  AstNode *left;
  AstNode *right;
} AstBinOp;

typedef struct {
  AstNode header;
  OpCode op;
  AstNode *operand;
} AstUnOp;


/*
** Statements
*/
typedef struct {
  AstNode header;
  StmtList *statements;
} AstBlock;

typedef struct {
  AstNode header;
  TString *name;
} AstLabel;

typedef struct {
  AstNode header;
  TString *name;
} AstGoto;

typedef struct {
  AstNode header;
  ExprList *exprs;
} AstReturn;


/*
** Root AST structure
*/
typedef struct AST {
  Mbuffer *arena;    /* memory arena (reference, owned by SParser) */
  AstNode *root;     /* root node (chunk) */
} AST;


/*
** Parsing state (similar to FuncState in lparser)
*/
typedef struct AstState {
  Mbuffer *arena;           /* AST node arena (reference) */
  struct LexState *ls;      /* lexer state */
  /* future: prev, scopelevel, etc. */
} AstState;


/* AST parser entry point */
LUAI_FUNC AST *luaA_parser (lua_State *L, ZIO *z,
                            Mbuffer *lex_buff, Mbuffer *ast_arena,
                            const char *name, int firstchar);


#endif
