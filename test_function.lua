local add = fn(a, b) {
  return a + b
}

print(add(3, 5))

local greet = fn(name) {
  print("Hello, " .. name)
}

greet("World")

local factorial = fn(n) {
  if n <= 1 {
    return 1
  } else {
    return n * factorial(n - 1)
  }
}

print(factorial(5))
