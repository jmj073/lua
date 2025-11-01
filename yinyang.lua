local cont = require("continuation")

local yin = cont.callcc(fn(k) { return k })
print("@")
local yang = cont.callcc(fn(k) { return k })
print("*")
yin(yang)