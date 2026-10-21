// Two operator templates of the same signature that both deduce an instantiation, told apart
// only by their constraints, are reduced to declarations like any other template. The
// defaulted comparison calls them by name, so the instantiations it calls are specialized, and
// clang matches each specialization to the more constrained template that applies.
template <class T>
concept Any = sizeof(T) > 0;
template <class T>
concept Wide = Any<T> && (sizeof(T) > 1);
template <class T> struct Box {
  T v;
};
template <class T>
  requires Any<T>
bool operator==(const Box<T> &a, const Box<T> &b) {
  return a.v + 1 == b.v;
}
template <class T>
  requires Wide<T>
bool operator==(const Box<T> &a, const Box<T> &b) {
  return a.v == b.v;
}
struct Pair {
  Box<int> a;
  Box<char> b;
  bool operator==(const Pair &) const = default;
};
int main() {
  Pair p{{1}, {2}}, q{{1}, {3}};
  Box<int> x{4};
  return (p == q) * 4 + (x == Box<int>{4}) * 2 + (Box<char>{1} == Box<char>{2});
}
// CHECK: requires Any<T> bool operator==\(const Box<T> &___var__\d+, const Box<T> &___var__\d+\);
// CHECK: requires Wide<T> bool operator==\(const Box<T> &___var__\d+, const Box<T> &___var__\d+\);
// CHECK: template<> \[\[gnu::used\]\] inline bool operator==\(const Box<char> &
// CHECK: template<> \[\[gnu::used\]\] inline bool operator==<int>\(const Box<int> &
