// The calls hoisted out of a labeled statement are put after its label, so that a goto to the
// label still makes them: `L: x = f(g());` calls g() on every jump, in a block of their own, as
// a jump past their initialization is ill-formed.
// CHECK: L:\s*\{\s*int __func_hoist_ret
int counter = 0;
int g() { return ++counter; }
int f(int v) { return v; }
int skip(int c) {
  int r = 0;
  if (c == 0) goto end;
  r = f(g());
end:
  r += f(g());
  // the last statement of a statement expression is its value, and isn't put in a block
  return r + ({ int a = 1; f(g()) + a; });
}
int main() {
  int x = 0, n = 0;
  goto L;
  x = 100;
L:
  x = f(g());
  n++;
  if (n < 2) goto L;
  return x - 2 + skip(0) - skip(0) + 1;
}
