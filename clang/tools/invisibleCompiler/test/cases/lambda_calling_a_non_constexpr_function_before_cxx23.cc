// clang makes a lambda's call operator constexpr when its body only has what a constexpr function
// may, also when it calls `puts`, but before C++23 a call operator declared constexpr that can
// never be evaluated at compile time doesn't compile. Such a lambda becomes a struct whose
// operator() isn't constexpr, and one that can still is.
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
