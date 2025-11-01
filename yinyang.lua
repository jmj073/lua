local cont = require("continuation")
local cc = fn() { return cont.callcc(fn(k) { return k }) }

local yin = cc()
io.write("@")
local yang = cc()
io.write("*")
yin(yang)
