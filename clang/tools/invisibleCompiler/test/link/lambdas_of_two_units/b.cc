int run_b(int x) { auto f = [x] { return x * 100; }; return f(); }
static int run(int x) { auto f = [x] { return [x] { return x * 2; }() + 1; }; return f(); }
int run_c(int x) { return run(x); }
