local cont = require("continuation")

local yin = cont.callcc(fn(k) { return k })
io.write("@")
local yang = cont.callcc(fn(k) { return k })
io.write("*")
yin(yang)
