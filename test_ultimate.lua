print("=== Ultimate AST Compiler Test ===")

print("--- 1. Function Statements ---")
fn add(a, b) {
  return a + b
}
print(add(5, 3))

print("--- 2. Local Functions with Recursion ---")
local fn factorial(n) {
  if n <= 1 {
    return 1
  }
  return n * factorial(n - 1)
}
print(factorial(6))

print("--- 3. Higher-Order Functions ---")
local fn apply(f, x) {
  return f(x)
}

local fn square(x) {
  return x * x
}

print(apply(square, 7))

print("--- 4. Functions with Tables ---")
local calculator = {|
  add = fn(a, b) { return a + b },
  mul = fn(a, b) { return a * b }
|}

print(calculator.add(10, 20))
print(calculator.mul(5, 6))

print("--- 5. Closures (limited) ---")
local fn make_counter() {
  local count = 0
  return fn() {
    count = count + 1
    return count
  }
}

local counter = make_counter()
print(counter())
print(counter())
print(counter())

print("--- 6. Complex Nested Functions ---")
local fn outer(x) {
  return fn(y) {
    return x + y
  }
}

local add5 = outer(5)
print(add5(10))

print("=== All Ultimate Tests Passed! ===")
