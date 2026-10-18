namespace ns {
struct A {
    [[nodiscard]] friend bool operator==(A, A) { return true; }
    [[deprecated("x")]] friend int f(A) { return 1; }
    friend __attribute__((noinline)) int g(A) { return 2; }
    template <typename T> [[nodiscard]] friend T h(A, T t) { return t; }
    friend int plain(A) { return 3; }
};
template <typename T> struct B {
    [[nodiscard]] friend bool operator!=(B, B) { return false; }
};
}
int main() { ns::A a; ns::B<int> b; return (a == a) + g(a) + h(a, 1) + plain(a) + (b != b); }
