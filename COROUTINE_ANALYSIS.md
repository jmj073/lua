# Coroutine Resume 메커니즘 분석

## 핵심 발견

### Coroutine Resume의 실제 동작

```c
// ldo.c:926
static void resume (lua_State *L, void *ud) {
  int n = *(cast(int*, ud));
  StkId firstArg = L->top.p - n;
  CallInfo *ci = L->ci;
  
  if (L->status == LUA_OK)  /* starting? */
    ccall(L, firstArg - 1, LUA_MULTRET, 0);
  else {  /* resuming from yield */
    L->status = LUA_OK;
    if (isLua(ci)) {  /* Lua frame */
      ci->u.l.savedpc--;  // Adjust PC
      L->top.p = firstArg;
      luaV_execute(L, ci);  // ⭐ 여기가 핵심!
    }
    // ...
  }
}
```

## Continuation과의 차이점

### Coroutine (작동함)
```
lua_resume called
  → luaD_rawrunprotected(resume)
    → resume()
      → luaV_execute(L, ci)
        → ... execute and return normally ...
    ← return to resume()
  ← return to lua_resume
← return to caller
```

**특징**: `luaD_rawrunprotected`가 **보호된 환경**에서 실행

### Our Continuation (실패함)
```
cont_call (C function)
  → luaCont_invoke()
    → luaV_execute(L, ci)
      → ... 어디로 돌아가야 할지 모름! ...
    ← ???
```

**문제**: Protected mode 없음, C stack frame이 잘못됨

## 해결책: Protected Call 사용!

### 올바른 패턴

```c
// Continuation invoke를 protected mode에서 실행
static void invoke_protected(lua_State *L, void *ud) {
  Continuation *cont = (Continuation *)ud;
  
  // Restore context
  restore_stack(L, cont);
  CallInfo *ci = create_callinfo(L, cont);
  L->ci = ci;
  
  // Resume execution
  luaV_execute(L, ci);
}

void luaCont_invoke(...) {
  // Call in protected mode!
  TStatus status = luaD_rawrunprotected(L, invoke_protected, cont);
  
  // Handle status
  if (status != LUA_OK) {
    // Error handling
  }
}
```

## 왜 Protected Mode가 필요한가?

1. **Stack Overflow 보호**
2. **Error Propagation** - VM 에러를 catch
3. **Clean Stack Management** - setjmp/longjmp 자동 처리
4. **VM Expectations** - luaV_execute는 protected 환경을 기대

## Coroutine vs Continuation

| Aspect | Coroutine | Continuation |
|--------|-----------|--------------|
| Suspend Point | yield() call | callcc() call |
| Resume Point | After yield | After callcc |
| State | Thread has status | Continuation object |
| C Stack | Separate thread | Same thread |
| Multi-shot | No (consumed) | Yes (reusable) |

## 핵심 인사이트

**Coroutine은 separate thread이므로 C stack 독립적**
**Continuation은 same thread이므로 C stack 문제 발생**

### 해결 방법

Option 1: **Protected Mode로 실행**
- `luaD_rawrunprotected` 사용
- Coroutine 패턴 모방
- VM이 예상하는 환경 제공

Option 2: **C Stack Unwinding**
- longjmp 사용
- 하지만 multi-shot에 문제

Option 3: **Pseudo-Coroutine**
- Continuation을 별도 thread처럼 취급
- 복잡하지만 가장 깔끔

## 추천: Option 1 (Protected Mode)

```c
// Step 1: Continuation invoke를 protected function으로
struct InvokeData {
  Continuation *cont;
  int nargs;
};

static void do_invoke(lua_State *L, void *ud) {
  struct InvokeData *data = (struct InvokeData *)ud;
  // ... restore and execute ...
  luaV_execute(L, ci);
}

// Step 2: cont_call에서 protected call
int cont_call(lua_State *L) {
  struct InvokeData data = {cont, nargs};
  TStatus st = luaD_rawrunprotected(L, do_invoke, &data);
  
  if (st == LUA_OK) {
    return lua_gettop(L);
  } else {
    return lua_error(L);
  }
}
```

## 다음 단계

1. ✅ Protected mode wrapper 구현
2. ✅ Error handling 추가
3. ✅ Stack management 정리
4. ✅ 테스트

**예상 시간**: 1-2시간
**성공 확률**: 높음 (Coroutine 패턴 검증됨)

---

**결론**: longjmp가 아니라 **protected mode execution**이 답이다! 🎯
