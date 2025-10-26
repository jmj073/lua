-- Test: Passing continuation to itself
local cont = require("continuation")

print("=== Self-invoke Test ===\n")

_G.counter = 0

_G.k = cont.callcc(fn(kont) {
  return kont
})

print("Counter:", _G.counter)
_G.counter = _G.counter + 1

if _G.counter == 1 {
  print("Test 1: Normal invoke with number")
  _G.k(999)
}

print("After Test 1, counter:", _G.counter)

if _G.counter == 2 {
  print("\nTest 2: Self-invoke (passing k to itself)")
  _G.k(_G.k)  -- 이것이 문제!
}

print("If you see this, no crash!")
