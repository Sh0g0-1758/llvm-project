// An operator template is reduced to a declaration like any other, also when another template of
// its name deduces the same type, and a comparison rewritten from it, `a != b` from `a == b` or the
// reversed `3 == c`, calls its copy as what it was rewritten to.
// CHECK: template <class T> bool operator==\(V<T> ___var__\d+, V<T> ___var__\d+\);
// CHECK: template <class T> bool operator==\(W<T> ___var__\d+, int ___var__\d+\);
// CHECK: !\(static_cast<bool &&>
// CHECK-NOT: !=
template <class T> struct V { T x; };
template <class T> bool operator==(V<T> a, V<T> b) { return a.x == b.x; }
template <class T, class U> bool operator==(V<T> a, V<U> b) { return false; }
template <class T> struct W { T x; };
template <class T> bool operator==(W<T> a, int b) { return a.x == b; }
int use(V<char> a, V<char> b, W<int> c) { return (a == b) + (a != b) * 2 + (3 == c) * 4 + (c != 4) * 8; }
int main() { return use(V<char>{1}, V<char>{2}, W<int>{3}); }
