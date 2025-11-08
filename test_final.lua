print("=== Final AST Implementation Test ===")

print("--- 1. Literals ---")
print(nil)
print(true)
print(false)
print(42)
print("hello")

print("--- 2. Arithmetic & Operators ---")
print(1 + 2 * 3)
print(2 ^ 3)
print(10 // 3)
print(-5)
print(#"test")

print("--- 3. Comparison & Logical ---")
print(5 > 3)
print(3 >= 3)
print(true and 42)
print(false or "default")

print("--- 4. Local Variables ---")
local x = 10
local y = 20
print(x)
print(y)

print("--- 5. Assignment ---")
x = x + y
print(x)

local a, b = 1, 2
a, b = b, a
print(a)
print(b)

print("--- 6. If Statement ---")
if x > 20 {
  print("x is large")
} elseif x > 10 {
  print("x is medium")
} else {
  print("x is small")
}

print("--- 7. Nested Structures ---")
local result = 0
if true {
  local temp = 5
  if temp > 0 {
    result = temp * 2
  }
}
print(result)

print("=== All Tests Passed! ===")
