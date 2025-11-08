/*
** $Id: last.h $
** AST (Abstract Syntax Tree) generation
** See Copyright Notice in lua.h
*/

#ifndef last_h
#define last_h

#include "lobject.h"
#include "lzio.h"
#include "lcode.h"  /* For BinOpr and UnOpr enums */

/*
** AST Node Types
*/
typedef enum {
  /* Expressions */
  AST_NIL = 0,
  AST_TRUE,
  AST_FALSE,
  AST_NUMERAL,
  AST_STRING,
  AST_VAR,
  AST_VARARG,
  AST_BINOP,
  AST_UNOP,
  AST_CALL,
  AST_TABLECTOR,
  AST_INDEXING,
  AST_FUNCTION,
  
  /* Statements */
  AST_CHUNK = 29,  /* Keep at 29 to match existing code */
  AST_BLOCK,
  AST_LABEL,
  AST_GOTO,
  AST_RETURN,
  AST_FUNCCALL_STMT,
  AST_IF,
  AST_LOCAL_DECL,
  AST_ASSIGNMENT,
  AST_FUNCTION_STMT,
  AST_LOCAL_FUNC
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
** Name List (linked list)
*/
typedef struct NameList {
  TString *name;
  struct NameList *next;
} NameList;


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
  BinOpr op;
  AstNode *left;
  AstNode *right;
} AstBinOp;

typedef struct {
  AstNode header;
  UnOpr op;
  AstNode *operand;
} AstUnOp;

typedef struct {
  AstNode header;
  AstNode *func;       /* function expression */
  ExprList *args;      /* argument list */
} AstCall;

typedef enum {
  FIELD_LIST,    /* array element: expr */
  FIELD_REC      /* record element: key = value */
} FieldKind;

typedef struct TableField {
  FieldKind kind;
  AstNode *key;        /* NULL for list field */
  AstNode *value;
  struct TableField *next;
} TableField;

typedef struct {
  AstNode header;
  TableField *fields;
} AstTableCtor;

typedef struct {
  AstNode header;
  AstNode *table;      /* table expression */
  AstNode *index;      /* index expression */
} AstIndexing;

typedef struct {
  AstNode header;
  NameList *params;    /* parameter names */
  AstNode *body;       /* function body (block) */
  int is_vararg;       /* 1 if function has ... */
} AstFunction;


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

typedef struct {
  AstNode header;
  AstNode *call;  /* AstCall node */
} AstFuncCallStmt;

typedef struct IfClause {
  AstNode *condition;  /* condition expression (NULL for else) */
  AstNode *body;       /* block to execute */
  struct IfClause *next;
} IfClause;

typedef struct {
  AstNode header;
  IfClause *clauses;   /* if/elseif/else clauses */
} AstIf;

typedef struct {
  AstNode header;
  NameList *names;     /* list of variable names */
  ExprList *values;    /* optional initialization values */
} AstLocalDecl;

typedef struct {
  AstNode header;
  NameList *vars;      /* list of variables to assign to */
  ExprList *values;    /* values to assign */
} AstAssignment;


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
