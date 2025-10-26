-- Test upvalue handling with multi-shot continuation
local cont = require("continuation")

print("=== Upvalue Multi-shot Test ===\n")

-- Test 1: Basic upvalue
print("Test 1: Local variable as upvalue")
local count = 0
local k1

local r1 = cont.callcc(fn(kont) {
  k1 = kont
  count = count + 1
  return count
})

print("  First call: r1 =", r1, ", count =", count)

if r1 == 1 {
  print("  Calling k1() again...")
  local r2 = k1()
  print("  ERROR: Should not reach here")
} else {
  print("  Second call: r1 =", r1, ", count =", count)
  
  if r1 == 1 and count == 1 {
    print("  ✅ SUCCESS: Upvalue preserved! count still 1")
  } else {
    print("  ❌ FAIL: count changed to", count)
  }
}

-- Test 2: Multiple upvalues
print("\nTest 2: Multiple upvalues")
local x = 10
local y = 20
local k2

local result = cont.callcc(fn(kont) {
  k2 = kont
  x = x + 1
  y = y + 2
  return x + y
})

print("  First call: result =", result, ", x =", x, ", y =", y)

if result == 33 {
  print("  Calling k2() again...")
  local result2 = k2()
  print("  ERROR: Should not reach here")
} else {
  print("  Second call: result =", result, ", x =", x, ", y =", y)
  
  if x == 11 and y == 22 {
    print("  ✅ SUCCESS: Multiple upvalues preserved!")
  } else {
    print("  ❌ FAIL: upvalues changed")
  }
}

print("\n=== Test Complete ===")
