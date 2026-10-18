int f() { return 1; }
constexpr int cf() { return 2; }
void g(int x = f()) {}
auto h() -> decltype(f()) { return 0; }
void n() noexcept(noexcept(f())) {}
void arr(int (*p)[cf()]) {}
void outer() {
    struct L { int m() { return f(); } };
    auto lam = [](int y = f()) { return y; };
}
int main() { g(); h(); n(); arr(nullptr); outer(); }
