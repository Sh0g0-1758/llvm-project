// What decl-before-def and monomorphization declare is printed so that it parses back:
//  - a class hidden by a function of its name, POSIX's `struct stat` and `stat()`, which
//    googletest names through `typedef struct stat StatStruct`, with its keyword;
//  - a function returning a pointer to an array with its `noexcept` inside the declarator,
//    `const char (*f(...) noexcept)[3]`, as libstdc++'s reference_wrapper of `const char[11]`.
// CHECK: struct stat \*
struct stat { int size; };
int stat(const char *, struct stat *s) { s->size = 4; return 0; }
typedef struct stat StatStruct;
inline int Stat(const char *path, StatStruct *buf) { return stat(path, buf); }
template <class T> struct ref { T *p; T &get() const noexcept { return *p; } };
const char abc[3] = {'a', 'b', 0};
template <class T> T *address(T &x) noexcept { return &x; }
int main() {
  StatStruct s;
  Stat("x", &s);
  ref<const char[3]> r{&abc};
  const char (*p)[3] = address(r.get());
  return s.size - 4 + (*p)[0] - 'a';
}
