// A defaulted operator== calls the members' operator==, from an implicit member where a call
// can't be redirected to a copy, so the member keeps its name. `p.a != q.a`, rewritten from
// `V<int>::operator==`, calls its copy, and `p != q`, rewritten from the defaulted one, which
// has no copy, is printed as written.
template <class T> struct V {
  T x;
  bool operator==(const V &o) const { return x == o.x; }
};
struct Box {
  V<int> a;
  V<long> b;
  bool operator==(const Box &) const = default;
};
int main() {
  Box p{{1}, {2}}, q{{1}, {3}};
  return (p == q) * 8 + (p != q) * 4 + (p.a != q.a) * 2 + (p.a == q.a);
}
// CHECK: template<> \[\[gnu::used\]\] inline bool V<int>::operator==\(const V<int> &
// CHECK: template<> \[\[gnu::used\]\] inline bool V<long>::operator==\(const V<long> &
// CHECK: \(___var__\d+ != ___var__\d+\) \* 4 \+ \(!\(static_cast<bool &&>\(__func_hoist_ret__\d+\)\)\) \* 2
