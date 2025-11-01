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
#include "lfunc.h"
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
** CAPTURES ENTIRE CALLINFO CHAIN from base to source_ci
** This is necessary for continuations that return from nested functions
*/
static void setupThreadForResume (lua_State *thread, lua_State *L, CallInfo *source_ci) {
  StkId stack_bottom = L->stack.p;
  StkId stack_top = L->top.p;
  int total_slots = cast_int(stack_top - stack_bottom);
  int i;
  CallInfo *src_ci;
  CallInfo *dst_ci;
  ptrdiff_t stack_offset;
  
  fprintf(stderr, "[DEBUG] setupThreadForResume: copying ENTIRE stack (%d slots)\n", total_slots);
  fprintf(stderr, "[DEBUG]   Source stack: %p to %p\n", (void*)stack_bottom, (void*)stack_top);
  
  /* Ensure enough stack space for entire stack */
  luaD_checkstack(thread, total_slots + LUA_MINSTACK);
  
  /* Copy ENTIRE stack from L to thread */
  for (i = 0; i < total_slots; i++) {
    setobj2s(thread, thread->stack.p + i, s2v(stack_bottom + i));
  }
  thread->top.p = thread->stack.p + total_slots;
  
  /* Calculate offset for pointer adjustment */
  stack_offset = thread->stack.p - stack_bottom;
  fprintf(stderr, "[DEBUG]   Stack offset: %ld\n", (long)stack_offset);
  
  /* Now copy CallInfo chain from base_ci to source_ci */
  /* First, count how many CallInfo nodes we need */
  int ci_count = 0;
  for (src_ci = &L->base_ci; src_ci != NULL && src_ci != source_ci->next; src_ci = src_ci->next) {
    ci_count++;
  }
  fprintf(stderr, "[DEBUG]   CallInfo chain length: %d\n", ci_count);
  
  /* Start with thread's base_ci and build the chain */
  dst_ci = &thread->base_ci;
  src_ci = &L->base_ci;
  
  for (i = 0; i < ci_count; i++) {
    /* Allocate next CallInfo if needed (skip base_ci which already exists) */
    if (i > 0) {
      if (dst_ci->next == NULL) {
        dst_ci->next = luaE_extendCI(thread);
      }
      dst_ci = dst_ci->next;
      dst_ci->previous = thread->ci;
      thread->ci = dst_ci;
    }
    
    /* Copy CallInfo fields with adjusted pointers */
    dst_ci->func.p = src_ci->func.p + stack_offset;
    dst_ci->top.p = src_ci->top.p + stack_offset;
    dst_ci->callstatus = src_ci->callstatus & ~CIST_HOOKED;
    dst_ci->u2 = src_ci->u2;
    
    /* Copy union data (Lua or C) */
    if (isLua(src_ci)) {
      dst_ci->u.l.savedpc = src_ci->u.l.savedpc;
      dst_ci->u.l.trap = 0;
      dst_ci->u.l.nextraargs = src_ci->u.l.nextraargs;
    } else {
      dst_ci->u.c = src_ci->u.c;
    }
    
    fprintf(stderr, "[DEBUG]     CI[%d]: func=%p, top=%p, status=0x%x\n",
            i, (void*)dst_ci->func.p, (void*)dst_ci->top.p, dst_ci->callstatus);
    
    src_ci = src_ci->next;
  }
  
  /* Mark the top CallInfo for resume */
  if (isLua(thread->ci)) {
    thread->ci->callstatus |= CIST_HOOKYIELD;
    fprintf(stderr, "[DEBUG]   Top CI PC=%p (will be adjusted by resume)\n", 
            (void*)thread->ci->u.l.savedpc);
  }
  
  /* Set thread status to YIELD so lua_resume will continue execution */
  thread->status = LUA_YIELD;
  thread->nci = ci_count;
  
  /* Fix upvalues: Create independent UpVal objects for this continuation
  ** This ensures each continuation has its own copy of upvalues */
  for (i = 0; i < total_slots; i++) {
    TValue *o = s2v(thread->stack.p + i);
    if (ttisLclosure(o)) {
      LClosure *cl = clLvalue(o);
      int j;
      for (j = 0; j < cl->nupvalues; j++) {
        UpVal *old_uv = cl->upvals[j];
        if (old_uv) {
          /* Create a new UpVal for this closure */
          UpVal *new_uv = luaM_new(thread, UpVal);
          new_uv->v.p = &new_uv->u.value;  /* Always closed */
          luaC_objbarrier(thread, cl, new_uv);
          
          /* Copy the value from the old upvalue */
          if (upisopen(old_uv)) {
            TValue *uv_ptr = old_uv->v.p;
            /* Check if upvalue points within the original L's stack */
            if (uv_ptr >= (TValue*)stack_bottom && uv_ptr < (TValue*)stack_top) {
              /* Copy from corresponding position in thread's stack */
              ptrdiff_t offset = (StkId)uv_ptr - stack_bottom;
              setobj(thread, &new_uv->u.value, s2v(thread->stack.p + offset));
            } else {
              /* Copy from original location */
              setobj(thread, &new_uv->u.value, old_uv->v.p);
            }
          } else {
            /* Old upvalue was already closed */
            setobj(thread, &new_uv->u.value, &old_uv->u.value);
          }
          
          /* Replace the upvalue pointer in the closure */
          cl->upvals[j] = new_uv;
        }
      }
    }
  }
  
  (void)ci_count;  /* Mark as used for non-debug builds */
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
  setupThreadForResume(thread, L, caller_ci);
  
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
/*
** Helper: Setup upvalues for cloned closure
** SIMPLIFIED: Just share all upvalues with original
** This is safe and avoids complex memory management issues
*/
static void setupClonedClosureUpvalues(LClosure *clone_cl, LClosure *orig_cl) {
  int i;
  int nups = clone_cl->nupvalues;
  
  fprintf(stderr, "[CONT]   Sharing %d upvalues (simplified approach)\n", nups);
  
  for (i = 0; i < nups; i++) {
    /* Simply share all upvalues - both open and closed
    ** This means upvalue modifications ARE visible across invocations
    ** But it's safe and avoids complex GC/memory issues */
    clone_cl->upvals[i] = orig_cl->upvals[i];
    
    if (orig_cl->upvals[i]) {
      fprintf(stderr, "[CONT]     upval[%d]: shared %p\n", 
              i, (void*)orig_cl->upvals[i]);
    }
  }
  
  /* Note: This means we don't have true isolation for upvalues,
  ** but it's the safest approach and works for most use cases.
  ** For complete isolation, use global variables or tables */
}

static lua_State *cloneThreadForInvoke(lua_State *L, lua_State *orig, int *ref_out) {
  lua_State *clone;
  int stack_size;
  int i, ci_count;
  CallInfo *src_ci, *dst_ci;
  ptrdiff_t stack_offset;
  
  fprintf(stderr, "[CONT] cloneThreadForInvoke: cloning thread %p\n", (void*)orig);
  
  /* Create new thread - leaves it on L's stack */
  clone = lua_newthread(L);
  
  /* Store in registry to protect from GC */
  *ref_out = luaL_ref(L, LUA_REGISTRYINDEX);
  
  /* Copy stack - simple shallow copy
  ** Closures and upvalues are shared with original
  ** This is the safest approach and avoids GC issues */
  stack_size = cast_int(orig->top.p - orig->stack.p);
  luaD_checkstack(clone, stack_size);
  
  for (i = 0; i < stack_size; i++) {
    setobj2s(clone, clone->stack.p + i, s2v(orig->stack.p + i));
  }
  clone->top.p = clone->stack.p + stack_size;
  
  fprintf(stderr, "[CONT]   Stack copied: %d slots (shallow copy)\n", stack_size);
  
  /* Calculate stack offset for pointer adjustments */
  stack_offset = clone->stack.p - orig->stack.p;
  
  /* Copy ENTIRE CallInfo chain */
  ci_count = orig->nci;
  fprintf(stderr, "[CONT]   Cloning %d CallInfos\n", ci_count);
  
  src_ci = &orig->base_ci;
  dst_ci = &clone->base_ci;
  
  for (i = 0; i < ci_count; i++) {
    /* Allocate next CallInfo if needed */
    if (i > 0) {
      if (dst_ci->next == NULL) {
        dst_ci->next = luaE_extendCI(clone);
      }
      dst_ci = dst_ci->next;
      dst_ci->previous = clone->ci;
      clone->ci = dst_ci;
    }
    
    /* Copy CallInfo with adjusted pointers */
    dst_ci->func.p = src_ci->func.p + stack_offset;
    dst_ci->top.p = src_ci->top.p + stack_offset;
    dst_ci->callstatus = src_ci->callstatus;
    dst_ci->u2 = src_ci->u2;
    
    /* Copy union data */
    if (isLua(src_ci)) {
      dst_ci->u.l.savedpc = src_ci->u.l.savedpc;
      dst_ci->u.l.trap = 0;
      dst_ci->u.l.nextraargs = src_ci->u.l.nextraargs;
    } else {
      dst_ci->u.c = src_ci->u.c;
    }
    
    src_ci = src_ci->next;
    if (src_ci == NULL) break;
  }
  
  clone->status = orig->status;
  clone->nci = ci_count;
  
  /* Note: Upvalues were already closed in setupThreadForResume, so the
  ** cloned closures' upvalues already contain their own copies of values */
  
  fprintf(stderr, "[CONT]   clone created: %p, ref=%d\n", (void*)clone, *ref_out);
  return clone;
}


int luaCont_doinvoke (lua_State *L, StkId func, int nresults) {
  CClosure *cl;
  Continuation *cont;
  lua_State *thread;
  lua_State *clone;
  int clone_ref;
  int i;
  int nargs;
  const Instruction *saved_pc;
  Instruction call_inst;
  int ra_offset;
  StkId thread_dest;
  
  fprintf(stderr, "[CONT] luaCont_doinvoke: starting (Trampoline + Multi-shot)\n");
  
  /* Extract continuation from closure upvalue */
  cl = clCvalue(s2v(func));
  cont = (Continuation *)pvalue(&cl->upvalue[0]);
  
  if (!luaCont_isvalid(cont, L)) {
    luaG_runerror(L, "invalid continuation");
  }
  
  /* ⭐ MULTI-SHOT: Clone the thread to preserve original state */
  clone = cloneThreadForInvoke(L, cont->thread, &clone_ref);
  thread = clone;
  
  fprintf(stderr, "[CONT]   Cloned thread %p → %p (ref=%d)\n",
          (void*)cont->thread, (void*)clone, clone_ref);
  
  /* Count arguments (everything after func on stack) */
  nargs = cast_int(L->top.p - (func + 1));
  fprintf(stderr, "[CONT]   %d arguments to continuation\n", nargs);
  
  /* Save arguments BEFORE injection (they will be overwritten) */
  TValue saved_args[MAXRESULTS];
  for (i = 0; i < nargs && i < MAXRESULTS; i++) {
    setobj(L, &saved_args[i], s2v(func + 1 + i));
  }
  
  /* ⭐ TRAMPOLINE STEP 1: Calculate destination in thread BEFORE injection
  ** Extract RA offset from the CALL instruction that captured this continuation */
  saved_pc = thread->ci->u.l.savedpc;
  call_inst = *(saved_pc - 1);
  ra_offset = GETARG_A(call_inst);
  
  fprintf(stderr, "[CONT]   CALL instruction RA offset = %d\n", ra_offset);
  fprintf(stderr, "[CONT]   Thread base = %p\n", (void*)thread->ci->func.p);
  
  fprintf(stderr, "[CONT]   CALL instruction RA offset = %d, will place %d args\n", 
          ra_offset, nargs);
  
  /* ⭐ STEP 2: Inject context FIRST (this restores the captured state)
  ** After injection, the CallInfo chain and stack will be restored */
  fprintf(stderr, "[CONT]   Injecting context...\n");
  luaV_injectcontext(L, thread);
  
  fprintf(stderr, "[CONT]   After injection: L->ci->func.p=%p, savedpc=%p\n",
          (void*)L->ci->func.p, (void*)L->ci->u.l.savedpc);
  fprintf(stderr, "[CONT]   L->top.p=%p, L->ci->top.p=%p\n",
          (void*)L->top.p, (void*)L->ci->top.p);
  
  /* ⭐ STEP 3: Now copy arguments to the injected context
  ** Arguments go to the position specified by RA offset in the CALL instruction
  ** IMPORTANT: Use the captured context's func.p, not the original */
  StkId result_pos = L->ci->func.p + 1 + ra_offset;
  
  fprintf(stderr, "[CONT]   Placing %d args at result_pos=%p (func.p + 1 + %d)\n",
          nargs, (void*)result_pos, ra_offset);
  
  /* Ensure we have enough space */
  luaD_checkstack(L, nargs + LUA_MINSTACK);
  
  /* Copy arguments from saved_args to result position */
  for (i = 0; i < nargs; i++) {
    setobj2s(L, result_pos + i, &saved_args[i]);
    fprintf(stderr, "[CONT]   Placed arg[%d] type=%d, value=",
            i, ttype(s2v(result_pos + i)));
    if (ttisinteger(s2v(result_pos + i))) {
      fprintf(stderr, "%lld\n", (long long)ivalue(s2v(result_pos + i)));
    } else {
      fprintf(stderr, "(not int)\n");
    }
  }
  
  /* Update top to include the arguments */
  if (result_pos + nargs > L->top.p) {
    L->top.p = result_pos + nargs;
  }
  
  /* Verify what we placed */
  fprintf(stderr, "[CONT]   Verification - result_pos[0] type=%d\n",
          ttype(s2v(result_pos)));
  if (nargs > 0 && ttisinteger(s2v(result_pos))) {
    fprintf(stderr, "%lld\n", (long long)ivalue(s2v(result_pos)));
  } else {
    fprintf(stderr, "(not int)\n");
  }
  
  /* ⭐ MULTI-SHOT: Release clone from registry
  ** Original thread is preserved in continuation! */
  if (clone_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, clone_ref);
    fprintf(stderr, "[CONT]   Released clone ref=%d (original preserved)\n", clone_ref);
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
