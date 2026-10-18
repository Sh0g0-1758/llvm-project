// hoist-calls replaces the text of a call, here the conversion of `(res)` to bool, which may
// follow a word, eg. the alternative token `not` in a lambda no pass reprints.
struct R {
  int v;
  explicit operator bool() const { return v != 0; }
};
R make(int v) { return R{v}; }
struct Test { int (*run)(); };
Test test{[]() {
  R res = make(0);
  if (not(res)) return 0;
  return 1;
}};
int main() { return test.run(); }
