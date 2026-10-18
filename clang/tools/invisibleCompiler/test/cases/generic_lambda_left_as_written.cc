// A generic lambda's call operator is a template, which the passes after decl-before-def leave
// as written: here one materialize-lambda keeps, in the lambda of a global's initializer, as
// in kakoune's unit tests. With an operator== in scope, its dependent `==` is a call, which
// hoist-calls would give a variable of `<dependent type>`.
// CHECK-NOT: dependent type
struct Flag { bool on; };
bool operator==(Flag a, Flag b) { return a.on == b.on; }
template <class T> Flag parse(T) { return {true}; }
void fail(const char *) {}
struct Test { int (*run)(); };
Test test{[]() {
  auto check = [&](auto hint) {
    auto parsed = parse(hint);
    Flag expected{true};
    do {
      if (not(parsed == expected)) fail("parsed");
    } while (false);
    return 0;
  };
  return check(1);
}};
int main() { return test.run(); }
