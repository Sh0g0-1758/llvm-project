// A pass after decl-before-def writes what instantiates a template it reduced to a declaration:
// short-circuit-to-ir assigns the result of `?:`, whose move assignment's noexcept calls nothrow().
// CHECK: __short_circuit_result_0
template <class T> struct traits { static constexpr bool nothrow() { return sizeof(T) > 1; } };
template <class T> struct str {
  T *p = nullptr;
  str() = default;
  str(const str &) = default;
  str &operator=(const str &) = default;
  str &operator=(str &&o) noexcept(traits<T>::nothrow()) { p = o.p; return *this; }
  str sub(int n) const { str s; s.p = p + n; return s; }
};
struct dir { str<int> s; dir(str<int> &&s) : s(s) {} };
int main(int argc, char **) {
  str<int> b;
  dir d(argc > 1 ? b.sub(1) : b);
  return d.s.p != nullptr;
}
