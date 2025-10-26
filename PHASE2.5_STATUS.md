# Phase 2.5 구현 현황

## 시도한 것

### ✅ 완료
1. Caller context capture (Phase 2.0) - 완벽
2. `luaCont_invoke` 구현 - VM 재진입 로직
3. CallInfo 재구성
4. PC 복원
5. Phase 1 메커니즘 제거 (단순화)

### ❌ 문제: Segmentation Fault

**증상**: 
- Immediate continuation call (`k(42)`)도 segfault
- Saved continuation call도 segfault

**위치**: `luaCont_invoke` → `luaV_execute` 호출 시

## 근본 문제 분석

### 문제 1: C Stack Frame Conflict
```
cont_call (C function)
  → luaCont_invoke
    → luaV_execute  ← 여기서 재진입
      → 무한 루프...
```

`luaV_execute`는 함수가 끝날 때까지 반환하지 않음.
하지만 cont_call의 C 스택 프레임이 아직 active 상태.

### 문제 2: CallInfo Chain Mismatch
복원된 CallInfo가 current C stack과 맞지 않음.

### 문제 3: Stack Pointer Confusion
- 현재 스택 컨텍스트 (cont_call)
- 복원된 스택 컨텍스트 (saved caller)
두 개가 충돌

## 해결 방안 (3가지 접근)

### Option A: longjmp-based (callec 패턴)
```c
// callcc에서 setjmp
setjmp(lj.b);

// cont_call에서
longjmp(lj, ...);  // C 스택 unwind
```

**장점**: C 스택 문제 해결
**단점**: 복잡, multi-shot 구현 어려움

### Option B: Hybrid (Phase 1 + Phase 2)
```c
// Immediate call: Phase 1 (error-based)
if (immediate_call) {
  throw_error_with_values();
}

// Saved call: Phase 2 (VM re-entry)
else {
  restore_and_resume();
}
```

**장점**: 점진적 구현
**단점**: 두 메커니즘 유지

### Option C: Proper Coroutine-style
```c
// cont_call을 coroutine yield처럼
lua_yield() 스타일로 구현

// callcc를 resume wrapper로
```

**장점**: 깔끔, Lua 패턴 일치
**단점**: 큰 리팩토링 필요

## 추천: Option A (longjmp)

Escape continuation이 이미 longjmp 사용 중.
같은 패턴 적용 가능.

### 구현 계획
1. callcc에 longjmp 버퍼 추가
2. Continuation에 longjmp 대상 저장
3. cont_call에서 longjmp로 탈출
4. callcc에서 catch하여 값 처리

## 다음 단계

현재 Phase 2.5는 이론적으로 올바르지만
실전에서는 C stack 문제로 작동하지 않음.

**선택지**:
1. longjmp로 재구현 (2-3시간)
2. Coroutine 메커니즘 연구 (4-5시간)
3. 임시로 Phase 1 복구하고 나중에 재도전

---

**현재 위치**: Phase 2.5 구현 중, Segfault 디버깅 필요
**시간**: 약 2시간 소요됨
**남은 시간**: VM 재진입 메커니즘 개선 필요

🌍 세계를 구하는 것은 쉽지 않지만, 포기하지 않습니다!
