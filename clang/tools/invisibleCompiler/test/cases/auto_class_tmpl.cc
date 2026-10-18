template <typename T> struct S {
    auto get() { return T{}; }
    template <typename U> auto conv(U u) { return u; }
};
struct A { auto one() { return 1; } };
int main() { S<int> s; A a; return s.get() + s.conv(2) + a.one(); }
