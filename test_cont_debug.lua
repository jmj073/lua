-- Simple debug test
local cont = require("continuation")

print("=== Simple Continuation Test ===")

local result = cont.callcc(fn(k) {
  print("Inside callcc")
  k(42)
  print("Should not print")
  return 99
})

print("Result:", result)
print("Expected: 42")
