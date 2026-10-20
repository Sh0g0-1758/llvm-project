// A function that is neither a template nor constexpr, called by a template's default argument,
// only needs to be declared before it: it is moved like any other and isn't given a copy.
// CHECK-NOT: base__mono
int base() { return 10; }
template <class T> T add(T x, T y = T(base())) { return x + y; }
int use(int a) { return add(a); }
int main() { return use(5); }
