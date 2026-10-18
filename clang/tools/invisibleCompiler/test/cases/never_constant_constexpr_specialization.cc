// An instantiation of a constexpr template that can never be a constant expression, here as
// it constructs a S, is printed without constexpr: before C++23 clang rejects such a
// constexpr function unless it is instantiated from a template.
// STD: c++17
// CHECK-NOT: constexpr P<S, int>::P
extern "C" int puts(const char *);
struct S { S(const char *s) { puts(s); } };
template <class A, class B> struct P {
  A a; B b;
  template <class U, class V> constexpr P(U &&u, V &&v) : a(u), b(v) {}
};
template <class T> constexpr T twice(T t) { return t + t; }
// a member of a class template keeps the `constexpr` of its declaration in the class
template <class T> struct Box {
  T t;
  constexpr T get() const { return t; }
};
static_assert(twice(2) == 4, "a constant expression still evaluates the template");
int main() {
  P<S, int> p("hello", 1);
  Box<int> box{2};
  return p.b + twice(1) + box.get() - 5;
}
