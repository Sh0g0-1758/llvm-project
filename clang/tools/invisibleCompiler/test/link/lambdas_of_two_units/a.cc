// Two TUs with lambdas. run_a and run_b have different names, so their structs do too. Both TUs
// have a static run, so their structs get the same names, but each sits in an unnamed namespace.
// The program must print what the original does: no lambda body may replace another at link time.
extern "C" int printf(const char *, ...);
int run_b(int);
int run_c(int);
int run_a(int x) { auto f = [x] { return x + 1; }; return f(); }
static int run(int x) { auto f = [x] { return x - 1; }; return f(); }
int main() { printf("%d %d %d %d\n", run_a(10), run_b(10), run(10), run_c(10)); }
