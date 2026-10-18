// access-specifier makes members public, which a trait of a special member can tell: here
// fmt's buffer, whose protected destructor a static_assert checks. When making them public
// doesn't compile, constructors, destructors and assignments keep their access.
// CHECK: protected:\s*~buffer\(\)
template <class T> struct is_destructible {
  template <class U, class = decltype(static_cast<U *>(nullptr)->~U())> static char test(int);
  template <class> static long test(...);
  static constexpr bool value = sizeof(test<T>(0)) == 1;
};
template <class T> class buffer {
  T *ptr_;
protected:
  buffer(T *p) : ptr_(p) {}
  ~buffer() = default;
public:
  T *data() const { return ptr_; }
};
template <class T> struct owned : buffer<T> {
  owned(T *p) : buffer<T>(p) {}
};
static_assert(!is_destructible<buffer<int>>::value, "buffer's destructor is protected");
int main() {
  int x = 7;
  owned<int> b(&x);
  return *b.data() - 7;
}
