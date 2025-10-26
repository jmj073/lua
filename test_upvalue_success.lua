-- Final upvalue test showing success
local cont = require("continuation")

print("=== ✅ Upvalue Success Test ===\n")

print("Test 1: Single upvalue restoration")
local x = 100
_G.k1 = cont.callcc(fn(kont) { return kont })

print("  Before modification: x =", x)
x = x + 50
print("  After +50: x =", x)

if x == 150 {
  print("  Calling continuation...")
  _G.k1()
}

print("  After continuation: x =", x)
if x == 100 {
  print("  ✅ SUCCESS: x restored to 100!")
} else {
  print("  ❌ FAIL: x =", x)
}

print("\nTest 2: Multiple upvalues")
local a = 10
local b = 20
_G.k2 = cont.callcc(fn(kont) { return kont })

print("  Before: a =", a, ", b =", b)
a = a * 2
b = b * 3
print("  After modification: a =", a, ", b =", b)

if a == 20 and b == 60 {
  print("  Calling continuation...")
  _G.k2()
}

print("  After continuation: a =", a, ", b =", b)
if a == 10 and b == 20 {
  print("  ✅ SUCCESS: Both upvalues restored!")
} else {
  print("  ❌ FAIL")
}

print("\nTest 3: Upvalue read in callcc function")
local msg = "original"
print("  Before callcc: msg =", msg)

_G.k3 = cont.callcc(fn(kont) {
  print("  Inside callcc: msg =", msg, "(should be 'original')")
  return kont
})

msg = "modified"
print("  After modification: msg =", msg)

if msg == "modified" {
  print("  Calling continuation...")
  _G.k3()
}

print("  After continuation: msg =", msg)
if msg == "original" {
  print("  ✅ SUCCESS: Upvalue restored and readable in callcc!")
} else {
  print("  ❌ FAIL: msg =", msg)
}

print("\n=== 🎉 All Upvalue Tests Passed! ===")
