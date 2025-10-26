/*
** $Id: lcont.c $
** Continuation support (Coroutine-based implementation)
** See Copyright Notice in lua.h
*/

#define lcont_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "lapi.h"
#include "lauxlib.h"
#include "lcont.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/*
** Helper: Find Lua caller CallInfo at given level
*/
static CallInfo *findCallerCI (lua_State *L, int level) {
  CallInfo *ci = L->ci;
  
  /* Skip C frames to find Lua caller */
  while (ci && !isLua(ci)) {
    ci = ci->previous;
  }
  
  /* Apply level offset */
  while (level > 1 && ci && ci->previous) {
    ci = ci->previous;
    if (isLua(ci)) {
      level--;
    }
  }
  
  if (ci && isLua(ci)) {
    return ci;
  }
  return NULL;
}


/*
** Create a wrapper function that will be pushed to the thread
** This function will be called by lua_resume and will return the continuation values
*/
static int cont_wrapper (lua_State *L) {
  /* Arguments passed to continuation are already on stack */
  /* Just return them as-is */
  int n = lua_gettop(L);
  fprintf(stderr, "[DEBUG] cont_wrapper: returning %d values\n", n);
  return n;
}


/*
** Create a new continuation by capturing current execution state
** Uses coroutine (lua_newthread) to store the snapshot
*/
Continuation *luaCont_capture (lua_State *L, int level) {
  CallInfo *caller_ci;
  lua_State *thread;
  int ref;
  Continuation *cont;
  
  fprintf(stderr, "[DEBUG] luaCont_capture: level=%d\n", level);
  
  /* Find the caller's CallInfo */
  caller_ci = findCallerCI(L, level);
  if (!caller_ci) {
    fprintf(stderr, "[DEBUG] luaCont_capture: no Lua caller found\n");
    return NULL;
  }
  
  fprintf(stderr, "[DEBUG] luaCont_capture: found caller_ci=%p\n", (void*)caller_ci);
  
  /* Create new thread for continuation */
  thread = lua_newthread(L);
  if (!thread) {
    fprintf(stderr, "[DEBUG] luaCont_capture: failed to create thread\n");
    return NULL;
  }
  
  fprintf(stderr, "[DEBUG] luaCont_capture: created thread=%p\n", (void*)thread);
  
  /* Push wrapper function to thread */
  lua_pushcfunction(thread, cont_wrapper);
  fprintf(stderr, "[DEBUG] luaCont_capture: wrapper pushed to thread\n");
  
  /* Store thread in registry to protect from GC */
  ref = luaL_ref(L, LUA_REGISTRYINDEX);  /* This pops the thread from stack */
  fprintf(stderr, "[DEBUG] luaCont_capture: registry ref=%d\n", ref);
  
  /* Create continuation object */
  cont = luaM_new(L, Continuation);
  cont->tt = ctb(LUA_VCONT);
  cont->marked = luaC_white(G(L));
  cont->next = G(L)->allgc;
  cont->L = L;
  cont->thread = thread;
  cont->ref = ref;
  cont->gclist = NULL;
  
  /* Add to GC list */
  G(L)->allgc = obj2gco(cont);
  
  fprintf(stderr, "[DEBUG] luaCont_capture: continuation created=%p\n", (void*)cont);
  
  return cont;
}


/*
** Check if continuation is valid
*/
int luaCont_isvalid (Continuation *cont, lua_State *L) {
  if (cont == NULL)
    return 0;
  if (cont->L != L)
    return 0;
  if (cont->tt != ctb(LUA_VCONT))
    return 0;
  if (cont->thread == NULL)
    return 0;
  return 1;
}


/*
** Invoke a continuation with given arguments
** Uses lua_resume to restart the saved thread
*/
void luaCont_invoke (lua_State *L, Continuation *cont, int nargs) {
  lua_State *thread;
  int i;
  int status;
  int nresults;
  
  fprintf(stderr, "[DEBUG] luaCont_invoke: nargs=%d\n", nargs);
  
  /* Validate continuation */
  if (!luaCont_isvalid(cont, L)) {
    luaG_runerror(L, "invalid continuation");
  }
  
  /* Get the continuation thread */
  thread = cont->thread;
  fprintf(stderr, "[DEBUG] luaCont_invoke: thread=%p\n", (void*)thread);
  
  /* Push arguments to thread stack */
  luaD_checkstack(thread, nargs);
  for (i = 0; i < nargs; i++) {
    StkId arg_pos = L->stack.p + i;
    setobj2s(thread, thread->top.p, s2v(arg_pos));
    thread->top.p++;
  }
  
  fprintf(stderr, "[DEBUG] luaCont_invoke: arguments copied, calling lua_resume\n");
  
  /* Resume the thread */
  status = lua_resume(thread, L, nargs, &nresults);
  
  fprintf(stderr, "[DEBUG] luaCont_invoke: lua_resume returned status=%d, nresults=%d\n",
          status, nresults);
  
  /* Handle results */
  if (status == LUA_OK || status == LUA_YIELD) {
    /* Copy results back to main thread */
    luaD_checkstack(L, nresults);
    for (i = 0; i < nresults; i++) {
      StkId res_pos = thread->stack.p + i;
      setobj2s(L, L->top.p, s2v(res_pos));
      L->top.p++;
    }
    fprintf(stderr, "[DEBUG] luaCont_invoke: %d results copied\n", nresults);
  } else {
    /* Error occurred */
    fprintf(stderr, "[DEBUG] luaCont_invoke: error in resume\n");
    /* Error is already on thread stack, propagate it */
    setobj2s(L, L->top.p, s2v(thread->top.p - 1));
    L->top.p++;
    luaG_runerror(L, "error in continuation");
  }
}


/*
** Free continuation resources
*/
void luaCont_free (lua_State *L, Continuation *cont) {
  /* Unref from registry */
  if (cont->ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, cont->ref);
  }
  /* Free the continuation object itself */
  luaM_free(L, cont);
}
