-- Phase 2.5: True Multi-Shot Continuation Test
-- The moment of truth!

local cont = require("continuation")

print("=== Phase 2.5: Multi-Shot Continuation Test ===\n")

-- Test 1: Save and call continuation later
print("Test 1: Save continuation and call later")
local saved
local x = cont.callcc(fn(k) {
  print("  Inside callcc")
  saved = k
  return "first"
})
print("  First call: x =", x)

if x == "first" {
  print("  Calling saved continuation with 'second'...")
  saved("second")
  print("  ERROR: Should not reach here!")
} else {
  print("  After continuation call: x =", x)
}
print()

-- Test 2: Simple multi-shot test
print("Test 2: Multi-shot - call twice")
local saved2
local count = 0
count = count + 1
local y = cont.callcc(fn(k) {
  saved2 = k
  return count
})
print("  y =", y)

if count == 1 {
  saved2(count)
}
if count == 2 {
  print("  Multi-shot worked!")
}
print()

print("=== Test Complete ===")
