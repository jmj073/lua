# Phase 2 아키텍처 핵심 결정 사항

## ADR-001: CallInfo 복원 메커니즘

### 상황
Continuation 호출 시 저장된 실행 컨텍스트를 어떻게 복원할 것인가?

### 고려한 옵션

#### Option A: In-place Restoration
현재 스택 위치에서 CallInfo를 직접 수정
```c
// Pros: 간단, 빠름
// Cons: 현재 실행 컨텍스트 파괴, 에러 복구 어려움
ci->savedpc = saved_pc;
ci->func = restored_func;
```

#### Option B: Stack Swap + New CallInfo Chain
전체 스택을 교체하고 새로운 CallInfo 체인 생성
```c
// Pros: 깔끔, 격리됨, multi-shot 안전
// Cons: 메모리 사용량 증가
old_stack = L->stack;
L->stack = cont->stack;
L->ci = rebuild_callinfo_chain(cont);
```

#### Option C: Coroutine-style Context Switch
Coroutine처럼 context switching
```c
// Pros: 기존 메커니즘 재사용
// Cons: Coroutine과 interaction 복잡
lua_resume() 스타일 접근
```

### 결정: **Option B (Stack Swap + New CallInfo)**

**이유**:
1. **Multi-shot 안전성**: 원본 continuation 불변 유지
2. **격리**: 현재 실행과 독립적
3. **명확한 소유권**: Continuation이 모든 리소스 소유
4. **GC 안전**: 명확한 참조 관계
5. **디버깅 용이**: 각 invocation이 독립적

**Trade-off 수용**:
- 메모리 사용량 증가 → 허용 (세계 구하는 게 우선)
- 약간의 성능 오버헤드 → 허용 (정확성이 더 중요)

---

## ADR-002: Program Counter 복원 방법

### 상황  
어떻게 저장된 instruction에서 실행을 재개할 것인가?

### 고려한 옵션

#### Option A: VM 직접 진입
```c
L->ci->u.l.savedpc = cont->ci_info[i].savedpc;
luaV_execute(L, L->ci);
```

#### Option B: Protected Call Wrapper
```c
lua_pcall을 통한 간접 호출
```

#### Option C: longjmp-based Jump
```c
setjmp/longjmp로 직접 점프
```

### 결정: **Option A (VM 직접 진입)**

**이유**:
1. **가장 직접적**: VM 내부에서 실행
2. **Coroutine과 일관성**: lua_resume과 유사한 패턴
3. **에러 처리**: 기존 VM 메커니즘 활용
4. **성능**: 추가 레이어 없음

---

## ADR-003: 스택 데이터 저장 방식

### 결정: **Deep Copy (TValue array)**

현재 구현:
```c
cont->stack = luaM_newvector(L, stack_size, TValue);
for (i = 0; i < stack_size; i++) {
  setobj(L, &cont->stack[i], s2v(stack_base + i));
}
```

**이유**:
1. **GC 안전**: GC가 적절히 추적
2. **독립성**: 원본 스택 변경에 영향 없음  
3. **단순성**: 복잡한 참조 추적 불필요

**대안 거부 이유**:
- Shallow copy: GC 위험
- COW (Copy-on-Write): 구현 복잡도 증가

---

## ADR-004: Upvalue 처리

### 상황
Open upvalue는 스택을 참조하는데, 스택이 교체되면 어떻게 되는가?

### 고려한 옵션

#### Option A: Upvalue도 캡처
모든 upvalue 복사 및 재연결

#### Option B: Capture 시 모든 Upvalue Close
```c
luaF_close(L, cont->stack_base);
```

#### Option C: Lazy Handling
복원 시에만 처리

### 결정: **Option B (Capture 시 Close) - Phase 2 초기**

**이유**:
1. **단순성**: 추가 복잡도 최소화
2. **안전성**: Dangling reference 방지
3. **점진적 개선 가능**: 나중에 Option A로 업그레이드 가능

**제약 사항 명시**:
- Upvalue가 있는 경우 동작이 제한될 수 있음
- 문서화 필요: "Continuation은 upvalue를 close함"

---

## ADR-005: 인자 전달 의미론

### 상황
`k(1, 2, 3)` 호출 시 이 값들을 어떻게 전달?

### 결정: **스택 Top에 Push → 함수 "반환값"으로 취급**

```lua
local a, b = callcc(fn(k) {
  -- ... some code ...
  k(1, 2)  -- continuation 호출
  -- unreachable
})
-- a=1, b=2
```

**이유**:
1. **Scheme 의미론**: 표준 call/cc와 일치
2. **직관적**: continuation은 "조기 return"처럼 동작
3. **구현 간단**: 스택 top에 push만 하면 됨

---

## ADR-006: C Frame 경계 처리

### 결정: **C Frame 감지 시 에러 발생 (유지)**

현재 구현대로 유지:
```c
if (!isLuaFrame(ci)) {
  return NULL;  /* Cannot capture C frame */
}
```

**이유**:
1. **안전성**: C 스택 불일치 방지
2. **명확성**: 에러로 명확히 표시
3. **현실적**: C 스택 캡처는 거의 불가능

**사용자 가이드**:
```lua
-- ✅ OK: Pure Lua
callcc(fn(k) { ... })

-- ❌ Error: C function in call chain  
string.gsub("test", ".", fn(x) {
  callcc(fn(k) { ... })  -- Error!
})
```

---

## ADR-007: Error Handling Strategy

### 결정: **Layered Error Handling**

1. **Capture 단계**: NULL 반환 + luaL_error
2. **Restore 단계**: luaG_runerror  
3. **Invoke 단계**: Protected execution (pcall 가능)

**이유**:
- 각 단계마다 적절한 에러 처리
- 사용자가 pcall로 wrap 가능
- 디버깅 정보 보존

---

## ADR-008: GC Integration

### 결정: **Continuation as First-Class GC Object**

이미 구현됨:
- `LUA_VCONT` 타입
- `traversecont` for marking
- `freeobj` for cleanup

**추가 요구사항 (Phase 2)**:
- `ci_info` 배열 GC 추적
- `Proto` 참조 마킹 (PC 유효성 보장)

```c
static l_mem traversecont (global_State *g, Continuation *cont) {
  int i;
  /* Mark stack values */
  for (i = 0; i < cont->stack_size; i++) {
    markvalue(g, &cont->stack[i]);
  }
  /* Mark proto references */
  for (i = 0; i < cont->ci_count; i++) {
    if (cont->ci_info[i].proto) {
      markobject(g, cont->ci_info[i].proto);
    }
  }
  return sizeof(Continuation) + 
         cont->stack_size * sizeof(TValue) +
         cont->ci_count * sizeof(ContCallInfo);
}
```

---

## 결론

이 아키텍처 결정들은:
1. ✅ **안전성 우선**: GC, 에러 처리 robust
2. ✅ **단순성 추구**: 복잡도 최소화
3. ✅ **점진적 개선**: Phase 3, 4로 확장 가능
4. ✅ **표준 준수**: Scheme call/cc 의미론 준수

**다음: 구현 시작** 🚀
