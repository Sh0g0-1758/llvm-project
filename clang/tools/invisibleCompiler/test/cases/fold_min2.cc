template <typename C> struct SV {
    long f(long a = 1 + 1, long b = 2 + 2) { return a + b; }
};
int main() { SV<char> s; return s.f(0, 0); }
