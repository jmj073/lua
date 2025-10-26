-- Minimal test to isolate the problem
print("Starting minimal test...")

local cont = require("continuation")
print("Library loaded")

-- Try to call callcc with simplest possible function
print("Attempting callcc...")
local status, err = pcall(fn() {
  cont.callcc(fn(k) {
    print("Got continuation:", k)
  })
})

if status {
  print("Success!")
} else {
  print("Error:", err)
}
