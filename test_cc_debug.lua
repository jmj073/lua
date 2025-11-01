local cont = require("continuation")

print("=== Using cc function ===")
local cc = fn() {
    print("  [Inside cc function]")
    local k = cont.callcc(fn(x) { 
        print("  [callcc executed, returning:", x, "]")
        return x 
    })
    print("  [About to return from cc]")
    return k
}

local yin = cc()
print("Got yin, now executing main code...")
print("@")

local yang = cc()
print("Got yang, now executing main code...")
print("*")

print("Now invoking yin(yang)...")
yin(yang)
