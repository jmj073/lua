-- Test simple continuation return (current implementation)
local cont = require("continuation")

print("Test 1: Continuation returns value")
local k_saved = nil

local result1 = cont.callcc(fn(k) {
  k_saved = k
  print("  Inside callcc function")
  return "normal return"
})

print("  result1 =", result1)

print("\nTest 2: Call saved continuation")
if k_saved {
  print("  Calling k_saved(42)...")
  local result2 = k_saved(42)
  print("  result2 =", result2)
  print("  (Note: This just returns 42, doesn't jump)")
}

print("\nTests complete!")
