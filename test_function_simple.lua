local add = fn(a, b) {
  return a + b
}

print(add(3, 5))
print(add(10, 20))

local greet = fn(name) {
  print("Hello, " .. name .. "!")
}

greet("Lua")
greet("AST")

local square = fn(x) {
  return x * x
}

print(square(5))
print(square(12))

local max = fn(a, b) {
  if a > b {
    return a
  } else {
    return b
  }
}

print(max(10, 5))
print(max(3, 8))
