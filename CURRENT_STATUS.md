# Phase 2.5 현재 상태

## 시도한 것

1. ✅ Direct luaV_execute call → Segfault
2. ✅ Protected mode (luaD_rawrunprotected) → Segfault
3. ✅ Coroutine pattern analysis → 올바른 방향 확인

## 문제

Segfault가 **protected mode 내부**에서 발생.
즉, `do_invoke` 함수 내부의 어딘가에서 문제.

## 가능한 원인

1. **Stack 복원 문제**
   - `restorestack(L, cont->func_offset)`이 invalid pointer
   - Stack reallocation 후 offset이 invalid

2. **CallInfo 설정 문제**
   - `luaE_extendCI`가 올바른 상태 기대
   - 우리가 설정한 CallInfo가 VM 기대와 불일치

3. **PC가 invalid**
   - `cont->resume_pc`가 invalid instruction pointer
   - Function prototype이 GC되었을 수 있음

## 디버깅 필요

실제로 무엇이 문제인지 알아야 함.

### 접근 1: 단계별 검증
```c
// 각 단계마다 assert나 print
func = restorestack(...);
assert(func >= L->stack.p && func < L->stack_last.p);

// Stack restore
for (i = 0; i < cont->stack_size; i++) {
  assert(is_valid_tvalue(&cont->stack[i]));
}

// etc
```

### 접근 2: 최소 케이스
완전히 빈 continuation을 테스트
```lua
callcc(fn(k) { return 42 })  -- No k() call
```

### 접근 3: coroutine 코드 직접 복사
Resume 코드를 거의 그대로 가져오기

## 시간 고려

현재 약 3시간 소요됨.
세계를 구하는 것은 쉽지 않습니다.

## 권장 사항

**Option 1**: 더 디버깅 (1-2시간 더)
- Print statements
- GDB if available
- Step-by-step verification

**Option 2**: 세션 종료하고 정리
- 현재까지 Phase 2.0 완성 (caller context capture)
- 문서화 완료
- 나중에 fresh start

**Option 3**: 다른 접근
- Lua C API를 통한 구현
- 또는 Lua 레벨에서 emulation

---

**추천**: Option 1 - 조금만 더 디버깅
이미 거의 다 왔습니다! 🌍
