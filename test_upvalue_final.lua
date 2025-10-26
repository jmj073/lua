-- Final upvalue test
local cont = require("continuation")

print("=== Final Upvalue Test ===\n")

-- Test 1: Simple upvalue restoration
print("Test 1: Single upvalue")
local x = 100
local saved_k = cont.callcc(fn(k) { return k })

print("  x =", x)
x = x + 50
print("  After x += 50: x =", x)

if x == 150 {
  print("  Calling continuation...")
  saved_k()
}

print("  After continuation: x =", x)
if x == 100 {
  print("  ✅ SUCCESS: x restored to 100!")
} else {
  print("  ❌ FAIL: x =", x)
}

-- Test 2: Loop with upvalue (the problematic case)
print("\nTest 2: Loop with upvalue")
local count = 0
local k2, n = cont.callcc(fn(k) { return k, 3 })

print("  count =", count, ", n =", n)
count = count + 1

if n > 0 {
  print("  Calling k2(k2, n-1)...")
  k2(k2, n - 1)
} else {
  print("  Done! Final count =", count)
  if count == 1 {
    print("  ✅ SUCCESS: count preserved at each iteration!")
  } else {
    print("  ❌ FAIL: count =", count)
  }
}

print("\n=== Tests Complete ===")
