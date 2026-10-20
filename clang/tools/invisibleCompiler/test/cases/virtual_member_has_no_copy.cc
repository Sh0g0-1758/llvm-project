// A virtual member of a class template is written as an explicit specialization, which the
// vtable points at, and isn't given a copy: a call that doesn't dispatch, `q.Sq<int>::area()`,
// calls the specialization as well.
// CHECK-NOT: area__mono
// CHECK: \.Sq<int>::area\(\)
struct Shape {
  virtual int area() const = 0;
};
template <class T> struct Sq : Shape {
  T s;
  Sq(T s) : s(s) {}
  int area() const override { return s * s; }
};
int use(Sq<int> &q, Shape &s) { return q.area() + s.area() + q.Sq<int>::area(); }
int main() {
  Sq<int> q(3);
  return use(q, q);
}
