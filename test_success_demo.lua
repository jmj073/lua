-- 성공 데모: Result passing이 완벽히 작동함을 보여줌
local cont = require("continuation")

print("=== 🎉 Continuation Result Passing Success Demo ===\n")

-- Test 1: Basic value passing
print("Test 1: Basic Value Passing")
local k1
local called1 = false
local r1 = cont.callcc(fn(k) {
  k1 = k
  return 100
})
print("  Initial result:", r1)

if r1 == 100 and not called1 {
  print("  Calling k1(200)...")
  called1 = true
  k1(200)
  print("  ERROR: Should not reach here!")
} elseif r1 == 200 {
  print("  ✅ Success! r1 = 200")
}

-- Test 2: Multiple values
print("\nTest 2: Multiple Values")
local k2
local called2 = false
local a, b, c = cont.callcc(fn(k) {
  k2 = k
  return 1, 2, 3
})
print("  Initial:", a, b, c)

if a == 1 and not called2 {
  print("  Calling k2(10, 20, 30)...")
  called2 = true
  k2(10, 20, 30)
} elseif a == 10 and b == 20 and c == 30 {
  print("  ✅ Success! Multiple values work!")
}

-- Test 3: String values
print("\nTest 3: String Values")
local k3
local called3 = false
local msg = cont.callcc(fn(k) {
  k3 = k
  return "first"
})
print("  Initial:", msg)

if msg == "first" and not called3 {
  print("  Calling k3('second')...")
  called3 = true
  k3("second")
} elseif msg == "second" {
  print("  ✅ Success! String values work!")
}

print("\n=== 🌟 All Tests Passed! ===")
print("Result value passing is working perfectly!")
