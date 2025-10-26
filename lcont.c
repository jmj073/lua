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
** Setup thread to resume from a specific PC
** This mimics a "yielded" state so lua_resume will continue execution
*/
static void setupThreadForResume (lua_State *thread, CallInfo *source_ci) {
  CallInfo *ci;
  StkId func = source_ci->func.p;
  StkId top = source_ci->top.p;
  int nslots = cast_int(top - func);
  int i;
  
  fprintf(stderr, "[DEBUG] setupThreadForResume: copying %d stack slots\n", nslots);
  
  /* Ensure enough stack space */
  luaD_checkstack(thread, nslots + LUA_MINSTACK);
  
  /* Copy stack from source */
  for (i = 0; i < nslots; i++) {
    setobj2s(thread, thread->stack.p + i, s2v(func + i));
  }
  thread->top.p = thread->stack.p + nslots;
  
  /* Setup base CallInfo */
  ci = thread->ci;
  ci->func.p = thread->stack.p;
  ci->top.p = thread->top.p;
  ci->callstatus = source_ci->callstatus & ~CIST_HOOKED;  /* Remove hook flag */
  
  /* Copy Lua execution state */
  if (isLua(source_ci)) {
    /* CRITICAL: PC adjustment
    ** savedpc points to NEXT instruction after callcc
    ** But lua_resume will do savedpc-- (like line 939 in ldo.c)
    ** So we DON'T adjust here - let resume handle it */
    ci->u.l.savedpc = source_ci->u.l.savedpc;
    ci->u.l.trap = 0;
    ci->u.l.nextraargs = 0;
    
    /* Mark as HOOKYIELD so resume will do savedpc-- */
    ci->callstatus |= CIST_HOOKYIELD;
    
    fprintf(stderr, "[DEBUG] setupThreadForResume: PC=%p (will be adjusted by resume)\n", 
            (void*)ci->u.l.savedpc);
  }
  
  /* Set thread status to YIELD so lua_resume will continue execution */
  thread->status = LUA_YIELD;
  
  fprintf(stderr, "[DEBUG] setupThreadForResume: thread ready, status=YIELD\n");
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
  
  /* Setup thread with caller's state */
  setupThreadForResume(thread, caller_ci);
  
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
** Check if a function is a continuation invocation
** Used by VM to detect continuation calls
*/
int luaCont_iscontinvoke (const TValue *func) {
  if (ttisCclosure(func)) {
    CClosure *cl = clCvalue(func);
    lua_CFunction cont_func = luaCont_getcallfunc();
    if (cl->f == cont_func) {
      return 1;
    }
  }
  return 0;
}


/*
** Direct continuation invocation from VM
** Uses VM-level context injection for proper continuation semantics
*/
void luaCont_doinvoke (lua_State *L, StkId func, int nresults) {
  CClosure *cl;
  Continuation *cont;
  lua_State *thread;
  int i;
  int nargs;
  TValue *saved_args;
  
  fprintf(stderr, "[CONT] luaCont_doinvoke: starting\n");
  
  /* Extract continuation from closure upvalue */
  cl = clCvalue(s2v(func));
  cont = (Continuation *)pvalue(&cl->upvalue[0]);
  
  if (!luaCont_isvalid(cont, L)) {
    luaG_runerror(L, "invalid continuation");
  }
  
  /* Get continuation thread */
  thread = cont->thread;
  
  /* Count arguments (everything after func on stack) */
  nargs = cast_int(L->top.p - (func + 1));
  fprintf(stderr, "[CONT]   %d arguments to save\n", nargs);
  
  /* CRITICAL: Save arguments BEFORE injection!
  ** After injection, stack contents will change */
  saved_args = luaM_newvector(L, nargs, TValue);
  for (i = 0; i < nargs; i++) {
    setobj(L, &saved_args[i], s2v(func + 1 + i));
    fprintf(stderr, "[CONT]   saved arg[%d] type=%d\n", i, ttype(&saved_args[i]));
  }
  
  /* ⭐ KEY: Inject thread's context
  ** This replaces our CallInfo with thread's context */
  luaV_injectcontext(L, thread);
  
  /* NOW place saved continuation arguments as return values at func position
  ** These arguments become the "return values" from callcc */
  for (i = 0; i < nargs; i++) {
    setobj2s(L, func + i, &saved_args[i]);
    fprintf(stderr, "[CONT]   arg[%d] placed at func+%d, type=%d\n", 
            i, i, ttype(s2v(func + i)));
  }
  L->top.p = func + nargs;
  
  /* Free saved arguments */
  luaM_freearray(L, saved_args, nargs);
  
  fprintf(stderr, "[CONT] luaCont_doinvoke: done, VM will continue in new context\n");
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
