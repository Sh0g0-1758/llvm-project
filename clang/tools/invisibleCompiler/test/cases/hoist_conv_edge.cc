struct S { int v; };
struct P {
    explicit operator bool() const { return true; }
    operator int() const { return 3; }
    explicit operator S() const { return S{4}; }
};
int use(int x) { return x; }
int main() {
    P p, q;
    int r = 0;
    if (p.operator bool()) r += 1;
    if (p && q) r += 2;
    while (!p) break;
    r += use(p);
    S s = static_cast<S>(p);
    return r + s.v + (p ? 1 : 0);
}
