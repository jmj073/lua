-- Phase 1 Continuation Test
-- Tests simple continuation invocation (pass-through semantics)

local cont = require("continuation")

print("=== Phase 1: Simple Continuation Tests ===\n")

-- Test 1: Direct return (no continuation invocation)
print("Test 1: Direct return from callcc")
local result = cont.callcc(fn(k) {
  return "direct return"
})
print("Result:", result)
print()

-- Test 2: Call continuation immediately
print("Test 2: Immediate continuation invocation")
local result2 = cont.callcc(fn(k) {
  k("from continuation")
  return "never reached"
})
print("Result:", result2)
print()

-- Test 3: Multiple values through continuation
print("Test 3: Multiple return values")
local a, b, c = cont.callcc(fn(k) {
  k(1, 2, 3)
  return 99, 99, 99
})
print(string.format("Results: %s, %s, %s", tostring(a), tostring(b), tostring(c)))
print()

-- Test 4: Conditional continuation call
print("Test 4: Conditional continuation")
local fn test_cond(use_cont) {
  return cont.callcc(fn(k) {
    if use_cont {
      k("used continuation")
    } else {
      return "normal return"
    }
  })
}

print("With continuation:", test_cond(true))
print("Without continuation:", test_cond(false))
print()

-- Test 5: Save continuation (won't work properly in Phase 1, but should not crash)
print("Test 5: Save continuation for later (Phase 1 limitation)")
local saved_cont
cont.callcc(fn(k) {
  saved_cont = k
  print("  Saved continuation")
})
print("  Continuation saved:", type(saved_cont))
-- Note: Calling saved_cont() in Phase 1 won't jump back, just returns values
print()

print("=== Phase 1 Tests Complete ===")
print("Note: Multi-shot (calling saved continuation) not yet fully implemented")
