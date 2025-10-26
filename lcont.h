/*
** $Id: lcont.h $
** Continuation support
** See Copyright Notice in lua.h
*/

#ifndef lcont_h
#define lcont_h

#include "lobject.h"
#include "lstate.h"

/* Create a new continuation by capturing current stack state */
LUAI_FUNC Continuation *luaCont_capture (lua_State *L, int level);

/* Invoke a continuation with given arguments */
LUAI_FUNC void luaCont_invoke (lua_State *L, Continuation *cont, int nargs);

/* Free continuation resources */
LUAI_FUNC void luaCont_free (lua_State *L, Continuation *cont);

/* Check if continuation can be invoked */
LUAI_FUNC int luaCont_isvalid (Continuation *cont, lua_State *L);

/* VM-level continuation invocation */
LUAI_FUNC int luaCont_iscontinvoke (const TValue *func);
LUAI_FUNC void luaCont_doinvoke (lua_State *L, StkId func, int nresults);

/* Get cont_call function pointer for comparison */
LUAI_FUNC lua_CFunction luaCont_getcallfunc (void);

#endif
