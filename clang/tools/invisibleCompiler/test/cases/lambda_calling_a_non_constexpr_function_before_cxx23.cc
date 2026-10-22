// Before C++23, a function marked constexpr must be usable at compile time, or it doesn't
// compile. A lambda that calls puts can't be. So its struct's operator() isn't marked constexpr,
// while a lambda that returns 3 can be, and its operator() is.
// STD: c++17
// CHECK: inline auto operator\(\)\(\) const -> void
// CHECK: inline constexpr auto operator\(\)\(\) const -> int
int puts(const char *);
void say() {
  auto f = [] { puts("x"); };
  f();
}
int main() {
  auto three = [] { return 3; };
  static_assert(three() == 3, "");
  say();
  return three() - 3;
}
