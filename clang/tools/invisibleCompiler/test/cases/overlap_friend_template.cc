constexpr int g_n = 3;
struct A {
    template <int N = g_n> friend int f(A) { return N; }
};
int main() { return f(A{}); }
