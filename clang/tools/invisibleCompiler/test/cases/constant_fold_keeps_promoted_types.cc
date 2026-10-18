// constant-fold writes a value of a type that promotes to int with a cast to that type:
// `char('$')` as `36` is an int and would pick the template constructor.
// CHECK: \(char\)36
// CHECK-NOT: \(36\)
extern "C" int printf(const char *, ...);
struct D {
  D(char) { printf("char\n"); }
  template <class S> D(const S &) { printf("template\n"); }
};
template <class T> void show(T) { printf("%zu\n", sizeof(T)); }
int main() {
  D d(char('$'));
  show((unsigned char)(200 + 100));
  show(short(-5 * 3));
  show((signed char)-128);
  show(char16_t(65));
  auto c = char(10 * 2);
  show(c);
  int a[(char)3 + 1] = {};
  switch (a[0]) { case (char)(1 + 1): break; }
  show(true && 1);
}
