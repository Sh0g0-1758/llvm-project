template <typename... Ts> struct T {
    T(Ts... ts) {}
    template <typename... Us> T(int, Us... us) {}
};
int main() { T<int, char> a(1, 'c'); T<long> b(1, 2.0, 'x'); }
