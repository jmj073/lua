# Phase 2: 올바른 Call/CC 의미론

## 🔥 핵심 깨달음

Call/CC는 **callcc를 호출한 지점 이후의 continuation**을 캡처한다!

### 올바른 예제

```lua
-- Example 1: Basic continuation
local saved
local x = callcc(fn(k) {
  saved = k
  return 10
})
print("x =", x)  -- "x = 10"

-- 나중에 continuation 호출
saved(20)
-- 위 줄이 실행되면:
-- 1. callcc 호출 지점으로 "돌아감"
-- 2. callcc의 반환값이 20이 됨
-- 3. print("x =", x) 가 다시 실행됨
-- Output: "x = 20"
```

```lua
-- Example 2: Loop with continuation
local count = 0
local loop
count = count + 1
local x = callcc(fn(k) {
  loop = k
  return count
})
print("Count:", x)

if count < 3 {
  loop(count)  -- 다시 callcc 지점으로!
}
-- Output:
-- Count: 1
-- Count: 2  
-- Count: 3
```

```lua
-- Example 3: Nested calls
print("Start")
local result = callcc(fn(k) {
  k(42)
  return 99  -- 도달 안 됨
})
print("Result:", result)
print("End")

-- Output:
-- Start
-- Result: 42
-- End
```

## 캡처해야 할 것

### ❌ 잘못된 것 (이전 설계)
- callcc에 전달된 함수의 실행 컨텍스트
- 함수 내부의 PC

### ✅ 올바른 것 (수정된 설계)
- **callcc를 호출한 caller의 실행 컨텍스트**
- **callcc 호출 직후의 PC**
- callcc 호출 시점의 스택 상태

## 구현 전략 재설계

### 캡처 시점 (luaCont_capture)

```c
// callcc is called from Lua code:
// local x = callcc(fn(k) { ... })
//           ^
//           capture THIS point

luaCont_capture(L, level) {
  // level = 0: callcc 자체 (C function)
  // level = 1: callcc를 호출한 Lua 함수 ← 여기를 캡처!
  
  ci = L->ci;  // 현재 = callcc (C function)
  ci = ci->previous;  // callcc의 caller (Lua function)
  
  // 이 CallInfo의:
  // - savedpc: callcc 호출 **다음** instruction
  // - func, top: 스택 상태
  // - 이것들을 저장!
}
```

### 복원 시점 (luaCont_invoke)

```c
luaCont_invoke(L, cont, args) {
  // 1. 저장된 스택 복원
  // 2. CallInfo 복원
  // 3. savedpc를 callcc 호출 **다음**으로 설정
  // 4. args를 callcc의 "반환값"으로 스택에 배치
  // 5. VM 실행 재개
  
  // VM은 callcc 다음 instruction부터 계속 실행!
}
```

## 새로운 설계 도전 과제

### Challenge 1: Caller Context 캡처
**문제**: callcc는 C 함수인데, 어떻게 caller를 캡처?
**해결**: 
- callcc 실행 시 L->ci->previous가 Lua caller
- 그 CallInfo의 savedpc가 **callcc 호출 다음 instruction**
- 이미 올바른 위치를 가리킴!

### Challenge 2: 스택 프레임 경계
**문제**: callcc 내부 함수는 별도 프레임, caller는 상위 프레임
**해결**:
- Caller 프레임의 스택만 캡처
- callcc 내부 함수 프레임은 버림
- Continuation 호출 시 caller 프레임만 복원

### Challenge 3: 반환값 배치
**문제**: continuation 인자를 callcc "반환값"으로 어떻게?
**해결**:
```c
// VM에서 function call 반환 시:
// 반환값들이 func 위치부터 배치됨
// 
// Continuation 복원 시:
// 1. func 위치에 args 배치
// 2. nresults 설정
// 3. VM이 자동으로 처리
```

## 수정된 Continuation 구조

```c
typedef struct Continuation {
  CommonHeader;
  lua_State *L;
  
  /* Captured CALLER context (not callcc function) */
  StkIdRel caller_func;      /* caller's function position */
  StkIdRel caller_base;      /* caller's stack base */
  int caller_stack_size;     /* size of caller's stack */
  
  /* Caller's CallInfo */
  const Instruction *caller_pc;  /* PC after callcc call */
  l_uint32 callstatus;
  Proto *proto;              /* for validation */
  
  /* Captured stack */
  TValue *stack;
  
  GCObject *gclist;
} Continuation;
```

## 구현 계획 수정

### Step 1: Caller Context 캡처 ✨
```c
luaCont_capture(L, level):
  ci = L->ci;  // callcc (C function)
  
  // Find caller (skip C frames)
  while (ci && !isLua(ci)) {
    ci = ci->previous;
  }
  if (!ci) return NULL;
  
  // Capture CALLER context
  cont->caller_pc = ci->u.l.savedpc;  // Already points to next instruction!
  cont->caller_func = ci->func;
  cont->caller_base = ci->func + 1;  // First local
  
  // Capture caller's stack
  stack_size = ci->top - ci->func;
  copy_stack(cont, ci->func, stack_size);
```

### Step 2: Continuation 복원 및 실행 ✨
```c
luaCont_invoke(L, cont, nargs):
  // Restore caller's stack
  restore_stack(L, cont);
  
  // Create new CallInfo for caller
  ci = luaE_extendCI(L);
  ci->func = cont->caller_func;
  ci->top = cont->caller_base + cont->caller_stack_size;
  ci->u.l.savedpc = cont->caller_pc;  // Resume after callcc!
  ci->callstatus = cont->callstatus;
  
  // Place continuation args as "return values" from callcc
  // These go where callcc would have returned
  for (i = 0; i < nargs; i++) {
    setobj(L, ci->func + i, &args[i]);
  }
  
  // Set L->ci and resume VM
  L->ci = ci;
  luaV_execute(L, ci);  // VM continues from caller_pc!
```

## 올바른 성공 기준

```lua
-- Test 1: Basic continuation
print("A")
local saved
local x = callcc(fn(k) {
  saved = k
  return "first"
})
print("B: x =", x)
if x == "first" {
  saved("second")
}
print("C")

-- Expected output:
-- A
-- B: x = first
-- B: x = second  ← callcc 지점으로 돌아감!
-- C
```

```lua
-- Test 2: Loop
local i = 0
local loop
i = i + 1
local x = callcc(fn(k) {
  loop = k
  return i
})
print("Iteration:", x)
if i < 3 {
  loop(i)
}

-- Expected:
-- Iteration: 1
-- Iteration: 2
-- Iteration: 3
```

```lua
-- Test 3: Early return
local fn compute() {
  local result = callcc(fn(k) {
    if some_condition {
      k(42)  -- "return" 42 from compute
    }
    return do_expensive_work()
  })
  return result
}
```

## 결론

이것이 **진정한 first-class continuation**입니다!

**난이도**: ⭐⭐⭐⭐⭐ (매우 높음)
**하지만 가능합니다!** 🌍

핵심은:
1. Caller context를 캡처
2. Caller의 PC (callcc 다음)를 저장
3. 복원 시 그 지점으로 돌아가기

Phase 1의 error-based unwinding은 실제로 부분적으로 맞았습니다 - 
callcc 내부에서 탈출하는 것까지는 맞았지만,
진정한 continuation은 **caller로 돌아가는 것**입니다!
