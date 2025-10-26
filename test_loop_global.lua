-- Test with global variables (no upvalue issue)
local cont = require("continuation")

_G.x = 0
_G.k, _G.n = cont.callcc(fn(kont) {
    return kont, 3
})

print("x =", _G.x, ", n =", _G.n)
_G.x = _G.x + 1

if _G.n > 0 {
    print("Calling k(k, n-1)...")
    _G.k(_G.k, _G.n - 1)
} else {
    print("Done! x =", _G.x)
}
