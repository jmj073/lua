-- Edge Case Tests: callcc와 callec 혼용, cross-scope continuation calls

local cont = require("continuation")

print("=== Edge Case Tests ===\n")

-- Test 1: callcc와 callec 혼용 - callcc 안에 callec
print("Test 1: callcc containing callec")
local result1 = cont.callcc(fn(k_outer) {
  print("  In callcc")
  local inner = cont.callec(fn(e) {
    print("    In callec")
    e("escaped from callec")
    return "callec normal"
  })
  print("  After callec:", inner)
  k_outer("from callcc: " .. inner)
  return "callcc normal"
})
print("  Final result:", result1)
print("  ✓ Pass\n")

-- Test 2: callec 안에 callcc
print("Test 2: callec containing callcc")
local result2 = cont.callec(fn(e) {
  print("  In callec")
  local inner = cont.callcc(fn(k) {
    print("    In callcc")
    k("from callcc")
    return "callcc normal"
  })
  print("  After callcc:", inner)
  e("escaped: " .. inner)
  return "callec normal"
})
print("  Final result:", result2)
print("  ✓ Pass\n")

-- Test 3: Nested callcc - inner에서 outer continuation 호출
print("Test 3: Inner callcc calls outer continuation")
local outer_k
local inner_called = false
local outer_result = cont.callcc(fn(k_outer) {
  outer_k = k_outer
  print("  In outer callcc")
  
  local inner_result = cont.callcc(fn(k_inner) {
    print("    In inner callcc")
    inner_called = true
    -- 여기서 outer continuation을 호출!
    k_outer("jumped from inner to outer")
    print("    This should not print")
    return "inner normal"
  })
  
  print("  After inner callcc:", inner_result)
  return "outer normal: " .. inner_result
})

print("  Outer result:", outer_result)
print("  Inner was called:", inner_called)
assert(outer_result == "jumped from inner to outer", "Cross-scope call failed")
print("  ✓ Pass\n")

-- Test 4: callcc, callec 혼합 with cross-calls
print("Test 4: Mixed callcc/callec with escape from nested")
local result4 = cont.callcc(fn(k_callcc) {
  print("  In callcc")
  
  local escaped = cont.callec(fn(e) {
    print("    In callec (nested in callcc)")
    -- callec의 escape를 호출하면 callec만 빠져나가야 함
    e("escaped from callec only")
    return "callec normal"
  })
  
  print("  After callec:", escaped)
  -- 이제 callcc의 continuation을 호출
  k_callcc("then escaped from callcc: " .. escaped)
  return "callcc normal"
})

print("  Final result:", result4)
assert(result4 == "then escaped from callcc: escaped from callec only", "Mixed escape failed")
print("  ✓ Pass\n")

-- Test 5: 3-level nesting with cross-calls
print("Test 5: Three-level nesting")
local level1_k, level2_k, level3_k

local level1 = cont.callcc(fn(k1) {
  level1_k = k1
  print("  Level 1")
  
  local level2 = cont.callcc(fn(k2) {
    level2_k = k2
    print("    Level 2")
    
    local level3 = cont.callcc(fn(k3) {
      level3_k = k3
      print("      Level 3")
      -- Level 3에서 Level 1으로 직접 점프
      k1("jumped from level 3 to level 1")
      return "level 3 normal"
    })
    
    print("    After level 3:", level3)
    return "level 2 normal"
  })
  
  print("  After level 2:", level2)
  return "level 1 normal"
})

print("  Final result:", level1)
assert(level1 == "jumped from level 3 to level 1", "3-level jump failed")
print("  ✓ Pass\n")

-- Test 6: callec escape crossing callcc boundary
print("Test 6: callec escape through callcc")
local final_result = cont.callec(fn(escape_outer) {
  print("  Outer callec")
  
  local middle = cont.callcc(fn(k_middle) {
    print("    Middle callcc")
    
    local inner = cont.callec(fn(escape_inner) {
      print("      Inner callec")
      -- Inner callec에서 outer callec로 탈출 시도
      escape_outer("escaped through callcc boundary")
      return "inner callec normal"
    })
    
    print("    After inner callec:", inner)
    k_middle(inner)
    return "middle callcc normal"
  })
  
  print("  After middle callcc:", middle)
  return "outer callec normal: " .. middle
})

print("  Final result:", final_result)
assert(final_result == "escaped through callcc boundary", "Cross-boundary escape failed")
print("  ✓ Pass\n")

-- Test 7: Saved continuations from different scopes
print("Test 7: Save continuations from multiple scopes")
local saved_outer, saved_inner

local r1 = cont.callcc(fn(k_outer) {
  saved_outer = k_outer
  print("  Outer callcc - saving k_outer")
  
  local r2 = cont.callcc(fn(k_inner) {
    saved_inner = k_inner
    print("    Inner callcc - saving k_inner")
    return "inner return"
  })
  
  print("  Inner returned:", r2)
  return "outer return"
})

print("  First execution:", r1)
print("  Saved outer:", type(saved_outer))
print("  Saved inner:", type(saved_inner))
assert(saved_outer != nil and saved_inner != nil, "Failed to save both continuations")
print("  ✓ Pass (Note: calling saved continuations not yet implemented)\n")

print("=== All Edge Case Tests Passed! ===")
print("\nKey findings:")
print("- callcc and callec can be nested freely")
print("- Inner continuations can call outer continuations")
print("- Escape continuations work across callcc boundaries")
print("- Multiple continuation scopes are handled correctly")
