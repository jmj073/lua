-- Multi-shot continuation test
local cont = require("continuation")

print("=== Multi-shot Continuation Test ===\n")

print("Test: Generator pattern with continuation")
local saved_k = nil
local count = 0

local generator = fn() {
  return cont.callcc(fn(k) {
    saved_k = k
    count = count + 1
    print("  Generator called, count =", count)
    return count
  })
}

print("First call:")
local val1 = generator()
print("  Received:", val1)

print("\nSecond call (via continuation):")
if saved_k {
  local val2 = saved_k(count + 1)
  print("  Received:", val2)
}

print("\nThird call (via continuation):")
if saved_k {
  local val3 = saved_k(count + 2)
  print("  Received:", val3)
}

print("\nTest complete!")
