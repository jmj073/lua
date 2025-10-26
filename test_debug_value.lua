-- Debug value passing
local cont = require("continuation")

print("Starting test...")

local k
print("1. k is nil:", k == nil)

local result = cont.callcc(fn(kont) {
  print("2. Inside callcc, setting k")
  k = kont
  print("3. k is set:", not (k == nil))
  return 100
})

print("4. After callcc, result =", result)
print("5. k is nil:", k == nil)
if k {
  print("6. k type:", type(k))
} else {
  print("6. k is nil!")
}

if result == 100 {
  print("7. First pass, calling k(200)")
  print("8. About to call k...")
  k(200)
  print("9. ERROR - should not print")
} else {
  print("10. Second pass, result =", result)
}

print("11. End")
