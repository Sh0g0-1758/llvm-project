// materialize-lambda puts the struct before the enclosing function and the attributes written
// on it, not before the implicit visibility attribute that `#pragma GCC visibility push`, as
// libstdc++ has around <compare>, gives it at the pragma.
// CHECK: struct __invisible_lambda_0
#pragma GCC visibility push(default)
template <class T> int next(T x) {
  auto inc = [](T y) { return y + 1; };
  return inc(x);
}
[[gnu::noinline]] int twice(int x) {
  auto mul = [x] { return 2 * x; };
  return mul();
}
#pragma GCC visibility pop
int main() { return next(1) + twice(1) - 4; }
