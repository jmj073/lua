print("=== Recursive Functions Test ===")

local fn factorial(n) {
  if n <= 1 {
    return 1
  } else {
    return n * factorial(n - 1)
  }
}

print(factorial(5))
print(factorial(10))

local fn fib(n) {
  if n <= 1 {
    return n
  } else {
    return fib(n - 1) + fib(n - 2)
  }
}

print(fib(10))

fn greet(name) {
  print("Hello, " .. name .. "!")
}

greet("World")
greet("Recursive Functions")

local fn sum(n) {
  if n == 0 {
    return 0
  } else {
    return n + sum(n - 1)
  }
}

print(sum(100))

print("=== All Recursive Tests Passed! ===")
