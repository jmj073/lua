-- Multi-shot test with proper logic
local cont = require("continuation")

print("=== Multi-shot Continuation Test ===\n")

local k = nil
local call_count = 0

print("Setting up continuation...")
local result = cont.callcc(fn(kont) {
  k = kont
  return "first"
})

call_count = call_count + 1
print("Call", call_count, "- Result:", result)

-- Continue calling if we haven't called 3 times yet
if k and call_count < 3 {
  print("  -> Calling continuation again...")
  k("iteration " .. tostring(call_count + 1))
}

print("\nTest complete! Total calls:", call_count)
