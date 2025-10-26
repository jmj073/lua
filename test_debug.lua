-- Debug test to see what's happening
local cont = require("continuation")

print("Test: Immediate continuation call")
local result = cont.callcc(fn(k) {
  print("  Before k() call")
  print("  Type of k:", type(k))
  local status, err = pcall(fn() {
    k("test value")
  })
  print("  After k() call")
  print("  Status:", status)
  print("  Error:", err)
  return "fallback"
})
print("Result:", result)
