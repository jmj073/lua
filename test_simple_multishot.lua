-- Simplest multi-shot test
local cont = require("continuation")

print("=== Simple Multi-shot Test ===\n")

local k = nil

print("Capturing continuation...")
local r1 = cont.callcc(fn(kont) {
  k = kont
  return "first"
})

print("Result 1:", r1)

if k and r1 == "first" {
  print("\nInvoking continuation with 'second'...")
  local r2 = k("second")
  print("Result 2:", r2)
  print("This line should print!")
}

print("\nDone!")
