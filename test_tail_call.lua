local cont = require("continuation")

print("=== Test: Non-tail position (will crash) ===")
local bad_cc = fn() {
    local k = cont.callcc(fn(x) { return x })
    print("After callcc, about to return")
    return k  -- NOT in tail position!
}

local good_cc = fn() {
    return cont.callcc(fn(x) { return x })  -- Tail position!
}

-- This works
print("Testing good_cc (tail call)...")
local k1 = good_cc()
print("Got k1:", k1)

-- This will crash when invoked
print("\nTesting bad_cc (non-tail)...")
local k2 = bad_cc()
print("Got k2:", k2)

print("\nInvoking k2 with 123...")
k2(123)  -- This should crash
print("After k2 invoke (shouldn't reach here)")
