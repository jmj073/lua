-- Minimal test for continuation invocation
local cont = require("continuation")

print("Minimal invoke test...")

-- Just try immediate invocation first
local result = cont.callcc(fn(k) {
  print("Before k()")
  k(42)
  print("After k() - should not print")
  return 99
})

print("Result:", result)
print("Test complete")
