// hoist-calls writes the conversion of the variable of a condition, `bool b(s);`, after mono
// gave `sp<int>::operator bool` a copy and reduced its template to a declaration. The template
// is then left as written, or the conversion would be undefined. `elsewhere` is defined in
// another translation unit, and clang reports the first template it lacks a definition of only.
template <class T> int elsewhere(T);
int unused() { return elsewhere(0); }
template <class T> struct sp {
  T *p;
  explicit operator bool() const { return p != nullptr; }
  T *get() const { return p; }
};
template <class T> struct wp {
  T *p;
  sp<T> lock() const { return {p}; }
};
int main() {
  int x = 1;
  wp<int> w{&x};
  if (auto s = w.lock()) return *s.get() - 1;
  return 1;
}
