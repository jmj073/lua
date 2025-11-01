local cc = fn() { return continuation.callcc(fn(k) { return k }) }

local yin = cc()
io.write("@")
local yang = cc()
io.write("*")
yin(yang)
