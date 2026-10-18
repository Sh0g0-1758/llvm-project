namespace ns {
int sum(int n, __builtin_va_list ap) { int s = 0; for (int i = 0; i < n; i++) s += __builtin_va_arg(ap, int); return s; }
template <typename T> T first(T, __builtin_va_list ap) { return __builtin_va_arg(ap, T); }
struct V { int get(__builtin_va_list ap) { return __builtin_va_arg(ap, int); } };
int fwd(int n, ...) { __builtin_va_list ap; __builtin_va_start(ap, n); int r = sum(n, ap); __builtin_va_end(ap); return r; }
int fwd2(int n, ...) { __builtin_va_list ap; __builtin_va_start(ap, n); int r = first(n, ap) + V{}.get(ap); __builtin_va_end(ap); return r; }
}
int main() { return ns::fwd(2, 1, 2) + ns::fwd2(1, 5, 6); }
