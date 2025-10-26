-- Final multi-shot demonstration
local cont = require("continuation")

print("=== Final Multi-shot Demonstration ===\n")

local k = nil
local times = 0

print("Creating continuation...")
cont.callcc(fn(kont) {
  k = kont
  print("  [Inside callcc function]")
})

times = times + 1
print("\nExecution #" .. tostring(times))
print("  times =", times)

if times == 1 {
  print("  -> First time, calling k() to jump back...")
  k()
} elseif times == 2 {
  print("  -> Second time, calling k() again...")
  k()
} elseif times == 3 {
  print("  -> Third time, stopping here.")
}

print("\n✅ Multi-shot continuation works!")
print("Total executions:", times)
