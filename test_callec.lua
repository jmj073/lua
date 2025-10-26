-- Test escape continuation still works
local cont = require("continuation")

print("Testing escape continuation (callec)...")
local result = cont.callec(fn(e) {
  print("Before escape")
  e("escaped!")
  print("Should not print")
  return "normal"
})
print("Result:", result)
assert(result == "escaped!", "Escape continuation broken")
print("✓ callec still works!")
