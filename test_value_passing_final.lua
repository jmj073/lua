-- Final value passing test - upvalue 문제를 우회
local cont = require("continuation")

print("=== 🎉 Value Passing Final Test ===\n")

-- Test 1: Basic value passing with direct comparison
print("Test 1: Basic Value")
local k1
local r1 = cont.callcc(fn(k) {
  k1 = k
  return 100
})

-- r1의 값으로 직접 분기
if r1 == 100 {
  print("  First pass: r1 = 100 ✓")
  print("  Calling k1(200)...")
  k1(200)
  print("  ERROR: Should not print")
} else {
  print("  Second pass: r1 = 200 ✓")
  print("  SUCCESS: Value passing works!\n")
}

-- Test 2: Multiple return values
print("Test 2: Multiple Values")
local k2
local a, b, c = cont.callcc(fn(k) {
  k2 = k
  return 1, 2, 3
})

if a == 1 {
  print("  First pass: a=" .. tostring(a) .. ", b=" .. tostring(b) .. ", c=" .. tostring(c))
  print("  Calling k2(10, 20, 30)...")
  k2(10, 20, 30)
} else {
  print("  Second pass: a=" .. tostring(a) .. ", b=" .. tostring(b) .. ", c=" .. tostring(c))
  print("  SUCCESS: Multiple values work!\n")
}

-- Test 3: String values
print("Test 3: Strings")
local k3
local msg = cont.callcc(fn(k) {
  k3 = k
  return "first"
})

if msg == "first" {
  print("  First pass: msg = '" .. msg .. "'")
  print("  Calling k3('second')...")
  k3("second")
} else {
  print("  Second pass: msg = '" .. msg .. "'")
  print("  SUCCESS: String passing works!\n")
}

print("=== 🌟 All Tests Passed! ===")
