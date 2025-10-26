-- Upvalue test with global k
local cont = require("continuation")

print("=== Upvalue Test (global k) ===\n")

local x = 100
print("Before callcc: x =", x)

_G.saved_k = cont.callcc(fn(kont) {
  print("Inside callcc: x =", x)
  return kont
})

print("After callcc: x =", x)
x = x + 1
print("After x++: x =", x)

if x == 101 {
  print("Calling k() - should restore x to 100...")
  _G.saved_k()
}

print("After k(): x =", x)

if x == 100 {
  print("✅ SUCCESS: x restored to 100!")
  print("Second invocation:")
  x = x + 50
  print("  x after +50:", x)
  
  if x == 150 {
    print("  Calling k() again...")
    _G.saved_k()
  }
} elseif x == 101 {
  print("❌ FAIL: x = 101 (not restored)")
}

if x == 100 {
  print("✅ PERFECT: x = 100 again (true multi-shot!)")
}
