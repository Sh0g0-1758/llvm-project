int g_size = 4;
constexpr int g_c = 3;
int arr[g_c];
struct P { int v = g_c; int buf[g_c]; };
enum E { e0 = g_c };
using T = decltype(g_size);
static_assert(g_c == 3);
int f(int x = g_size);
auto h() -> decltype(g_size);
auto h() -> decltype(g_size) { return g_size; }
template <int N = g_c> int t() { return N; }
struct Q {
    int m(int a = g_size) const noexcept { return a + g_size; }
    Q() : q(g_size) {}
    int q;
    [[nodiscard]] friend bool operator==(Q, Q) { return true; }
};
int f(int x) { return x + g_size; }
int main() { Q qq; return f() + h() + t() + qq.m() + (qq == qq) + P{}.v + arr[0] + e0; }
