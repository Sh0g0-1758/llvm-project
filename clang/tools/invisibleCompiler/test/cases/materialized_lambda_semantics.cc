// A materialized lambda behaves as the lambda: a mutable one keeps its state between calls,
// one capturing by reference writes the variable, an init-capture is moved, and one capturing
// `this` reaches the object's members.
extern "C" int printf(const char *, ...);
struct Counter {
  int n = 0;
  int bump(int by) {
    auto add = [this, by] { n += by; return n; };
    return add() + add();
  }
};
int main() {
  int seen = 0;
  auto count = [k = 0]() mutable { return ++k; };
  count();
  auto see = [&seen](int v) { seen += v; };
  see(count());
  int total = 0;
  for (int i = 0; i < 3; ++i) [&] { total += i * seen; }();
  Counter c;
  printf("%d %d %d\n", seen, total, c.bump(2));
  return seen - 2;
}
