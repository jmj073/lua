# Phase 1 구현 문제 분석

## 현재 상황
- Continuation이 생성되고 함수로 전달됨 ✅
- 함수가 정상적으로 실행됨 ✅
- `k(...)` 호출 시 cont_call이 실행됨 ✅
- **문제**: cont_call이 값을 반환하지만, callcc 내부 함수를 종료시키지 못함 ❌

## 왜 작동하지 않는가?

```lua
callcc(fn(k) {
  k(42)        -- cont_call(42) 실행 → 42 반환
  return 100   -- 하지만 함수는 계속 실행되어 100 반환
})
```

**문제의 핵심**: `k(42)`는 단순히 42를 반환하지만, 함수 실행을 멈추지 않음!

## 해결 방법 옵션

### Option A: Error-based unwinding (Escape continuation 방식)
callec이 사용하는 방법과 유사:
1. continuation 호출 시 특별한 error throw
2. callcc가 pcall로 함수 실행
3. error catch하여 값 추출
4. 정상 반환처럼 보이게 처리

**장점**: 
- 스택 unwinding 자동 처리
- escape continuation과 비슷한 메커니즘
- 비교적 간단

**단점**:
- Error mechanism 사용이 해킹처럼 느껴짐
- 진짜 에러와 구별 필요

### Option B: Registry-based signaling
1. continuation 호출 시 레지스트리에 값 저장
2. cont_call에서 특별한 sentinel 값 반환
3. callcc 함수 내부에서 sentinel 체크
4. sentinel이면 레지스트리에서 값 가져와 반환

**장점**:
- 더 깔끔한 구현
- Error가 아닌 정상 제어 흐름

**단점**:
- 복잡도 증가
- 레지스트리 관리 필요

### Option C: Immediate Phase 2 (CallInfo + longjmp)
바로 완전한 구현으로:
1. CallInfo 저장
2. longjmp로 callcc 호출 지점으로 직접 점프
3. 저장된 값 반환

**장점**:
- 진짜 continuation
- Multi-shot도 가능

**단점**:
- 복잡도 높음
- 디버깅 어려움
- 한 번에 너무 많은 것 구현

## 추천: Option A (Error-based)
Escape continuation이 이미 비슷한 방식 사용 중이므로 일관성 있음.
callec 코드를 참고하여 빠르게 구현 가능.

## 다음 단계
1. callcc를 pcall로 함수 실행하도록 수정
2. cont_call에서 특별한 error throw
3. callcc에서 error catch하여 값 추출
4. 정상 반환으로 변환
