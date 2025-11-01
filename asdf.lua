local cont = require("continuation")

local x = 0
local n, k
k, n = cont.callcc(fn(cont) {
    return cont, 3
})

print(x)
x = x + 1

if n > 0 {
    k(k, n - 1)
} else {
    print(x)
}