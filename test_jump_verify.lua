-- Verify that continuation actually jumps
local cont = require("continuation")

print("=== Jump Verification Test ===\n")

local k = nil

print("Before callcc")
local result = cont.callcc(fn(kont) {
  print("  Inside callcc function")
  k = kont
  return 111
})
print("After callcc, result =", result)

if result == 111 {
  print("\nFirst time through, calling k(222)...")
  k(222)
  print("ERROR: Should not reach here!")
} else {
  print("SUCCESS: Jumped back! result =", result)
}

print("\nEnd of test")
