// What is written at the end keeps the order of the TU, a copy where its template is. `sq`
// comes before `main`, so its copy is defined before `main` is, and the constexpr initializer
// in `main` can call the copy, which constant-fold then folds: `main` moves to the end like any
// other function.
// CHECK: ^int main\(int ___var__\d+, char \*\*___var__\d+\);
// CHECK: constexpr int ___var__\d+ = 9;
// CHECK: = sq__mono[0-9a-f]+\(___var__\d+\);
template <class T> constexpr T sq(T x) { return x * x; }
int main(int argc, char **) {
  constexpr int k = sq(3);
  return k + sq(argc) - 10;
}
