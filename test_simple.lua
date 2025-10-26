-- Simple test for continuation library
print("=== Testing Continuation Library ===\n")

-- Test 1: Load the library
local cont = require("continuation")
print("✓ Continuation library loaded")
print("  Available functions:", cont.callcc and "callcc" or "", cont.callec and "callec" or "")
print()

-- Test 2: Escape continuation (should work)
print("Test 2: Escape continuation (callec)")
local fn test_escape() {
  return cont.callec(fn(escape) {
    print("  Inside callec")
    escape("escaped value")
    print("  This should NOT print")
    return "normal return"
  })
}

local ok, result = pcall(test_escape)
if ok {
  print("  Result:", result)
  print("✓ Escape continuation works")
} else {
  print("✗ Error:", result)
}
print()

-- Test 3: Basic callcc (will likely fail but let's see)
print("Test 3: Basic callcc (may fail - incomplete implementation)")
local fn test_callcc() {
  return cont.callcc(fn(k) {
    print("  Inside callcc, got continuation")
    print("  Continuation type:", type(k))
    return "normal return from callcc"
  })
}

ok, result = pcall(test_callcc)
if ok {
  print("  Result:", result)
  print("✓ Basic callcc works")
} else {
  print("✗ Error:", result)
}
print()

print("=== Test Complete ===")
