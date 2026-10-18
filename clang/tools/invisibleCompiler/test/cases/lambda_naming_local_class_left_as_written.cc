// A lambda that names a class local to the function it is in, here flatbuffers' TwoValue in its
// parameters, is left as written: the struct it would become is put before the function, where
// the class isn't seen. Another lambda of the function is still materialized.
// CHECK: \[&\]\(const TwoValue &
// CHECK: struct __invisible_lambda_
template <class It, class Less> void sort2(It a, Less less) {
  if (less(a[1], a[0])) { auto t = a[0]; a[0] = a[1]; a[1] = t; }
}
int smallest(int x, int y) {
  struct TwoValue { int key; int val; };
  TwoValue pair[2] = {{x, 0}, {y, 1}};
  int compared = 0;
  sort2(pair, [&](const TwoValue &a, const TwoValue &b) { ++compared; return a.key < b.key; });
  auto twice = [](int v) { return 2 * v; };
  return pair[0].key + twice(compared) - compared;
}
int main() { return smallest(5, 3) - 4; }
