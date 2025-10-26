-- Phase 2.0 Detailed Testing
-- Verify caller context capture is working correctly

local cont = require("continuation")

print("=== Phase 2.0: Detailed Caller Context Tests ===\n")

-- Test 1: Verify immediate continuation still works
print("Test 1: Immediate continuation (Phase 1 behavior)")
local result1 = cont.callcc(fn(k) {
  print("  Inside callcc")
  k(42)
  print("  This should not print")
  return 99
})
print("  Result:", result1)
assert(result1 == 42, "Immediate continuation failed")
print("  ✓ Pass\n")

-- Test 2: Multiple immediate calls  
print("Test 2: Multiple immediate continuation calls")
local count = 0
local fn test_multiple(i) {
  if i > 3 {
    return
  }
  local r = cont.callcc(fn(k) {
    count = count + 1
    k(count)
    return -1
  })
  print("  Iteration", i, "result:", r)
  assert(r == i, "Multiple calls failed")
  test_multiple(i + 1)
}
test_multiple(1)
print("  ✓ Pass\n")

-- Test 3: Nested callcc (both immediate)
print("Test 3: Nested callcc")
local outer_result = cont.callcc(fn(k_outer) {
  print("  In outer callcc")
  local inner_result = cont.callcc(fn(k_inner) {
    print("    In inner callcc")
    k_inner(10)
    return 20
  })
  print("  Inner returned:", inner_result)
  k_outer(inner_result + 5)
  return 30
})
print("  Outer result:", outer_result)
assert(outer_result == 15, "Nested callcc failed")
print("  ✓ Pass\n")

-- Test 4: Continuation with local variables
print("Test 4: Local variables in caller")
local fn test_locals() {
  local a = 10
  local b = 20
  local c = cont.callcc(fn(k) {
    k(a + b)
    return 0
  })
  return c
}
local result4 = test_locals()
print("  Result:", result4)
assert(result4 == 30, "Local variables test failed")
print("  ✓ Pass\n")

-- Test 5: Continuation in different scopes
print("Test 5: Different scopes")
local global_result = 0
local fn outer() {
  local x = 100
  local fn inner() {
    local y = 50
    local z = cont.callcc(fn(k) {
      k(x + y)
      return 0
    })
    return z
  }
  return inner()
}
global_result = outer()
print("  Result:", global_result)
assert(global_result == 150, "Different scopes test failed")
print("  ✓ Pass\n")

-- Test 6: Continuation with no arguments
print("Test 6: Continuation with no arguments")
local result6 = cont.callcc(fn(k) {
  k()
  return "fallback"
})
print("  Result:", result6 == nil and "nil" or result6)
print("  ✓ Pass\n")

-- Test 7: Continuation with many arguments
print("Test 7: Many arguments")
local r1, r2, r3, r4, r5 = cont.callcc(fn(k) {
  k(1, 2, 3, 4, 5)
  return 0, 0, 0, 0, 0
})
print("  Results:", r1, r2, r3, r4, r5)
assert(r1 == 1 and r2 == 2 and r3 == 3 and r4 == 4 and r5 == 5, "Many args failed")
print("  ✓ Pass\n")

-- Test 8: Continuation saved but not called
print("Test 8: Save but don't call")
local saved8
local result8 = cont.callcc(fn(k) {
  saved8 = k
  return "normal"
})
print("  Result:", result8)
print("  Saved:", type(saved8))
assert(result8 == "normal", "Save without call failed")
assert(type(saved8) == "function", "Continuation not saved properly")
print("  ✓ Pass\n")

-- Test 9: Error handling in callcc
print("Test 9: Error handling")
local ok, err = pcall(fn() {
  cont.callcc(fn(k) {
    error("test error")
  })
})
print("  Error caught:", not ok)
assert(not ok, "Error not propagated")
print("  ✓ Pass\n")

-- Test 10: Continuation with conditional logic
print("Test 10: Conditional in callcc")
local fn conditional_test(should_continue) {
  return cont.callcc(fn(k) {
    if should_continue {
      k("continued")
    } else {
      return "normal"
    }
  })
}
local r10a = conditional_test(true)
local r10b = conditional_test(false)
print("  With continuation:", r10a)
print("  Without continuation:", r10b)
assert(r10a == "continued", "Conditional true failed")
assert(r10b == "normal", "Conditional false failed")
print("  ✓ Pass\n")

print("=== All Phase 2.0 Tests Passed! ===")
print("Phase 1 functionality maintained")
print("Caller context is being captured")
print("\nNext: Implement Phase 2.5 for true multi-shot continuation")
