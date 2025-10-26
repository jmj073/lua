-- 가장 간단한 value passing 테스트
local cont = require("continuation")

print("Simple value passing test")

local k
local r = cont.callcc(fn(kont) {
  k = kont
  return 111
})

print("r =", r)

if r == 111 {
  print("Calling k(222)...")
  k(222)
  print("ERROR")
} else {
  print("Success! r =", r)
}

print("Done")
