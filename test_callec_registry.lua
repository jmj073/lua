local cont = require('continuation')

-- 기본 테스트
print('=== Test 1: Basic escape ===')
local r1, r2 = cont.callec(fn(esc) {
  esc(10, 20)
  return 30, 40  -- 실행 안됨
})
print('Result:', r1, r2)
assert(r1 == 10 and r2 == 20, 'Basic escape failed')

-- 재진입 테스트 (중첩 callec)
print('\n=== Test 2: Nested callec (reentrant) ===')
local outer = cont.callec(fn(esc1) {
  local inner = cont.callec(fn(esc2) {
    esc2(100)  -- 내부 escape
    return 999
  })
  print('Inner result:', inner)
  esc1(200)  -- 외부 escape
  return 888
})
print('Outer result:', outer)
assert(outer == 200, 'Nested escape failed')

-- 여러 번 호출 테스트
print('\n=== Test 3: Multiple invocations ===')
local test_multiple
test_multiple = fn(i, max) {
  if i > max {
    return
  }
  local result = cont.callec(fn(esc) {
    esc(i * 10)
  })
  print('Invocation', i, ':', result)
  assert(result == i * 10)
  test_multiple(i + 1, max)
}
test_multiple(1, 5)

-- 다중 값 테스트
print('\n=== Test 4: Multiple values ===')
local a, b, c, d = cont.callec(fn(esc) {
  esc(1, 2, 3, 4)
})
print('Multiple values:', a, b, c, d)
assert(a == 1 and b == 2 and c == 3 and d == 4)

-- 정상 리턴 테스트
print('\n=== Test 5: Normal return (no escape) ===')
local x, y = cont.callec(fn(esc) {
  return 100, 200
})
print('Normal return:', x, y)
assert(x == 100 and y == 200)

print('\n✅ All tests passed!')
