// A copy is printed outside its class, with every name qualified, also inside braces, whose
// syntactic form the printer prints: googletest's `static constexpr VTable kVTable =
// {&MatchAndExplainImpl<P>}` would otherwise name a member the copy doesn't see.
// CHECK-MONO: table__mono[0-9a-f]+\(M<int> \*__this\)\{
template <class T> struct M {
  struct VT { int (*f)(int); };
  template <class P> static int impl(int x) { return x + P::v; }
  template <class P> const VT *table() {
    static constexpr VT t = {&impl<P>};
    return &t;
  }
};
struct One { static constexpr int v = 1; };
int main() {
  M<int> m;
  return m.table<One>()->f(1) - 2;
}
