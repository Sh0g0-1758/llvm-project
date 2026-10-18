// Two translation units, both using a template, a member function and an inline function, and
// each with a static function and one in an unnamed namespace of the same name as the other's.
// Their outputs link: what both define is inline, so the copies and specializations merge, and
// what is internal stays internal.
template <class T> T twice(T x) { return x + x; }
struct Counter {
  int n = 0;
  int bump() { return ++n; }
};
static int local() { return 1; }
namespace { int hidden() { return 2; } }
inline int shared() { return 3; }
int from_b(int);
int main() {
  Counter c;
  c.bump();
  return twice(2) + from_b(1) + local() + hidden() + shared() + c.n - 17;
}
