local cont = require("continuation")

print("=== Simple cc test ===")
local cc = fn() {
    print("  [1: Inside cc, before callcc]")
    local k = cont.callcc(fn(x) { 
        print("  [2: Inside callcc function, x=", x, "]")
        return x 
    })
    print("  [3: After callcc, k=", k, "]")
    return k
}

print("[A: Calling cc() first time]")
local yin = cc()
print("[B: Returned from cc(), yin=", yin, "]")

print("[C: Calling cc() second time]")
local yang = cc()
print("[D: Returned from cc(), yang=", yang, "]")

print("[E: Now calling yin(999)]")
yin(999)
print("[F: After yin() call - should not reach here]")
