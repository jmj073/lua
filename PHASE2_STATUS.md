# Phase 2 구현 상태

## 현재 완료 (Phase 2.0)

### ✅ 구조 설계 완료
- Continuation이 caller context를 저장하도록 수정
- `resume_pc`: callcc 호출 직후 instruction
- `func_offset`: caller 함수 스택 위치
- `proto`: 함수 프로토타입 (GC 안전성)

### ✅ Capture 구현 완료
```c
// luaCont_capture now captures CALLER context
caller_ci = L->ci->previous;  // Find Lua caller
cont->resume_pc = caller_ci->u.l.savedpc;  // PC after callcc!
// Copy caller's stack, not internal function
```

### ✅ GC 통합 완료
- Proto 참조 마킹
- 스택 값 마킹
- 메모리 해제 구현

### ✅ Phase 1 기능 유지
- Direct return: 작동
- Immediate k() call: 작동
- Error-based unwinding: 작동

## 현재 작동하지 않는 것

### ❌ Saved Continuation 호출
```lua
local saved
local x = callcc(fn(k) {
  saved = k
  return 10
})
-- x = 10

saved(20)  -- ❌ Error: <<CONTINUATION_INVOKED>>
-- Should: x = 20, execution continues after callcc
```

**이유**: Phase 1의 error-based mechanism은 즉시 호출에만 작동함

## 다음 구현 필요 (Phase 2.5)

### 🔥 luaCont_invoke 재구현

**목표**: 저장된 PC에서 실행 재개

**필요한 작업**:

1. **스택 복원**
   ```c
   StkId func = restorestack(L, cont->func_offset);
   // Copy cont->stack to func
   ```

2. **CallInfo 생성**
   ```c
   CallInfo *ci = luaE_extendCI(L);
   ci->u.l.savedpc = cont->resume_pc;  // ← 핵심!
   ci->func = func;
   ci->top = func + cont->stack_size;
   ```

3. **인자 배치**
   ```c
   // Continuation args become callcc "return values"
   for (i = 0; i < nargs; i++) {
     setobj(L, s2v(func + 1 + i), &args[i]);
   }
   ```

4. **VM 재진입**
   ```c
   L->ci = ci;
   luaV_execute(L, ci);  // Resume from saved PC!
   ```

**문제**: 
- `luaV_execute`는 무한 루프
- C stack 문제: cont_call의 C frame이 남아있음

**해결 방안**:
- Option A: longjmp 사용 (callec 패턴)
- Option B: Tail call로 구현
- Option C: Protected call wrapper

## 예상 난이도 및 시간

**Phase 2.5 (Basic Invoke)**:
- 난이도: ⭐⭐⭐⭐
- 시간: 3-5시간
- VM 재진입 메커니즘 이해 필요

**Phase 2.9 (Full Multi-shot)**:
- 난이도: ⭐⭐⭐⭐⭐
- 시간: 2-4시간
- Edge cases, 안정성, 테스트

**총 예상**: 5-9시간

## 성공 기준

```lua
-- Must work:
local saved
print("A")
local x = callcc(fn(k) {
  saved = k
  return "first"
})
print("B:", x)

if x == "first" {
  saved("second")  -- Should jump back to callcc!
}
print("C")

-- Expected output:
-- A
-- B: first
-- B: second  ← Jumped back!
-- C
```

## 다음 즉시 할 일

1. `luaV_execute` 분석 (30분)
2. Coroutine resume 메커니즘 참고 (30분)
3. Invoke 구현 시도 (2-3시간)
4. 디버깅 및 테스트 (1-2시간)

---

**현재 위치**: Phase 2.0 완료 ✅
**다음 목표**: Phase 2.5 - VM 재진입 구현 🎯
**최종 목표**: 세계 구하기! 🌍
