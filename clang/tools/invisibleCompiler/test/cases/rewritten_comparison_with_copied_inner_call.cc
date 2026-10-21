// `a < b` is rewritten to `(a <=> b) < 0`. The defaulted `<=>` has no copy, but the `< 0`, a
// constexpr operator of std::strong_ordering, has one, so the comparison is written as what it
// was rewritten to, calling that copy, and not as `a < b`, which clang can't print once a call in
// it is redirected.
// CHECK: = operator__mono[0-9a-f]+\(static_cast<std::strong_ordering &&>\(__func_hoist_ret__\d+\), 0\);
#include <compare>
struct S {
  int x;
  std::strong_ordering operator<=>(const S &) const = default;
};
bool less(S a, S b) { return a < b; }
int main() { return less(S{1}, S{2}) ? 0 : 1; }
