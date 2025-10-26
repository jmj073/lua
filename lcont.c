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
/*
** Clone a thread for multi-shot continuation
** Creates a shallow copy of the thread's stack and CallInfo
*/
static lua_State *cloneThreadForInvoke(lua_State *L, lua_State *orig) {
  lua_State *clone;
  int stack_size;
  int i;
  
  fprintf(stderr, "[CONT] cloneThreadForInvoke: cloning thread %p\n", (void*)orig);
  
  /* Create new thread */
  clone = lua_newthread(L);
  lua_pop(L, 1);  /* Remove from stack */
  
  /* Copy stack */
  stack_size = cast_int(orig->top.p - orig->stack.p);
  luaD_checkstack(clone, stack_size);
  
  for (i = 0; i < stack_size; i++) {
    setobj2s(clone, clone->stack.p + i, s2v(orig->stack.p + i));
  }
  clone->top.p = clone->stack.p + stack_size;
  
  /* Copy CallInfo state */
  clone->ci->func.p = clone->stack.p + (orig->ci->func.p - orig->stack.p);
  clone->ci->top.p = clone->top.p;
  clone->ci->callstatus = orig->ci->callstatus;
  
  if (isLua(orig->ci)) {
    clone->ci->u.l.savedpc = orig->ci->u.l.savedpc;
    clone->ci->u.l.trap = 0;
    clone->ci->u.l.nextraargs = 0;
  }
  
  clone->status = orig->status;
  
  fprintf(stderr, "[CONT]   clone created: %p\n", (void*)clone);
  return clone;
}


void luaCont_doinvoke (lua_State *L, StkId func, int nresults) {
  CClosure *cl;
  Continuation *cont;
  lua_State *thread;
  lua_State *clone;
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
  
  /* Get continuation thread and clone it for multi-shot */
  thread = cont->thread;
  clone = cloneThreadForInvoke(L, thread);
  
  /* Count arguments (everything after func on stack) */
  nargs = cast_int(L->top.p - (func + 1));
  fprintf(stderr, "[CONT]   %d arguments to save\n", nargs);
  
  /* CRITICAL: Place arguments in clone BEFORE injection!
  ** The clone's stack (slot[1] onwards) will become the return values */
  
  fprintf(stderr, "[CONT]   placing %d arguments in clone at slot[1]\n", nargs);
  
  /* Clone's slot[0] is the function, slot[1] onwards are return values
  ** We place our arguments there */
  luaD_checkstack(clone, nargs + 1);
  for (i = 0; i < nargs; i++) {
    setobj2s(clone, clone->ci->func.p + 1 + i, s2v(func + 1 + i));
    fprintf(stderr, "[CONT]   arg[%d] placed in clone slot[%d], type=%d\n",
            i, 1 + i, ttype(s2v(clone->ci->func.p + 1 + i)));
  }
  
  /* Update clone's top to include arguments */
  clone->top.p = clone->ci->func.p + 1 + nargs;
  clone->ci->top.p = clone->top.p;
  
  /* ⭐ KEY: Inject clone's context (with arguments already placed)
  ** This replaces our CallInfo with clone's context */
  luaV_injectcontext(L, clone);
  
  /* CRITICAL: After injection, ci->func.p has changed!
  ** We need to place arguments relative to NEW ci->func.p, not old func!
  ** VM will execute with base = ci->func.p + 1 */
  StkId new_func = L->ci->func.p;
  fprintf(stderr, "[CONT]   After injection: new func=%p (was %p)\n",
          (void*)new_func, (void*)func);
  
  /* Place arguments at new_func+1 (they're already in the injected stack) */
  L->top.p = new_func + 1 + nargs;
  
  /* CRITICAL: The VM needs to see results starting from func+1
  ** OP_CALL expects results there after call completion */
  fprintf(stderr, "[CONT]   L->top set to func+%d\n", 1 + nargs);
  
  fprintf(stderr, "[CONT]   after injection, L->top=%p (func+%d)\n",
          (void*)L->top.p, 1 + nargs);
  
  /* Debug: Show what's at func positions after injection */
  fprintf(stderr, "[CONT]   Final stack state:\n");
  fprintf(stderr, "[CONT]     new_func (0x%p) type=%d\n", 
          (void*)new_func, ttype(s2v(new_func)));
  fprintf(stderr, "[CONT]     new_func+1 (0x%p) type=%d\n", 
          (void*)(new_func+1), ttype(s2v(new_func+1)));
  
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
