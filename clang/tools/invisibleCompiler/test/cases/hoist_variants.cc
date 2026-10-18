int* loc();
int g(int x) { return x + 1; }
int h(int x) { return x * 2; }
template <typename T> T stoa(T v) {
    struct Save {
        Save() : e(*loc()) { *loc() = 0; }
        ~Save() { if (*loc() == 0) *loc() = e; }
        int e;
    } const save;
    struct Chk {
        int d = g(h(1));
        static bool chk(T val) { return g(h(val)) > 3; }
    };
    int x = g(h(v));
    return Chk::chk(v) ? x : Chk{}.d;
}
int main() { return stoa(1) + stoa(2L); }
