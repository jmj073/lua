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
#include "lopcodes.h"
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
static lua_State *cloneThreadForInvoke(lua_State *L, lua_State *orig, int *ref_out) {
  lua_State *clone;
  int stack_size;
  int i;
  
  fprintf(stderr, "[CONT] cloneThreadForInvoke: cloning thread %p\n", (void*)orig);
  
  /* Create new thread - leaves it on L's stack */
  clone = lua_newthread(L);
  
  /* Store in registry to protect from GC */
  *ref_out = luaL_ref(L, LUA_REGISTRYINDEX);
  
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
  
  fprintf(stderr, "[CONT]   clone created: %p, ref=%d\n", (void*)clone, *ref_out);
  return clone;
}


int luaCont_doinvoke (lua_State *L, StkId func, int nresults) {
  CClosure *cl;
  Continuation *cont;
  lua_State *thread;
  int i;
  int nargs;
  const Instruction *saved_pc;
  Instruction call_inst;
  int ra_offset;
  StkId thread_dest;
  
  fprintf(stderr, "[CONT] luaCont_doinvoke: starting (Trampoline approach)\n");
  
  /* Extract continuation from closure upvalue */
  cl = clCvalue(s2v(func));
  cont = (Continuation *)pvalue(&cl->upvalue[0]);
  
  if (!luaCont_isvalid(cont, L)) {
    luaG_runerror(L, "invalid continuation");
  }
  
  /* Get continuation thread (single-shot for now) */
  thread = cont->thread;
  
  /* Count arguments (everything after func on stack) */
  nargs = cast_int(L->top.p - (func + 1));
  fprintf(stderr, "[CONT]   %d arguments to continuation\n", nargs);
  
  /* ⭐ TRAMPOLINE STEP 1: Calculate destination in thread BEFORE injection
  ** Extract RA offset from the CALL instruction that captured this continuation */
  saved_pc = thread->ci->u.l.savedpc;
  call_inst = *(saved_pc - 1);
  ra_offset = GETARG_A(call_inst);
  
  fprintf(stderr, "[CONT]   CALL instruction RA offset = %d\n", ra_offset);
  fprintf(stderr, "[CONT]   Thread base = %p\n", (void*)thread->ci->func.p);
  
  /* ⭐ TRAMPOLINE STEP 2: Prepare thread by placing arguments at destination
  ** This is the "trampoline" - we're setting up the thread's stack
  ** so that when injected, everything is already in place */
  thread_dest = thread->ci->func.p + 1 + ra_offset;
  
  fprintf(stderr, "[CONT]   Thread destination = %p (base + 1 + %d)\n",
          (void*)thread_dest, ra_offset);
  
  /* Ensure thread has enough stack space */
  luaD_checkstack(thread, ra_offset + nargs + LUA_MINSTACK);
  
  /* Copy arguments to thread's destination */
  for (i = 0; i < nargs; i++) {
    setobjs2s(thread, thread_dest + i, func + 1 + i);
    fprintf(stderr, "[CONT]   Prepared arg[%d] in thread at offset %d, type=%d, value=",
            i, ra_offset + i, ttype(s2v(thread_dest + i)));
    if (ttisinteger(s2v(thread_dest + i))) {
      fprintf(stderr, "%lld\n", (long long)ivalue(s2v(thread_dest + i)));
    } else {
      fprintf(stderr, "(not int)\n");
    }
  }
  
  /* ⭐ TRAMPOLINE STEP 3: Now inject - arguments are already in place!
  ** Injection will copy the prepared thread stack to L */
  fprintf(stderr, "[CONT]   Injecting prepared thread context...\n");
  luaV_injectcontext(L, thread);
  
  fprintf(stderr, "[CONT]   After injection: L->ci->func.p=%p, savedpc=%p\n",
          (void*)L->ci->func.p, (void*)L->ci->u.l.savedpc);
  
  /* Update top to reflect the result */
  StkId result_pos = L->ci->func.p + 1 + ra_offset;
  L->top.p = result_pos + nargs;
  
  fprintf(stderr, "[CONT]   L->top.p set to %p (result_pos + %d)\n",
          (void*)L->top.p, nargs);
  
  /* Verify what got injected */
  fprintf(stderr, "[CONT]   Verification - result_pos type=%d, value=",
          ttype(s2v(result_pos)));
  if (ttisinteger(s2v(result_pos))) {
    fprintf(stderr, "%lld\n", (long long)ivalue(s2v(result_pos)));
  } else {
    fprintf(stderr, "(not int)\n");
  }
  
  fprintf(stderr, "[CONT] luaCont_doinvoke: done, returning %d args\n", nargs);
  
  return nargs;
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
