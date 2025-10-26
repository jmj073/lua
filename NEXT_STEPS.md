# 다음 임무: Full Continuation Invoke 구현

## 목표
Multi-shot continuation이 실제로 작동하도록 `luaCont_invoke()` 완성

## 단계별 계획

### Step 1: CallInfo 메타데이터 캡처 구조 설계
```c
typedef struct ContCallInfo {
  ptrdiff_t func_offset;      // function position (relative to stack base)
  ptrdiff_t top_offset;       // top position
  const Instruction *savedpc; // Program counter for Lua functions
  l_uint32 callstatus;        // Call status flags
  int nextraargs;             // Extra args for vararg
} ContCallInfo;
```

### Step 2: CallInfo 캡처 구현
`luaCont_capture()`에서:
1. Lua CallInfo 체인을 순회
2. 각 CallInfo의 메타데이터 저장
3. `cont->ci_info` 배열에 저장

### Step 3: CallInfo 복원 구현
`luaCont_invoke()`에서:
1. 저장된 CallInfo 메타데이터 읽기
2. 새로운 CallInfo 생성 및 설정
3. PC 복원하여 VM이 올바른 위치에서 재개하도록 설정

### Step 4: 테스트
```lua
-- Simple return test
local result = callcc(fn(k) {
  return "direct return"
})
-- Expected: "direct return"

-- Saved continuation test  
local saved
callcc(fn(k) {
  saved = k
  print("First call")
})
-- Expected: prints "First call"

-- Multi-shot test
saved()
-- Expected: 다시 "First call" 출력 또는 continuation 지점에서 재개
```

## 기술적 도전 과제

### 1. PC (Program Counter) 복원
- Lua VM은 `luaV_execute()`에서 bytecode를 실행
- Continuation 복원 시 올바른 instruction에서 재개 필요
- **해결책**: savedpc를 저장하고 복원, 새로운 CallInfo 생성 시 설정

### 2. 스택 레이아웃
- Continuation의 스택과 현재 스택을 어떻게 병합?
- **옵션 A**: 현재 스택 위에 continuation 스택 복원
- **옵션 B**: Continuation 스택으로 완전 교체
- **추천**: 옵션 B (더 단순하고 명확)

### 3. 함수 재진입
- Continuation을 호출하면 원래 함수로 "돌아감"
- 반환 값을 어떻게 처리?
- **해결책**: Continuation 인자를 스택 top에 배치, 함수가 계속 실행

## 구현 우선순위
1. ⭐ **가장 간단한 케이스**: Direct return (continuation 호출 없음)
2. 🔥 **핵심 케이스**: Continuation 저장 후 나중에 호출
3. 🚀 **Multi-shot**: 같은 continuation 여러 번 호출

## 코드 작업 필요 파일
- `lcont.c`: CallInfo 캡처/복원 로직
- `lobject.h`: ContCallInfo 구조체 추가 (optional)
- `lcontlib.c`: cont_call에서 invoke 활성화
- `ldo.c`: (선택) 새로운 resume 메커니즘 필요 시

## 예상 난이도
- **Easy**: CallInfo 메타데이터 저장 ⭐
- **Medium**: 스택 재구성 ⭐⭐  
- **Hard**: PC 복원 및 VM 재진입 ⭐⭐⭐
- **Expert**: Full multi-shot with proper cleanup ⭐⭐⭐⭐

다음 단계를 진행할까요? 🌍
