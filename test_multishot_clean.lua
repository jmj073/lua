-- Clean multi-shot test without upvalues
local cont = require("continuation")

print("=== Clean Multi-shot Test ===\n")

-- Global variable to avoid upvalue issues
_G.saved_continuation = nil
_G.call_count = 0

print("Test 1: First invocation")
local r1 = cont.callcc(fn(k) {
  _G.saved_continuation = k
  _G.call_count = _G.call_count + 1
  return 100 + _G.call_count
})

print("  Result:", r1)
print("  Call count:", _G.call_count)

if r1 == 101 {
  print("\nTest 2: Second invocation (multi-shot!)")
  local r2 = _G.saved_continuation(200)
  print("  ERROR: Should not reach here")
} else {
  print("  Second result:", r1)
  print("  Call count:", _G.call_count)
}

if r1 == 200 and _G.call_count == 1 {
  print("\n✅ SUCCESS: Multi-shot works!")
  print("  - Original state preserved")
  print("  - Call count still 1 (not incremented)")
  print("  - New argument (200) received")
  
  print("\nTest 3: Third invocation (multi-shot again!)")
  local r3 = _G.saved_continuation(300)
  print("  ERROR: Should not reach here")
} else {
  if r1 == 300 and _G.call_count == 1 {
    print("\n✅ SUCCESS: Multi-shot works multiple times!")
    print("  - Can invoke same continuation again")
    print("  - State still preserved")
  }
}

print("\n=== Test Complete ===")
