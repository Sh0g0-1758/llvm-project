template <typename T> struct S {
    template <typename... Args> S(Args&&... args);
};
template<> template<> S<char>::S(int &a, int &&b);
int main() { int a = 1; S<char> s(a, 2); }
template<> template<> S<char>::S(int &a, int &&b) {}
