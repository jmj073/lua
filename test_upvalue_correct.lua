-- Correct upvalue test for multi-shot continuation
local cont = require("continuation")

print("=== Correct Upvalue Multi-shot Test ===\n")

-- Test 1: Basic upvalue after callcc
print("Test 1: Upvalue modification after callcc")
local count = 0
local k1 = cont.callcc(fn(kont) { return kont })

-- callcc 이후의 코드 - 이 부분이 반복 실행됨
count = count + 1
print("  count =", count)

if count == 1 {
  print("  First execution, calling k1()...")
  k1()  -- callcc 직후로 점프 → count = count + 1 다시 실행
}

if count == 2 {
  print("  ❌ FAIL: count = 2 (upvalue not preserved)")
  print("  Multi-shot failed - original state was modified")
} elseif count == 1 {
  print("  ✅ SUCCESS: count still 1 (upvalue preserved!)")
  print("  Multi-shot works - original state preserved")
}

-- Test 2: Multiple upvalues
print("\nTest 2: Multiple upvalues")
local x = 0
local y = 0
local k2 = cont.callcc(fn(kont) { return kont })

-- callcc 이후 - upvalue 수정
x = x + 10
y = y + 20
print("  x =", x, ", y =", y)

if x == 10 and y == 20 {
  print("  First execution, calling k2()...")
  k2()
}

if x == 20 and y == 40 {
  print("  ❌ FAIL: x =", x, ", y =", y, "(not preserved)")
} elseif x == 10 and y == 20 {
  print("  ✅ SUCCESS: x =", x, ", y =", y, "(preserved!)")
}

-- Test 3: Upvalue in loop (classic test)
print("\nTest 3: Loop with upvalue")
local iteration = 0
local k3 = cont.callcc(fn(kont) { return kont })

iteration = iteration + 1
print("  Iteration:", iteration)

if iteration < 3 {
  k3()  -- 다시 점프
}

if iteration == 3 {
  print("  ❌ FAIL: iteration = 3 (accumulated)")
  print("  Each k3() call modified the same upvalue")
} elseif iteration == 1 {
  print("  ✅ SUCCESS: iteration = 1 (each call sees original state)")
  print("  True multi-shot - each invocation is independent")
}

print("\n=== Test Complete ===")
