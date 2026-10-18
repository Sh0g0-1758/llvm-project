int f() { return 1; }
constexpr int cf() { return 2; }
struct G { int v = f(); };
int outer() {
    struct L {
        int a = f();
        int b[cf()];
        int m() { return f(); }
    };
    enum E { A = cf(), B };
    auto lam = [c = f()]() { return c; };
    L l;
    return l.a + l.m() + B + lam();
}
int main() { G g; return outer() + g.v; }
