local cont = require("continuation")

local x = 0
local asdf = nil
local k = cont.callcc(fn(k) {
    asdf = k
    return k
})

print(x)
x = x + 1

if k {
    k(false)
} else {
    print(x)
}

asdf(false)