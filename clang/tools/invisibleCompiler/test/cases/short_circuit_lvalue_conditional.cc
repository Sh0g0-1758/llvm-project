// short-circuit-to-ir spells out `?:` with calls into a variable of the type of the whole
// expression, not of its first operand: `c ? half(3) : 2.5` is a double. An lvalue `?:` stays
// the object it names: assigning through it or binding a reference to it reaches x.
int x = 1, y = 2;
int &fx() { return x; }
int &fy() { return y; }
int half(int v) { return v / 2; }
int main(int argc, char **) {
  (argc > 0 ? fx() : fy()) = 5;
  int &r = argc > 0 ? fx() : fy();
  r += 1;
  double d = argc > 5 ? half(3) : 2.5;
  return x - 6 + (d == 2.5 ? 0 : 1);
}
