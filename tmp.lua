fn cons(a, b) {
  return fn (k) { return k(a, b) }
}

fn car(pair) {
  return pair(fn (a, b) { return a })
}

fn cdr(pair) {
  return pair(fn (a, b) { return b })
}

fn foreach(f, ls) {
  if (ls == nil) { return }
  f(car(ls))
  return foreach(f, cdr(ls))
}

a = cons(1, cons(2, cons(3, nil)))

foreach(print, a)

fn foo(k) {
  k(4)
}

print(continuation.callec(fn (k) { foo(k) }))
