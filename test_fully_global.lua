local cont = require("continuation")

_G.x = 0
_G.k, _G.n = cont.callcc(fn(cont) {
    return cont, 3
})

print(_G.x)
_G.x = _G.x + 1

if _G.n > 0 {
    _G.k(_G.k, _G.n - 1)
} else {
    print(_G.x)
}
