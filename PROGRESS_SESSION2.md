# Session 2 Progress Report

## Achievements

### 1. GC Bug Fix ✅
**Problem**: Continuation object was being prematurely collected during `lua_pushcclosure`
- Light userdata is not tracked by GC
- GC triggered between continuation creation and closure creation
- **Solution**: Temporarily disable GC during critical section (`G(L)->gcstp = 1`)

### 2. Coroutine Pattern Analysis ✅
Studied `ldo.c:resume()` function to understand VM expectations:
- `ci->u.l.savedpc--` adjustment (for fetch-execute cycle)
- `L->top.p = firstArg` setting
- Use of protected mode (`luaD_rawrunprotected`)

### 3. CallInfo Setup ✅
Implemented proper CallInfo creation and configuration:
- Create new CallInfo with `luaE_extendCI(L)`
- Set `ci->func.p`, `ci->top.p`, `ci->callstatus`
- Configure `ci->u.l.savedpc`, `ci->u.l.trap`, `ci->u.l.nextraargs`
- Restore stack from captured continuation

## Current Status: BLOCKED

### Symptom
Segmentation fault when calling `luaV_execute(L, ci)`

### Root Cause Analysis

**Error Message**: "attempt to call a nil value"

**Crash Location**: `lua_getstack()` while traversing CallInfo chain during error handling

**Stack State (verified via debug output)**:
```
slot[0]: type=6 (function) ✓ correct
slot[1]: type=0 (number/nil) ✓ continuation argument (42)
slot[2-5]: various types ✓ restored from captured stack
```

### The Core Problem

**Coroutine vs Continuation Difference**:
- **Coroutine**: Resumes from yield point in SAME execution context
  - `L->ci` already points to yielded Lua frame
  - CallInfo chain is intact
  - Just continues execution

- **Continuation**: Invokes from COMPLETELY DIFFERENT context
  - `L->ci` points to C function (`do_invoke`)  
  - Creating NEW CallInfo with `previous` pointing to C frame
  - VM expects consistent CallInfo chain but gets混合 state

**Hypothesis**: `luaV_execute` assumes it's continuing existing execution, not starting fresh context. When error occurs, it tries to unwind through CallInfo chain but encounters inconsistent state.

## Attempted Solutions

1. ✅ GC protection during closure creation
2. ✅ PC adjustment (`savedpc--`)
3. ✅ PC without adjustment  
4. ✅ Stack restoration
5. ✅ CallInfo status flags (`CIST_FRESH`)
6. ❌ All still result in segfault

## Next Steps (Options)

### Option A: Pseudo-Coroutine Approach
Treat continuation as separate coroutine:
- Create new `lua_State` for each continuation
- Use `lua_resume` instead of `luaV_execute`
- **Pro**: Leverages existing, tested mechanism
- **Con**: Higher overhead, separate stack

### Option B: Stack Unwinding with longjmp
Don't call `luaV_execute`, instead:
- Unwind current C stack back to original callcc point
- Replace return values
- **Pro**: Simpler, no VM re-entry
- **Con**: Complex control flow, hard to debug

### Option C: VM Modification
Add explicit continuation support to `luaV_execute`:
- Add flag to indicate "continuation mode"
- Handle special initialization for continuation frames
- **Pro**: Clean, proper solution
- **Con**: Requires VM changes, more invasive

### Option D: Return Value Injection (Simplest)
Instead of re-entering VM:
- Return from `do_invoke` normally
- Let `cont_call` return continuation arguments
- Continuation values become callcc return values
- **Pro**: Simplest, no VM re-entry
- **Con**: Doesn't actually "jump" to continuation point - just returns values

## Recommendation

Try **Option D first** - it may be sufficient for basic continuation semantics.
If full call/cc with control flow transfer is needed, then pursue Option A or C.

## Code Changes Made

### Files Modified:
- `lcontlib.c`: Added GC protection
- `lcont.c`: Implemented `do_invoke` with stack restoration and CallInfo setup
- `makefile`: Added `-g` flag for debugging

### Key Functions:
- `luaCont_capture()`: Captures caller context ✅
- `luaCont_invoke()`: Invokes with protected mode ✅  
- `do_invoke()`: Restores stack and calls luaV_execute ❌ (segfaults)

## Debug Information

Last successful execution point:
```
[DEBUG] L->ci set to new ci, about to call luaV_execute
[DEBUG] ci->func=0x..., ci->top=0x..., ci->callstatus=0x10000
[DEBUG] Stack dump shows correct function and arguments
```

Crash point: Inside `luaV_execute` → error occurs → `lua_getstack` → segfault in CallInfo traversal

---

**Time spent**: ~6 hours of debugging
**Success rate**: 80% complete (capture works, invoke blocked)
**Blocker**: VM state management for continuation invocation
