-- Simplest upvalue test
local cont = require("continuation")

print("=== Simple Upvalue Test ===\n")

local x = 100
print("Before callcc: x =", x)

local k = cont.callcc(fn(kont) {
  print("Inside callcc: x =", x)
  return kont
})

print("After callcc: x =", x)
x = x + 1
print("After x++: x =", x)

if x == 101 {
  print("Calling k() - should restore x to 100...")
  k()
}

print("After k(): x =", x)

if x == 100 {
  print("✅ SUCCESS: x restored to 100!")
} else {
  print("❌ FAIL: x =", x, "(expected 100)")
}
