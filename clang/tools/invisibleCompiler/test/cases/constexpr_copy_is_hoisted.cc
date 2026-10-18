// Monomorphization prints a copy constexpr when what it copies can be a constant expression, and
// the passes after decl-before-def still rewrite it: only what stays where it is written is
// evaluated by the compiler, which a copy never is. Its nested calls are hoisted.
// CHECK-MONO: quad__mono[0-9a-f]+\(int ___var__[0-9]+\)\{\s*int __func_hoist_ret__[0-9]+ = twice__mono
template <class T> constexpr T twice(T x) { return x + x; }
template <class T> constexpr T quad(T x) { return twice(twice(x)); }
int main(int argc, char **) { return quad(argc) - 4; }
