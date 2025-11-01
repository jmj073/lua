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

/* Export cont_call pointer for VM to detect continuation invocations */
int cont_call(lua_State *L);  /* forward declaration */
lua_CFunction luaCont_getcallfunc (void) {
  return cont_call;
}

static int k_escape(lua_State *L) {
  lua_longjmp *lj = (lua_longjmp *)lua_touserdata(L, lua_upvalueindex(1));
  lua_State *origin_L = (lua_State *)lua_touserdata(L, lua_upvalueindex(2));
  int result_ref = (int)lua_tointeger(L, lua_upvalueindex(3));  /* registry ref for result table */
  
  /* Check if we're trying to escape across coroutine boundaries */
  if (L != origin_L) {
    return luaL_error(L, "cannot escape across coroutine boundaries");
  }
  
  /* Check if the escape continuation is still valid (result_ref >= 0) */
  if (result_ref == LUA_NOREF || result_ref == LUA_REFNIL) {
    return luaL_error(L, "escape continuation is no longer valid");
  }
  
  /* Get result table from registry */
  lua_rawgeti(L, LUA_REGISTRYINDEX, result_ref);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return luaL_error(L, "result table is invalid");
  }
  
  /* Pack arguments into the result table */
  int n = lua_gettop(L) - 1;  /* exclude the table itself */
  int i;
  for (i = 1; i <= n; i++) {
    lua_pushvalue(L, i);
    lua_rawseti(L, -2, i);  /* table[i] = arg[i] */
  }
  lua_pop(L, 1);  /* pop table */
  
  /* Jump back to callec's protected runner using an error status so VM unwinds */
  luaD_longjump(L, lj, LUA_ERRRUN);
  return 0; /* unreachable */
}

typedef struct CallecData {
  lua_longjmp *lj;
  int result_ref;  /* registry reference for result table */
} CallecData;

static void callec_runner(lua_State *L, void *ud) {
  CallecData *data = (CallecData *)ud;
  int i, nresults;
  
  /* arg 1 must be a function */
  luaL_checktype(L, 1, LUA_TFUNCTION);
  
  /* Create escape closure with 3 upvalues: lj, L, result_ref */
  lua_pushlightuserdata(L, data->lj);
  lua_pushlightuserdata(L, L);
  lua_pushinteger(L, data->result_ref);
  lua_pushcclosure(L, k_escape, 3);
  
  /* call func(escape) with multiple returns */
  lua_call(L, 1, LUA_MULTRET);
  
  /* Normal return: store results in table for consistency */
  nresults = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, data->result_ref);
  
  /* Mark this as normal return by setting a flag */
  lua_pushboolean(L, 1);
  lua_rawseti(L, -2, 0);  /* table[0] = true (normal return marker) */
  
  /* Store results in table */
  for (i = 1; i <= nresults; i++) {
    lua_pushvalue(L, i);
    lua_rawseti(L, -2, i);
  }
  lua_pop(L, 1);  /* pop table */
}

static int luaB_callec(lua_State *L) {
  lua_longjmp lj;
  CallecData data;
  int result_ref;
  TStatus st;
  int is_normal_return;
  lua_Unsigned len;
  lua_Integer i;
  int table_idx;
  
  /* Create result table and store in registry */
  lua_newtable(L);
  result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  
  /* Setup data for runner */
  data.lj = &lj;
  data.result_ref = result_ref;
  
  /* Run with longjump protection */
  st = luaD_runwithjump(L, &lj, callec_runner, &data);
  
  /* Get result table from registry */
  lua_rawgeti(L, LUA_REGISTRYINDEX, result_ref);
  
  if (!lua_istable(L, -1)) {
    /* Result table is invalid - this is a real error */
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, result_ref);
    
    if (st != LUA_OK) {
      return lua_error(L);
    }
    return luaL_error(L, "callec: result table is invalid");
  }
  
  /* Check if this is normal return (table[0] == true) or escape */
  lua_rawgeti(L, -1, 0);
  is_normal_return = lua_toboolean(L, -1);
  lua_pop(L, 1);
  
  if (st != LUA_OK && !is_normal_return) {
    /* Escape path: table has results from k_escape */
    len = lua_rawlen(L, -1);
    table_idx = lua_gettop(L);
    
    /* Unpack table contents onto stack */
    for (i = 1; i <= (lua_Integer)len; i++) {
      lua_rawgeti(L, table_idx, i);
    }
    
    /* Remove table */
    lua_remove(L, table_idx);
    
    /* Clean up */
    luaL_unref(L, LUA_REGISTRYINDEX, result_ref);
    
    return (int)len;
  } else if (st == LUA_OK && is_normal_return) {
    /* Normal return path: table has results from callec_runner */
    len = lua_rawlen(L, -1);
    table_idx = lua_gettop(L);
    
    /* Unpack table contents onto stack */
    for (i = 1; i <= (lua_Integer)len; i++) {
      lua_rawgeti(L, table_idx, i);
    }
    
    /* Remove table */
    lua_remove(L, table_idx);
    
    /* Clean up */
    luaL_unref(L, LUA_REGISTRYINDEX, result_ref);
    
    return (int)len;
  } else {
    /* Error condition */
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, result_ref);
    return lua_error(L);
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
int cont_call(lua_State *L) {
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
