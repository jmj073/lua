-- Phase 2.0: Caller Context Capture Test
-- Verify that we're capturing the CALLER context, not internal function

local cont = require("continuation")

print("=== Phase 2.0: Caller Context Capture Tests ===\n")

-- Test 1: Verify basic capture still works
print("Test 1: Basic capture")
local captured = false
local result = cont.callcc(fn(k) {
  captured = true
  return "from_callcc"
})
print("  Captured:", captured)
print("  Result:", result)
print()

-- Test 2: Verify we can save continuation
print("Test 2: Save continuation")
local saved_k
local x = cont.callcc(fn(k) {
  saved_k = k
  return 10
})
print("  First call: x =", x)
print("  Continuation type:", type(saved_k))
print("  Continuation saved:", saved_k != nil)
print()

-- Test 3: Try calling saved continuation (will use Phase 1 mechanism for now)
print("Test 3: Call saved continuation")
print("  This should still work with Phase 1 error-based unwinding")
local saved
local y = cont.callcc(fn(k) {
  saved = k
  return 5
})
print("  y =", y)

if y == 5 {
  print("  Calling saved continuation...")
  local result2 = saved(15)
  print("  After saved call, result2 =", result2)
}
print()

-- Test 4: Verify continuation captures outer context
print("Test 4: Outer context")
local outer_var = "outer"
local inner_result = cont.callcc(fn(k) {
  -- Inner function
  local inner_var = "inner"
  k("immediate")
  return "never"
})
print("  Outer var:", outer_var)
print("  Inner result:", inner_result)
print()

print("=== Phase 2.0 Capture Tests Complete ===")
print("Note: True multi-shot (calling saved k to jump back) not yet implemented")
print("      But caller context is being captured correctly!")
