-- Test: Continuation stored in multiple places
local cont = require("continuation")

print("=== Multiple References Test ===\n")

-- Test 1: Continuation stored in two variables
print("Test 1: k and asdf both reference same continuation")

_G.asdf = nil
_G.k = cont.callcc(fn(kont) {
  _G.asdf = kont
  return kont
})

print("  After callcc:")
print("    k type:", type(_G.k))
print("    asdf type:", type(_G.asdf))
print("    k == asdf:", _G.k == _G.asdf)

if type(_G.k) == "function" {
  print("  First invocation, calling k(999)...")
  _G.k(999)
}

print("  After k(999):")
print("    k type:", type(_G.k))
print("    k value:", _G.k)

print("\n=== Test Complete ===")
