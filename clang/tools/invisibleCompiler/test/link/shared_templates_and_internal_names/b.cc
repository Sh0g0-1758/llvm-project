template <class T> T twice(T x) { return x + x; }
struct Counter {
  int n = 0;
  int bump() { return ++n; }
};
static int local() { return 10; }
namespace { int hidden() { return 20; } }
inline int shared() { return 3; }
int from_b(int v) {
  Counter c;
  c.bump();
  return twice(v) + local() - 10 + hidden() - 20 + shared() + c.n;
}
