-- Multi-shot continuation test
local cont = require("continuation")

print("=== Multi-shot Continuation Test ===\n")

-- Test 1: Basic multi-shot
print("Test 1: Call continuation multiple times")
local saved_k = nil
local counter = 0

local result = cont.callcc(fn(k) {
  saved_k = k
  counter = counter + 1
  print("  Inside callcc, counter =", counter)
  return "first return"
})

print("  First result:", result)
print("  Counter after first:", counter)

if saved_k and counter == 1 {
  print("\n  Calling k(\"second\")...")
  saved_k("second")
}

print("  Should not reach here after second call")

-- This should not execute
print("ERROR: Reached end of test 1")
