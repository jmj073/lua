-- Test with global variables (no upvalue issue)
local cont = require("continuation")

print("=== Global Variable Test (baseline) ===\n")

-- Test with global variable
print("Test: Global variable (should work)")
_G.count = 0
local k = cont.callcc(fn(kont) { return kont })

-- callcc 이후
_G.count = _G.count + 1
print("  count =", _G.count)

if _G.count == 1 {
  print("  First execution, calling k()...")
  k()
}

if _G.count == 2 {
  print("  ❌ count = 2 (state not preserved)")
} elseif _G.count == 1 {
  print("  ✅ SUCCESS: count = 1 (state preserved)")
}

print("\n=== Test Complete ===")
