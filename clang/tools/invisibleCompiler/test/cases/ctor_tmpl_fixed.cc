template <typename T> struct S {
    template <typename It, typename = void> S(It b, It e);
};
template<> template<> S<char>::S(char *b, char *e);
int main() { char buf[2]; S<char> s(buf, buf + 1); }
template<> template<> S<char>::S(char *b, char *e) {}
