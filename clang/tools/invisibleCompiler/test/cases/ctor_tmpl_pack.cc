template <typename T> struct S {
    template <typename... Args> S(Args&&... args) {}
};
int main() { int a = 1; S<char> s(a, 2); }
