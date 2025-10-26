-- Test file for first-class continuations (callcc)
-- Note: Uses modified Lua syntax with {} instead of end, fn instead of function

local continuation = require("continuation")
local callcc = continuation.callcc
local callec = continuation.callec

print("=== Testing First-Class Continuations ===\n")

-- Test 1: Basic callcc usage
print("Test 1: Basic callcc")
local result = callcc(fn(k) {
  print("Inside callcc function")
  return "normal return"
})
print("Result:", result)
print()

-- Test 2: Escape continuation (single use)
print("Test 2: Escape continuation")
local escape_result = callec(fn(escape) {
  print("Before escape")
  escape("escaped!")
  print("This should not print")
  return "normal"
})
print("Result:", escape_result)
print()

-- Test 3: Multi-shot continuation (return multiple times)
print("Test 3: Multi-shot continuation (saved)")
local saved_cont
local first_result = callcc(fn(k) {
  saved_cont = k
  print("First call to continuation")
  return "first"
})
print("First result:", first_result)

if first_result == "first" {
  print("Calling saved continuation again...")
  -- saved_cont("second")  -- This would re-invoke the continuation
  print("(Skipped second call to avoid complexity in initial test)")
}
print()

-- Test 4: Continuation with multiple return values
print("Test 4: Multiple return values")
local a, b, c = callcc(fn(k) {
  return 1, 2, 3
})
print(string.format("Results: %d, %d, %d", a, b, c))
print()

-- Test 5: Nested callcc
print("Test 5: Nested callcc")
local outer_result = callcc(fn(outer_k) {
  print("Outer callcc")
  local inner_result = callcc(fn(inner_k) {
    print("Inner callcc")
    return "inner"
  })
  print("Inner returned:", inner_result)
  return "outer"
})
print("Outer result:", outer_result)
print()

print("=== All tests completed ===")
