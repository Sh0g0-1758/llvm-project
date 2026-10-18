// `a && b` and `a || b` are of type bool in C++: the variable short-circuit-to-ir spells them
// into picks the same overload, and `auto` deduces the same type.
extern "C" int printf(const char *, ...);
void show(bool) { printf("bool\n"); }
void show(int) { printf("int\n"); }
int one() { return 1; }
int main() {
  show(one() > 0 && one() < 2);
  show(one() > 5 || one() < 2);
  auto both = one() > 0 && one() > 0;
  printf("%zu\n", sizeof(both));
}
