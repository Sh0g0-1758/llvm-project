// constant-fold folds no class value, also not of a union: googletest's `buffer_()` of a
// union member is value initialized, which would become `{.ptr = nullptr}` in place of the
// whole member initializer.
// CHECK: buffer_\(\)
template <class T> struct Matcher {
  union Buffer { void *ptr; double d; };
  const int *vtable_;
  Buffer buffer_;
  Matcher() : vtable_(nullptr), buffer_() {}
};
int main() {
  Matcher<int> m;
  return m.buffer_.ptr != nullptr;
}
