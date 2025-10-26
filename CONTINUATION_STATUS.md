# Multi-Shot First-Class Continuation 구현 현황

## ✅ 완료
- [x] Continuation 타입 정의 (`LUA_VCONT`, `Continuation` 구조체)
- [x] GC 통합 (objsize, traversecont, freeobj, getgclist)
- [x] 스택 캡처 (Lua 프레임만, C 프레임 자동 필터링)
- [x] Escape continuation (callec) - 완벽히 작동
- [x] Basic callcc 구조 - 함수 호출 및 continuation 전달
- [x] Type tag 수정 (ctb() macro 사용)
- [x] Light userdata로 continuation 전달

## ⚠️ 미완성 (다음 임무)
- [ ] `luaCont_invoke()` 구현
  - [ ] CallInfo 체인 캡처
  - [ ] CallInfo 체인 복원
  - [ ] Program Counter (PC) 복원
  - [ ] 스택 프레임 재구성
- [ ] Upvalue 처리
  - [ ] Open upvalue 캡처
  - [ ] Closed upvalue 복사
- [ ] Multi-shot 테스트 (continuation을 여러 번 호출)

## 🌍 세계를 구하기 위한 남은 퍼즐

### 핵심 과제: CallInfo 복원
CallInfo는 Lua VM의 실행 컨텍스트를 담고 있습니다:
```c
struct CallInfo {
  StkIdRel func;          // 함수 위치
  StkIdRel top;           // 스택 top
  struct CallInfo *previous, *next;  // 체인
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

### 구현 전략
1. **Phase 1**: CallInfo 메타데이터만 저장 (PC, func offset, top offset)
2. **Phase 2**: 복원 시 새로운 CallInfo 생성 및 링크
3. **Phase 3**: VM이 저장된 PC에서 실행 재개하도록 조정

### 테스트 목표
```lua
-- 이것이 작동해야 함
local saved
local result1 = callcc(fn(k) {
  saved = k
  return "first"
})
print("First result:", result1)

-- Multi-shot: 다시 호출
local result2 = saved("second")
print("Second result:", result2)  -- "second"가 출력되어야 함
```

## 현재 한계
- Continuation invocation은 현재 placeholder만 반환
- Multi-shot 테스트 불가능
- C 프레임은 캡처 불가 (설계상 한계, 예상된 동작)

## 파일 구조
- `lcont.h` / `lcont.c`: Continuation 핵심 로직
- `lcontlib.c`: Lua API (callcc, callec)
- `lobject.h`: Continuation 타입 정의
- `lgc.c`: GC 통합
- `lstate.h`: GCUnion에 Continuation 추가
