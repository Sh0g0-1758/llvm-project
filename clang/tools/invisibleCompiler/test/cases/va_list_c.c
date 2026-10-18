#include <stdarg.h>
int apply(int (*f)(int, va_list), int n, ...) { va_list ap; va_start(ap, n); int r = f(n, ap); va_end(ap); return r; }
int first(int n, va_list ap) { return n + va_arg(ap, int); }
int main(void) { return apply(first, 1, 2); }
