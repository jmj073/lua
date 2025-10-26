# Callcc 구현 세션 요약

## 날짜: 2025-10-26

## 🎯 목표
Result value passing 문제 해결 - continuation arguments가 result에 반영되지 않아 무한루프 발생

## ✅ 달성한 것

### 1. 문제 정확히 파악 ✓
**근본 원인 발견**:
- Captured PC가 가리키는 다음 instruction은 **특정 register(RA)**를 result로 사용
- 기존 구현은 arguments를 `func+1`에 배치
- 하지만 RA는 `func+1`이 아니라 **instruction의 A field**로 결정됨!

**해결 방향**:
```c
// savedpc-1에서 CALL instruction 분석
call_inst = *(L->ci->u.l.savedpc - 1);
ra_offset = GETARG_A(call_inst);
result_pos = L->ci->func.p + 1 + ra_offset;
// Arguments를 result_pos에 배치!
```

### 2. 이론적 해결책 구현 ✓
- `lcont.h`: `luaCont_doinvoke` 시그니처 변경 (void → int)
- `lopcodes.h` include 추가
- RA offset 추출 로직 구현
- OP_CALL에서 returned args 처리

### 3. Registry를 통한 Arguments 저장 시도
- Arguments를 context injection 전에 table에 저장
- Injection 후 retrieve
- **문제**: `free(): invalid pointer` 에러 발생

### 4. Clone Thread를 통한 Multi-shot 시도
- Thread cloning 구현
- Arguments를 clone의 stack에 저장
- **문제**: Offset 계산 문제로 무한루프 또는 정지

## ⚠️ 현재 상태: 막힘

### 주요 문제들

#### 1. Arguments 저장 위치
여러 접근법 시도했으나 모두 문제 발생:
- **Registry table**: free() 에러
- **Clone thread stack**: Injection 후 offset 문제
- **L->top.p 위**: Injection이 L->top 변경하여 무효화

#### 2. Context Injection의 복잡성
`luaV_injectcontext(L, clone)`는:
- L의 전체 스택을 clone의 스택으로 교체
- L->top, L->ci->func.p, L->ci->top.p 모두 변경
- Injection 전의 포인터들이 모두 무효화됨

#### 3. Multi-shot 지원의 난이도
- 원본 thread를 직접 수정하면 captured state 손상
- Clone을 만들면 메모리 관리와 offset 계산 복잡

## 📊 테스트 결과

### Before (이전 세션에서 작동)
```
test_jump_verify.lua:
  After callcc, result = 111
  After callcc, result = 222  ✓
  SUCCESS!
```

### After (현재)
```
test_simple_value.lua:
  r = 111
  Calling k(222)...
  (멈춤 - 무한루프 또는 정지)
```

## 🔍 남은 과제

### Priority 1: Arguments 전달 방법 재설계
현재 접근법의 근본적인 문제:
1. Context injection이 스택을 완전히 교체
2. 포인터 기반 저장이 무효화됨
3. Offset 기반 접근도 복잡함

**대안**:
- A. Injection 후 PC를 조정하여 arguments를 다시 읽게 만들기
- B. Continuation에 arguments를 미리 저장
- C. VM instruction 추가 (OP_CONT_INVOKE)

### Priority 2: 단순화
현재 코드가 너무 복잡함:
- 많은 임시 변수
- 복잡한 offset 계산
- 디버깅 어려움

**필요한 것**:
- 명확한 데이터 흐름
- 최소한의 포인터 조작
- 단계별 검증 가능한 구조

### Priority 3: Single-shot부터 작동시키기
Multi-shot은 나중에:
1. 먼저 원본 thread로 single-shot 작동
2. 그 다음 cloning 추가

## 💡 제안: 다른 접근법

### 접근법 A: PC 조정
Arguments를 스택에 배치한 후, PC를 CALL instruction 직후가 아닌 특별한 위치로 조정:
```c
// Pseudo-code
luaCont_doinvoke() {
  // 1. Arguments를 스택에 배치
  // 2. Context injection
  // 3. PC를 조정하여 배치된 arguments를 읽도록
  L->ci->u.l.savedpc = special_instruction;
}
```

### 접근법 B: Continuation 구조체에 저장
Capture 시점에 arguments도 저장:
```c
struct Continuation {
  ...
  TValue *pending_args;  // 다음 invoke 시 사용할 arguments
  int nargs;
}
```

### 접근법 C: Simpler Thread Swap
Context injection 대신 더 간단한 thread swap:
```c
// L의 주요 필드만 swap
swap(L->stack, clone->stack);
swap(L->ci, clone->ci);
swap(L->top, clone->top);
```

## 📝 코드 변경 사항

### 수정된 파일
- `lcont.h`: 시그니처 변경
- `lcont.c`: RA offset 추출 로직 추가 (80+ lines 변경)
- `lvm.c`: OP_CALL 수정 (5 lines)

### 시도한 구현들
1. Registry table 저장 (실패 - free 에러)
2. Clone thread + offset (실패 - 정지)
3. L->top.p 위 저장 (실패 - 정지)

## 🎓 교훈

1. **VM-level 작업은 매우 섬세함**
   - 작은 포인터 실수가 crash 유발
   - 메모리 관리가 핵심

2. **단순한 것부터 작동시키기**
   - Multi-shot은 나중에
   - 먼저 single-shot 완성

3. **테스트가 중요**
   - 각 단계마다 작은 테스트
   - 디버그 로그 필수

4. **이전 작동 상태 보존**
   - Git commit 자주
   - 작동하는 버전 백업

## 다음 세션을 위한 제안

1. **이전 작동 버전 복원**
   - test_jump_verify.lua가 작동했던 시점으로
   - 작은 변경부터 시작

2. **더 간단한 접근법 시도**
   - 먼저 single-shot만
   - Cloning은 나중에

3. **다른 continuation 구현 참고**
   - Scheme의 call/cc
   - OCaml의 continuation
   - 다른 Lua fork들

## 🌟 긍정적인 면

- 문제의 근본 원인은 **정확히 파악**했음
- RA offset 추출 로직은 **올바름**
- 이론적 해결책은 **타당함**
- 실행은 어렵지만 **가능함**

---

**결론**: 95%에서 99%로 가는 마지막 5%가 가장 어렵다. 하지만 포기하지 않고 계속 시도하면 해결할 수 있다!
