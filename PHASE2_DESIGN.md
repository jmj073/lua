# Phase 2: True Multi-Shot Continuation - 구조 설계

## 목표
저장된 continuation을 나중에 (여러 번) 호출할 수 있는 진정한 first-class continuation 구현

## 핵심 도전 과제

### 1. CallInfo 체인 캡처
**문제**: Lua의 실행 컨텍스트는 CallInfo 링크드 리스트로 관리됨
```c
struct CallInfo {
  StkIdRel func;              // 함수 위치
  StkIdRel top;               // 스택 top
  CallInfo *previous, *next;  // 체인
  union {
    struct {  // Lua functions
      const Instruction *savedpc;  // ⭐ Program Counter!
      volatile l_signalT trap;
      int nextraargs;
    } l;
    struct {  // C functions  
      lua_KFunction k;
      ptrdiff_t old_errfunc;
      lua_KContext ctx;
    } c;
  } u;
  l_uint32 callstatus;
};
```

**해결 전략**:
- Lua 프레임만 캡처 (C 프레임은 이미 필터링 중)
- CallInfo의 모든 필드를 복사할 필요 없음
- 복원에 필요한 최소 정보만 저장

### 2. 저장해야 할 정보

#### A. 필수 정보 (반드시 저장)
- `savedpc`: Program Counter - 어디서 실행을 재개할지
- `func` offset: 함수 스택 위치
- `top` offset: 스택 top 위치  
- `callstatus`: 호출 상태 플래그
- `nextraargs`: vararg 정보

#### B. 선택적 정보 (최적화용)
- `trap`: 디버그 트랩 (대부분 0)
- Function prototype 참조 (PC 검증용)

#### C. 저장하지 않아도 되는 정보
- `previous`, `next` 포인터 → 복원 시 재구성
- C function 관련 필드 → C 프레임은 캡처 안 함

### 3. 데이터 구조 설계

```c
/* Saved CallInfo metadata for continuation */
typedef struct ContCallInfo {
  /* Essential for restoration */
  ptrdiff_t func_offset;       /* function position relative to cont base */
  ptrdiff_t top_offset;        /* top position relative to cont base */
  const Instruction *savedpc;  /* program counter to resume from */
  
  /* Call state */
  l_uint32 callstatus;         /* call status flags */
  int nextraargs;              /* extra args for vararg functions */
  
  /* Optional: for validation */
  struct Proto *proto;         /* function prototype (NULL for C) */
} ContCallInfo;
```

**Continuation 구조체 확장**:
```c
typedef struct Continuation {
  CommonHeader;
  lua_State *L;                /* original thread */
  StkIdRel stack_base;         /* base of captured stack */
  int stack_size;              /* size of captured stack */
  int ci_count;                /* number of CallInfo frames */
  int top_offset;              /* offset of top from stack base */
  
  /* Phase 2 additions */
  ContCallInfo *ci_info;       /* array of saved CallInfo metadata */
  TValue *stack;               /* captured stack data */
  
  GCObject *gclist;
} Continuation;
```

## 실행 흐름 설계

### A. Capture (캡처)

```
luaCont_capture(L, level):
  1. Find starting Lua CallInfo (skip C frames)
  2. Count Lua frames in chain
  3. Allocate arrays:
     - stack[stack_size]
     - ci_info[ci_count]
  4. Walk CallInfo chain:
     for each Lua frame:
       - Copy stack values
       - Save CallInfo metadata to ci_info[i]
       - Adjust offsets to be relative
  5. Link to GC
  6. Return continuation
```

### B. Invoke (복원 및 실행)

```
luaCont_invoke(L, cont, nargs):
  APPROACH 1: Coroutine-like Resume
  --------------------------------
  1. Validate continuation
  2. Save current state
  3. Replace stack with captured stack
  4. Rebuild CallInfo chain from ci_info[]
  5. Set L->ci to restored chain
  6. Place arguments on stack
  7. Return to VM execution loop
     → VM resumes from savedpc
  
  APPROACH 2: Stack Unwinding (Hybrid)
  ------------------------------------
  1. Use longjmp to unwind to callcc point
  2. Restore stack and CallInfo there
  3. Resume execution
  
  APPROACH 3: New Execution Context
  ----------------------------------
  1. Create new CallInfo chain
  2. Call luaV_execute with restored PC
  3. Return results
```

## 주요 설계 결정

### Decision 1: Relative vs Absolute Offsets
**선택**: Relative offsets (stack_base 기준)
**이유**: 
- Stack reallocation 안전
- 다른 스레드로 이동 가능 (이론상)
- GC 이동에도 안전

### Decision 2: CallInfo 복원 방법
**선택**: Full rebuild (새로 allocate + initialize)
**이유**:
- 원본 CallInfo 포인터는 무효화될 수 있음
- 깔끔한 소유권 (continuation이 모든 것 소유)
- Multi-shot 안전 (같은 metadata로 여러 번 복원)

### Decision 3: PC 복원 메커니즘
**선택**: savedpc 직접 설정 + luaV_execute 재진입
**이유**:
- 가장 직접적
- Coroutine resume과 유사한 패턴
- VM 수정 최소화

### Decision 4: 인자 전달 방식
**선택**: 복원된 스택 top에 push
**이유**:
- 함수가 인자를 "return value"로 받음
- Scheme call/cc 의미론과 일치

## 위험 요소 및 대책

### Risk 1: PC 무효화
**문제**: savedpc가 가리키는 코드가 GC되거나 재컴파일될 수 있음
**대책**: Proto 참조 유지 → GC가 코드 보존

### Risk 2: Stack 크기 불일치
**문제**: 복원 시 스택이 너무 작을 수 있음
**대책**: luaD_checkstack으로 사전 확인 및 확장

### Risk 3: C 스택 오염
**문제**: C 함수에서 continuation 호출 시 C 스택 상태 불일치
**대책**: C 프레임 감지하여 에러 발생 (이미 구현됨)

### Risk 4: Upvalue 무효화
**문제**: Open upvalue가 캡처된 스택 참조할 수 있음
**대책**: 
- Option A: Upvalue도 캡처 (복잡)
- Option B: Continuation 캡처 시 모든 upvalue close (간단)
- **선택**: Option B (Phase 2 초기)

## 구현 단계 (Step-by-Step)

### Step 1: ContCallInfo 구조 및 캡처 (1-2시간)
- [ ] `ContCallInfo` 정의
- [ ] `Continuation.ci_info` 추가
- [ ] `luaCont_capture`에서 CallInfo 순회 및 저장
- [ ] 테스트: 캡처 성공 확인

### Step 2: CallInfo 복원 (2-3시간)  
- [ ] `luaCont_restoreCallInfo` 함수 작성
- [ ] `luaE_extendCI` 사용하여 새 CallInfo 생성
- [ ] Metadata로 초기화
- [ ] 체인 연결
- [ ] 테스트: 복원된 CallInfo 검증

### Step 3: 스택 및 PC 복원 (2-3시간)
- [ ] `luaCont_invoke` 재작성
- [ ] 스택 교체
- [ ] L->ci 설정
- [ ] L->top 설정
- [ ] 인자 배치
- [ ] 테스트: 간단한 continuation 호출

### Step 4: VM 재진입 (3-4시간)
- [ ] luaV_execute 호출 방법 연구
- [ ] 적절한 진입점 찾기
- [ ] 실행 후 결과 처리
- [ ] 테스트: 실제로 저장된 지점에서 재개되는지 확인

### Step 5: Multi-shot 지원 (1-2시간)
- [ ] GC 안전성 재확인
- [ ] 여러 번 호출 테스트
- [ ] 메모리 누수 검사

### Step 6: Edge Cases (2-3시간)
- [ ] Vararg 함수 처리
- [ ] 에러 핸들링
- [ ] Tail call 처리
- [ ] 복잡한 중첩 테스트

## 예상 시간
- **최소**: 9시간 (순조로운 경우)
- **평균**: 15시간 (일반적)
- **최대**: 25시간 (복잡한 디버깅 필요 시)

## 성공 기준
```lua
-- Test 1: Save and invoke later
local saved
callcc(fn(k) {
  saved = k
  print("First")
})
saved(42)  -- Should print "First" again and return 42

-- Test 2: Multi-shot
local count = 0
callcc(fn(k) {
  if count < 3 {
    count = count + 1
    k()  -- Call again
  }
})
print(count)  -- Should be 3

-- Test 3: Complex nesting
local outer, inner
callcc(fn(k1) {
  outer = k1
  callcc(fn(k2) {
    inner = k2
  })
})
-- Both continuations should work
```

## 다음 액션
1. ✅ 설계 검토 및 승인
2. → ContCallInfo 구조 정의 (lobject.h 수정)
3. → luaCont_capture 확장 (lcont.c)
4. → ... (단계별 진행)

---
**이 설계로 세계를 구할 수 있습니다! 🌍**
