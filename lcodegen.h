/*
** $Id: lcodegen.h $
** Code generator from AST
** See Copyright Notice in lua.h
*/

#ifndef lcodegen_h
#define lcodegen_h

#include "lobject.h"
#include "lparser.h"
#include "last.h"


/* Code generator entry point */
LUAI_FUNC LClosure *luaY_generate (lua_State *L, AST *ast, 
                                   Dyndata *dyd, const char *name);


#endif
