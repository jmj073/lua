print("=== Comprehensive AST Test ===")

print("--- Arithmetic ---")
print(1 + 2 * 3)
print((1 + 2) * 3)

print("--- Comparison ---")
print(5 > 3)
print(3 >= 3)

print("--- Logical ---")
print(true and 42)
print(false or "hello")

print("--- If/Elseif/Else ---")
if 10 > 20 {
  print("wrong")
} elseif 10 < 20 {
  print("correct: 10 < 20")
} else {
  print("wrong")
}

print("--- Nested If ---")
if true {
  if 5 > 3 {
    print("nested if works")
  }
}

print("=== All Tests Passed! ===")
