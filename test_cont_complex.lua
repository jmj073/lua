-- Test complex continuation scenarios
local cont = require("continuation")

print("Test 1: Nested local variables")
local a = 1
local b = 2
local c = 3
local k, result

k, result = cont.callcc(fn(cc) {
    return cc, a + b + c
})

print("Result:", result)
assert(result == 6, "Expected 6")

print("Test 2: Continuation with closure")
local outer = 10
local inner = 20
local k3, result3

k3, result3 = cont.callcc(fn(cc) {
    return cc, outer + inner
})

print("Closure result:", result3)
assert(result3 == 30, "Expected 30")

print("Test 3: Multiple continuations")
local k1, k2
local v1, v2

k1, v1 = cont.callcc(fn(cc) { return cc, 100 })
k2, v2 = cont.callcc(fn(cc) { return cc, 200 })

print("Multiple:", v1, v2)
assert(v1 == 100 and v2 == 200, "Multiple continuations failed")

print("All tests passed!")
