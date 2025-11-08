print("=== Complete AST Implementation Test ===")

print("--- 1. Basic Types ---")
print(42)
print("hello")
print(true)
print(nil)

print("--- 2. Operators ---")
print(2 + 3 * 4)
print(10 >= 5)
print(true and 42)

print("--- 3. Variables ---")
local x = 10
local y = 20
print(x + y)

print("--- 4. Assignment ---")
x = 100
print(x)

print("--- 5. Tables ---")
local t = {| 1, 2, 3 |}
print(t[1])
print(t[2])

local person = {| name = "Alice", age = 30 |}
print(person.name)
print(person.age)

print("--- 6. If Statements ---")
if x > 50 {
  print("x is large")
}

print("--- 7. Functions ---")
local add = fn(a, b) {
  return a + b
}
print(add(5, 7))

local greet = fn(name) {
  return "Hello, " .. name
}
print(greet("Lua"))

print("--- 8. Nested Structures ---")
local calc = fn(op, a, b) {
  if op == "add" {
    return a + b
  } elseif op == "mul" {
    return a * b
  } else {
    return 0
  }
}

print(calc("add", 3, 4))
print(calc("mul", 3, 4))

print("=== All Features Working! ===")
