#define lcontlib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"
#include "ldo.h"
#include "lcont.h"
#include "lobject.h"
#include "lstate.h"

static int luaB_callec(lua_State *L); /* forward */

static int k_escape(lua_State *L) {
  lua_longjmp *lj = (lua_longjmp *)lua_touserdata(L, lua_upvalueindex(1));
  lua_State *origin_L = (lua_State *)lua_touserdata(L, lua_upvalueindex(2));
  /* Check if we're trying to escape across coroutine boundaries */
  if (L != origin_L) {
    return luaL_error(L, "cannot escape across coroutine boundaries");
  }
  /* Check if the escape continuation is still valid */
  /* Use lj+1 as key for validity flag to avoid collision with result table */
  lua_pushlightuserdata(L, (void*)((char*)lj + 1));
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (!lua_toboolean(L, -1)) {
    lua_pop(L, 1);
    return luaL_error(L, "escape continuation is no longer valid");
  }
  lua_pop(L, 1);
  /* Pack arguments into a table and stash in registry under key 'lj' */
  int n = lua_gettop(L);
  int i;
  lua_createtable(L, n, 0);
  {
    int t = lua_absindex(L, -1);
    luaL_checktype(L, t, LUA_TTABLE);
    for (i = 1; i <= n; i++) {
      lua_pushvalue(L, i);
      lua_rawseti(L, t, i);
    }
    /* registry[lj] = table */
    lua_pushlightuserdata(L, lj);       /* key */
    lua_pushvalue(L, t);                /* value = table */
    lua_settable(L, LUA_REGISTRYINDEX);
  }
  lua_pop(L, 1); /* pop local table */
  /* Jump back to callec's protected runner using an error status so VM unwinds */
  luaD_longjump(L, lj, LUA_ERRRUN);
  return 0; /* unreachable */
}

static void callec_runner(lua_State *L, void *ud) {
  lua_longjmp *lj = (lua_longjmp *)ud;
  /* arg 1 must be a function */
  luaL_checktype(L, 1, LUA_TFUNCTION);
  /* Mark this escape continuation as valid in registry */
  /* Use lj+1 as key for validity flag to avoid collision with result table */
  lua_pushlightuserdata(L, (void*)((char*)lj + 1));
  lua_pushboolean(L, 1);
  lua_rawset(L, LUA_REGISTRYINDEX);
  /* push escape closure as single argument with 2 upvalues: lj and L */
  lua_pushlightuserdata(L, lj);
  lua_pushlightuserdata(L, L);
  lua_pushcclosure(L, k_escape, 2); /* escape */
  /* call func(escape) with multiple returns */
  lua_call(L, 1, LUA_MULTRET);
}

static int luaB_callec(lua_State *L) {
  lua_longjmp lj;
  int base = lua_gettop(L);
  TStatus st = luaD_runwithjump(L, &lj, callec_runner, &lj);
  /* Invalidate the escape continuation */
  lua_pushlightuserdata(L, (void*)((char*)&lj + 1));
  lua_pushnil(L);
  lua_rawset(L, LUA_REGISTRYINDEX);
  /* After run: either normal return (st==LUA_OK) or escaped via longjump (st!=LUA_OK) */
  /* Check if escaped: registry[lj] holds a table of results */
  lua_pushlightuserdata(L, &lj);
  lua_gettable(L, LUA_REGISTRYINDEX);
  if (lua_istable(L, -1)) {
    /* escape path: keep table, compute len, then move table just above base */
    lua_Unsigned len = lua_rawlen(L, -1);
    /* clear registry entry now */
    lua_pushlightuserdata(L, &lj);
    lua_pushnil(L);
    lua_settable(L, LUA_REGISTRYINDEX);
    /* move table to position base+1 and drop anything above */
    lua_insert(L, base + 1);  /* table to base+1 */
    lua_settop(L, base + 1);  /* stack: ... table */
    {
      lua_Integer i;
      for (i = 1; i <= (lua_Integer)len; i++) {
        lua_rawgeti(L, base + 1, i);
      }
    }
    /* remove the table, leaving only the pushed values */
    lua_remove(L, base + 1);
    return (int)len;
  } else {
    /* no escape: pop lookup result */
    lua_pop(L, 1);
    if (st != LUA_OK) {
      /* real error */
      return lua_error(L);
    }
    /* normal return from function: results left on stack by lua_call */
    return lua_gettop(L); /* stack now contains exactly the results */
  }
}

/*
** Continuation invocation function (Phase 2.5: Direct VM re-entry)
** Upvalue 1: the continuation object (as light userdata pointer)
**
** When continuation is called: k(values...)
** This directly invokes the continuation by restoring the saved context
** and resuming execution from the saved PC.
*/
static int cont_call(lua_State *L) {
  Continuation *cont;
  int nargs = lua_gettop(L);
  
  /* Get continuation from upvalue (stored as light userdata) */
  cont = (Continuation *)lua_touserdata(L, lua_upvalueindex(1));
  if (cont == NULL) {
    return luaL_error(L, "continuation pointer is NULL");
  }
  
  /* Validate continuation */
  if (cont->tt != ctb(LUA_VCONT)) {
    return luaL_error(L, "continuation has wrong type tag: %d (expected %d)", 
                      cont->tt, ctb(LUA_VCONT));
  }
  
  if (cont->L != L) {
    return luaL_error(L, "continuation from different thread");
  }
  
  /* Phase 2.5: Direct invocation
  ** luaCont_invoke will restore the context and resume execution.
  ** 
  ** Note: luaCont_invoke calls luaV_execute which may not return normally
  ** if the continuation transfers control elsewhere. We need to handle this.
  */
  
  /* Invoke continuation - this may not return! */
  luaCont_invoke(L, cont, nargs);
  
  /* If we get here, luaV_execute returned normally.
  ** Return whatever is on the stack. */
  return lua_gettop(L);
}


/*
** callcc implementation (Phase 2.5: Direct invocation)
** Takes a function and calls it with a continuation object
** The continuation can be invoked multiple times (multi-shot)
*/
static int luaB_callcc(lua_State *L) {
  Continuation *cont;
  int nresults;
  
  /* Argument must be a function */
  luaL_checktype(L, 1, LUA_TFUNCTION);
  
  /* Capture current continuation (level 1 = caller of callcc) */
  cont = luaCont_capture(L, 1);
  if (cont == NULL) {
    return luaL_error(L, "cannot capture continuation (C frame boundary)");
  }
  
  /* Stack: [function] */
  
  /* CRITICAL FIX: Push continuation as GCObject to protect from premature collection
  ** We temporarily push it as a string-like value that GC can see,
  ** then immediately convert to light userdata for the closure.
  ** Alternative: Stop GC during this critical section. */
  
  /* Disable GC temporarily to prevent collection during closure creation */
  lu_byte old_gcstp = G(L)->gcstp;
  G(L)->gcstp = 1;  /* stop GC */
  
  /* Push continuation pointer as light userdata */
  lua_pushlightuserdata(L, cont);
  
  /* Create closure with continuation as upvalue */
  lua_pushcclosure(L, cont_call, 1);
  
  /* Re-enable GC */
  G(L)->gcstp = old_gcstp;
  
  /* Stack: [function, cont_closure] */
  
  /* Duplicate function and move it to top */
  lua_pushvalue(L, 1);  /* Stack: [function, cont_closure, function] */
  lua_insert(L, 2);     /* Stack: [function, function, cont_closure] */
  lua_remove(L, 1);     /* Stack: [function, cont_closure] */
  
  /* Call function with continuation as argument
  ** If continuation is invoked, luaCont_invoke will handle it */
  lua_call(L, 1, LUA_MULTRET);
  
  /* Return whatever is on stack */
  nresults = lua_gettop(L);
  return nresults;
}


static const luaL_Reg cont_funcs[] = {
  {"callcc", luaB_callcc},
  {"callec", luaB_callec},
  {NULL, NULL}
};

LUAMOD_API int luaopen_continuation (lua_State *L) {
  luaL_newlib(L, cont_funcs);
  return 1;
}
