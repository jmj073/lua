-- Test: pcall 안에서 callec escape 사용 시 errorJmp 복원 확인

print("Test 1: pcall 안에서 callec escape 사용")
local ok, result = pcall(fn () {
  return continuation.callec(fn (escape) {
    escape("escaped from callec")
    return "should not reach here"
  })
})
print("ok:", ok, "result:", result)
assert(ok == true)
assert(result == "escaped from callec")

print("\nTest 2: pcall 후 다시 pcall 가능한지 확인 (errorJmp가 제대로 복원되었는지)")
local ok2, result2 = pcall(fn () {
  error("test error")
})
print("ok2:", ok2, "result2:", result2)
assert(ok2 == false)
assert(string.find(result2, "test error") ~= nil)

print("\nTest 3: 중첩된 pcall과 callec")
local ok3, result3 = pcall(fn () {
  return pcall(fn () {
    return continuation.callec(fn (escape) {
      escape("nested escape")
    })
  })
})
print("ok3:", ok3, "result3:", result3)
assert(ok3 == true)
assert(result3 == true)  -- inner pcall succeeded

print("\nTest 4: callec 후 에러 발생 시 pcall이 제대로 캐치하는지")
local ok4, result4 = pcall(fn () {
  continuation.callec(fn (escape) {
    -- escape를 호출하지 않고 정상 리턴
    return "normal return"
  })
  error("error after callec")
})
print("ok4:", ok4, "result4:", result4)
assert(ok4 == false)
assert(string.find(result4, "error after callec") ~= nil)

print("\nTest 5: callec 안에서 pcall 사용, pcall 안에서 escape 호출")
local result5 = continuation.callec(fn (escape) {
  -- callec 안에서 pcall 사용
  local ok, err = pcall(fn () {
    -- pcall 안에서 escape 호출 시도
    escape("escaped from inside pcall")
  })
  -- 만약 escape가 제대로 작동했다면 여기는 실행되지 않아야 함
  return "should not reach here: ok=" .. tostring(ok)
})
print("result5:", result5)
assert(result5 == "escaped from inside pcall")

print("\nTest 6: callec 안에서 pcall이 일반 에러를 캐치할 수 있는지")
local result6 = continuation.callec(fn (escape) {
  local ok, err = pcall(fn () {
    error("normal error in pcall")
  })
  -- pcall이 에러를 제대로 캐치했다면 여기가 실행되어야 함
  if not ok {
    return "pcall caught error: " .. tostring(err)
  }
  return "unexpected success"
})
print("result6:", result6)
assert(string.find(result6, "pcall caught error") ~= nil)
assert(string.find(result6, "normal error in pcall") ~= nil)

print("\n모든 테스트 통과!")
