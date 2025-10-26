# Phase 2 구현 계획: True Continuation

## 현재 상황 분석

### Phase 1이 구현한 것
```lua
callcc(fn(k) {
  k(42)  -- 여기서 탈출하여 callcc가 42 반환
  return 99
})
```
✅ 작동: Error-based unwinding으로 callcc 내부에서 탈출

### Phase 2가 구현해야 할 것
```lua
local saved
local x = callcc(fn(k) {
  saved = k
  return 10
})
print(x)  -- "10"

saved(20)  -- callcc 지점으로 돌아가서 x = 20으로 만듦
-- print(x) 다시 실행 → "20" 출력
```

## 핵심 차이점

| Aspect | Phase 1 | Phase 2 (필요) |
|--------|---------|----------------|
| 캡처 대상 | callcc 내부 함수 컨텍스트 | callcc **caller** 컨텍스트 |
| 저장 PC | (저장 안 함) | Caller의 savedpc (callcc 다음 instruction) |
| 복원 시 | Error throw → callcc 반환 | Caller PC로 jump → 실행 재개 |
| 스택 | 내부 함수 스택 | Caller 스택 |

## 구현 Step-by-Step

### Step 1: Caller Context 식별 및 캡처 ✅

**목표**: callcc를 호출한 Lua 함수의 컨텍스트 찾기

```c
// In luaCont_capture(L, level)

CallInfo *ci = L->ci;  // 현재 = callcc (C function)

// callcc의 caller 찾기 (Lua frame이어야 함)
CallInfo *caller_ci = ci->previous;
while (caller_ci && !isLua(caller_ci)) {
  caller_ci = caller_ci->previous;
}

if (!caller_ci) {
  return NULL;  // No Lua caller
}

// Caller의 정보 캡처
const Instruction *resume_pc = caller_ci->u.l.savedpc;
// ↑ 이미 callcc 호출 **다음** instruction을 가리킴!

StkId caller_func = caller_ci->func.p;
StkId caller_top = caller_ci->top.p;
int stack_size = caller_top - caller_func;

// 스택 복사
for (int i = 0; i < stack_size; i++) {
  setobj(L, &cont->stack[i], s2v(caller_func + i));
}

// Metadata 저장
cont->resume_pc = resume_pc;
cont->func_offset = savestack(L, caller_func);
cont->stack_size = stack_size;
cont->proto = clLvalue(s2v(caller_func))->p;  // Function prototype
```

**테스트**:
```lua
local function test() {
  local x = callcc(fn(k) {
    print("Capturing...")
    return 1
  })
  print("x =", x)
}
test()
```
캡처가 성공하고 에러 없이 실행되어야 함.

---

### Step 2: Continuation 구조 확장 ✅

**파일**: `lobject.h`

```c
typedef struct Continuation {
  CommonHeader;
  lua_State *L;
  
  /* Phase 2: Caller context */
  const Instruction *resume_pc;   /* PC to resume (after callcc call) */
  ptrdiff_t func_offset;          /* Caller function offset */
  int stack_size;                 /* Caller stack size */
  Proto *proto;                   /* Caller function prototype */
  l_uint32 callstatus;            /* Caller call status */
  
  /* Captured stack */
  TValue *stack;
  
  /* GC */
  GCObject *gclist;
  
  /* Deprecated Phase 1 fields (remove later) */
  StkIdRel stack_base;
  int ci_count;
  int top_offset;
  void *ci_base;
} Continuation;
```

---

### Step 3: Invoke 메커니즘 재설계 🔥

**가장 어려운 부분**: 저장된 PC에서 실행 재개

#### Option A: Direct VM Entry (추천)
```c
void luaCont_invoke(lua_State *L, Continuation *cont, int nargs) {
  StkId func;
  CallInfo *ci;
  int i;
  
  // Validate
  if (!luaCont_isvalid(cont, L)) {
    luaG_runerror(L, "invalid continuation");
  }
  
  // Restore stack
  func = restorestack(L, cont->func_offset);
  luaD_checkstack(L, cont->stack_size + nargs);
  
  for (i = 0; i < cont->stack_size; i++) {
    setobj(L, s2v(func + i), &cont->stack[i]);
  }
  
  // Create new CallInfo for restored context
  ci = luaE_extendCI(L);
  ci->func.p = func;
  ci->top.p = func + cont->stack_size;
  ci->u.l.savedpc = cont->resume_pc;  // ← 핵심!
  ci->u.l.trap = 0;
  ci->u.l.nextraargs = 0;
  ci->callstatus = cont->callstatus | CIST_FRESH;
  
  // Place continuation arguments as callcc "return values"
  // callcc는 1개 이상의 값을 반환할 수 있음
  L->top.p = func + 1;  // Reset top
  for (i = 0; i < nargs; i++) {
    // Arguments are already on stack from cont_call
    // Just adjust top
    L->top.p++;
  }
  
  // Set current CI and resume
  L->ci = ci;
  
  // ⭐ VM 재진입
  luaV_execute(L, ci);
}
```

**문제**: `luaV_execute`는 무한 루프를 돌며, 함수가 반환될 때만 종료됨.
Continuation invoke에서 어떻게 제어를 돌려받을 것인가?

#### 해결책: Continuation Invocation은 **tail call**처럼 동작

```c
// cont_call에서:
static int cont_call(lua_State *L) {
  Continuation *cont = ...;
  int nargs = lua_gettop(L);
  
  // Invoke는 반환하지 않고 실행을 이어감
  // 따라서 cont_call이 "꼬리 호출"처럼 동작
  
  luaCont_invoke(L, cont, nargs);
  
  // Unreachable (VM이 계속 실행)
  return 0;
}
```

**하지만**: 이것도 문제가 있음. cont_call의 C stack frame이 남아있음.

#### 더 나은 해결책: longjmp 사용 (callec 패턴)

```c
void luaCont_invoke(lua_State *L, Continuation *cont, int nargs) {
  /* Setup for jump */
  lua_longjmp lj;
  
  /* Prepare restored context */
  // ... (위의 스택/CallInfo 복원 코드)
  
  /* Use longjmp to unwind C stack and transfer control */
  // 이것은 복잡하지만 callec이 이미 비슷한 패턴 사용 중
}
```

---

### Step 4: 단순화된 접근 (초기 구현) ✨

**현실적 전략**: 완벽한 multi-shot보다 **작동하는 버전**을 먼저 만들기

```c
// Simplified Phase 2.0
void luaCont_invoke(lua_State *L, Continuation *cont, int nargs) {
  // 1. 저장된 스택을 현재 위치에 "복사"
  // 2. 에러를 throw하여 C stack unwind
  // 3. callcc wrapper가 catch하고 값 반환
  // 4. 하지만 이번에는 caller context를 저장했으므로
  //    나중에 더 정교하게 만들 수 있음
  
  // For now: Store in registry and throw
  // (Phase 1과 유사하지만 caller context 사용)
}
```

---

## 점진적 구현 전략

### Phase 2.0: Caller Context Capture (지금)
- ✅ Caller 식별
- ✅ Resume PC 저장
- ✅ Caller 스택 캡처
- ⚠️ Invoke는 Phase 1 메커니즘 사용 (error-based)

**결과**: 
- Direct return 작동
- Immediate k() 호출 작동
- ❌ Saved continuation 작동 안 함 (아직)

### Phase 2.5: VM Re-entry (다음)
- PC 복원
- CallInfo 재생성
- VM 재진입
- C stack 문제 해결

**결과**:
- ✅ Saved continuation 1회 호출 가능
- ⚠️ Multi-shot은 불안정할 수 있음

### Phase 2.9: Full Multi-shot (최종)
- GC 안전성 완벽히
- Multiple invocation 테스트
- Edge cases 처리

**결과**:
- ✅ 완전한 multi-shot continuation

---

## 다음 즉시 할 일

1. **Caller identification 구현** (30분)
   - `luaCont_capture`에서 caller CI 찾기
   - Resume PC 저장

2. **구조 업데이트** (30분)
   - `Continuation` 구조 확장
   - GC traversal 업데이트

3. **간단한 테스트** (30분)
   - Caller context 캡처 확인
   - Phase 1 기능 유지 확인

4. **VM re-entry 연구** (1-2시간)
   - `luaV_execute` 분석
   - Coroutine resume 메커니즘 참고
   - longjmp 패턴 이해

**총 예상 시간**: 3-4시간 (Phase 2.0 완성)

---

믿어주셔서 감사합니다. 이제 시작합니다! 🚀
