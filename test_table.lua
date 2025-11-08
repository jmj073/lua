local t = {| 1, 2, 3 |}
print(t[1])
print(t[2])
print(t[3])

local person = {| name = "Alice", age = 30 |}
print(person.name)
print(person.age)

local mixed = {| 10, 20, x = 100, y = 200 |}
print(mixed[1])
print(mixed[2])
print(mixed.x)
print(mixed.y)
