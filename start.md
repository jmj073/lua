빠른 시작 (5분)
1. 상황 파악
bash
cd /home/test/sol

# 핵심 문서 3개 읽기
cat COROUTINE_ANALYSIS.md      # Coroutine 메커니즘
cat PHASE2_DESIGN_CORRECTED.md # 올바른 continuation 의미
cat CURRENT_STATUS.md           # 현재 문제
2. 문제 위치 확인
파일: 
lcont.c
함수: 
do_invoke
 (line 187)
증상: luaV_execute(L, ci) 호출 시 segfault (line 249)
해결 전략 (우선순위 순)
🥇 Strategy 1: Coroutine 코드 1:1 복사 (1-2시간)
이유: Coroutine resume은 이미 작동함. 우리가 놓친 게 있음.

참고 파일: 
ldo.c
 line 926-954

핵심 차이점 찾기:

c
// Coroutine resume (ldo.c:935-941)
if (isLua(ci)) {
  ci->u.l.savedpc--;  // ⭐ 이거 우리는 안 함!
  L->top.p = firstArg;
  luaV_execute(L, ci);
}
시도할 것:

ci->u.l.savedpc-- 추가 (PC를 한 칸 뒤로?)
L->top.p 설정 방식 변경
ci->callstatus 값 정확히 매칭
🥈 Strategy 2: PC Validation (30분)
PC가 invalid할 수 있음. 확인 코드 추가:

c
// do_invoke에서 (line 200 근처)
Proto *p = cont->proto;
if (cont->resume_pc < p->code || 
    cont->resume_pc >= p->code + p->sizecode) {
  fprintf(stderr, "ERROR: Invalid PC!\n");
  fprintf(stderr, "PC=%p, code range=[%p, %p)\n", 
          cont->resume_pc, p->code, p->code + p->sizecode);
  return;  // or error
}
🥉 Strategy 3: Stack Layout Check (30분)
스택 구조가 잘못되었을 수 있음:

c
// do_invoke에서 복원 후
fprintf(stderr, "Stack layout check:\n");
fprintf(stderr, "  func=%p (type=%d)\n", func, ttype(s2v(func)));
fprintf(stderr, "  func+1=%p (type=%d)\n", func+1, ttype(s2v(func+1)));
// func should be a function, func+1... should be locals
🎯 구체적 작업 순서
1단계: Debug Output 확인 (5분)
bash
./lua test_minimal_invoke.lua 2>&1 | tee debug.log
Debug 출력 마지막 줄이 "about to call luaV_execute" → 확실히 luaV_execute 내부 문제

2단계: Coroutine 코드와 비교 (30분)
bash
# ldo.c의 resume 함수 열어서 line by line 비교
vim ldo.c +926
체크리스트:

 savedpc adjustment?
 L->top.p 설정?
 ci->callstatus 정확?
 ci->u.l.trap 설정?
 ci->func.p 정확?
3단계: 가장 의심되는 수정 (1시간)
lcont.c line 228 수정:

c
// Before:
ci->u.l.savedpc = cont->resume_pc;

// Try:
ci->u.l.savedpc = cont->resume_pc - 1;  // Like coroutine!
이유: Coroutine은 savedpc-- 사용. PC adjustment가 필요할 수 있음.

4단계: 테스트 (5분)
bash
make && ./lua test_minimal_invoke.lua
성공하면 → 🎉 세계 구원! 실패하면 → Strategy 2, 3 시도

💡 핵심 인사이트
Continuation의 본질
NOT: 함수를 다시 실행
YES: callcc 호출 이후부터 재개
VM의 기대
CallInfo가 정확한 상태여야 함
PC는 다음 실행할 instruction
하지만 어떤 경우 PC-1이 필요 (fetch-execute cycle)
Why Coroutine Works
Coroutine은 yield에서 멈췄다가 같은 지점에서 재개. Continuation도 callcc에서 "멈춘 것처럼" 재개해야 함.

🔧 디버깅 팁
GDB가 있다면
bash
gdb ./lua
run test_minimal_invoke.lua
# segfault 시
bt
frame 0
info locals
Printf만 있다면
lcont.c에 이미 fprintf 있음. 더 추가하려면:

c
fprintf(stderr, "[DEBUG] your message\n");
📚 필수 읽기 자료
COROUTINE_ANALYSIS.md - 가장 중요!
ldo.c line 926-954 - resume 함수
PHASE2_DESIGN_CORRECTED.md - continuation 의미
🎲 예상 시나리오
Best Case (30%): savedpc-- 추가만으로 해결 Normal Case (50%): CallInfo 설정 2-3군데 수정 Worst Case (20%): VM 내부 더 깊이 이해 필요

✨ 최종 격려
당신은 이미 80% 완료했습니다!

Continuation 의미 완벽 이해 ✅
Caller context capture 완성 ✅
Protected mode 적용 ✅
Coroutine 패턴 분석 ✅
남은 것: VM과의 미세 조정 (PC adjustment 등)

이것은 Lua VM 핵심을 다루는 expert-level 작업입니다. 여기까지 온 것만으로도 대단합니다! 🌟

다음 세션에서 세계를 구합시다! 🌍💪

Good luck, and may the continuation be with you! 🚀