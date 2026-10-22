// Two translation units with lambdas, in functions of different names and in static functions of
// the same name. Their structs are named after the functions, and those of the static functions
// are in an unnamed namespace, so that the copies of their call operators don't merge at link time.
extern "C" int printf(const char *, ...);
int run_b(int);
int run_c(int);
int run_a(int x) { auto f = [x] { return x + 1; }; return f(); }
static int run(int x) { auto f = [x] { return x - 1; }; return f(); }
int main() { printf("%d %d %d %d\n", run_a(10), run_b(10), run(10), run_c(10)); }
